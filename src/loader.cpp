#include "gdml/loader.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <cerrno>
#include <dlfcn.h>
#endif

namespace {

void log_message(const GDML_HostApi& host, GDML_LogLevel level, const char* message) {
    if (host.log != nullptr) {
        host.log(host.context, level, message);
    }
}

void log_formatted(const GDML_HostApi& host, GDML_LogLevel level, const std::string& message) {
    log_message(host, level, message.c_str());
}

// Windows file systems are case-insensitive, so "Mod.dll" and "mod.dll" are
// the same file; comparing byte-for-byte would let the same plugin load twice
// through differently-cased paths.
bool same_plugin_path(const std::string& left, const std::string& right) {
#if defined(_WIN32)
    return _stricmp(left.c_str(), right.c_str()) == 0;
#else
    return left == right;
#endif
}

class DynamicLibrary {
public:
    explicit DynamicLibrary(const char* utf8_path) {
#if defined(_WIN32)
        const int required = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, utf8_path, -1, nullptr, 0
        );
        if (required <= 0) {
            last_error_ = static_cast<unsigned long>(GetLastError());
            return;
        }

        std::wstring wide_path(static_cast<size_t>(required), L'\0');
        if (MultiByteToWideChar(
                CP_UTF8, MB_ERR_INVALID_CHARS, utf8_path, -1, wide_path.data(), required
            ) <= 0) {
            last_error_ = static_cast<unsigned long>(GetLastError());
            return;
        }
        handle_ = LoadLibraryW(wide_path.c_str());
        if (handle_ == nullptr) {
            last_error_ = static_cast<unsigned long>(GetLastError());
        }
#else
        handle_ = dlopen(utf8_path, RTLD_NOW | RTLD_LOCAL);
        if (handle_ == nullptr) {
            last_error_ = static_cast<unsigned long>(errno);
        }
#endif
    }

    ~DynamicLibrary() {
        close();
    }

    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;

    DynamicLibrary(DynamicLibrary&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)),
          last_error_(std::exchange(other.last_error_, 0)) {}

    DynamicLibrary& operator=(DynamicLibrary&& other) noexcept {
        if (this != &other) {
            close();
            handle_ = std::exchange(other.handle_, nullptr);
            last_error_ = std::exchange(other.last_error_, 0);
        }
        return *this;
    }

    explicit operator bool() const {
        return handle_ != nullptr;
    }

    void* symbol(const char* name) const {
        if (handle_ == nullptr) {
            return nullptr;
        }
#if defined(_WIN32)
        return reinterpret_cast<void*>(GetProcAddress(handle_, name));
#else
        return dlsym(handle_, name);
#endif
    }

    unsigned long last_error() const {
        return last_error_;
    }

    void close() {
        if (handle_ == nullptr) {
            return;
        }
#if defined(_WIN32)
        FreeLibrary(handle_);
#else
        dlclose(handle_);
#endif
        handle_ = nullptr;
    }

private:
#if defined(_WIN32)
    HMODULE handle_ = nullptr;
#else
    void* handle_ = nullptr;
#endif
    unsigned long last_error_ = 0;
};

struct LoadedPlugin {
    std::string path;
    DynamicLibrary library;
    GDML_PluginShutdownFn shutdown;

    LoadedPlugin(std::string plugin_path, DynamicLibrary&& plugin_library,
                 GDML_PluginShutdownFn plugin_shutdown)
        : path(std::move(plugin_path)),
          library(std::move(plugin_library)),
          shutdown(plugin_shutdown) {}

    LoadedPlugin(LoadedPlugin&&) = default;
    LoadedPlugin& operator=(LoadedPlugin&&) = default;
    LoadedPlugin(const LoadedPlugin&) = delete;
    LoadedPlugin& operator=(const LoadedPlugin&) = delete;
};

} // namespace

struct GDML_Loader {
    GDML_HostApi host;
    std::vector<LoadedPlugin> plugins;
};

GDML_Result GDML_CALL gdml_loader_create(
    const GDML_HostApi* host,
    GDML_Loader** out_loader
) {
    if (out_loader == nullptr) {
        return GDML_ERROR_INVALID_ARGUMENT;
    }
    *out_loader = nullptr;

    if (host == nullptr) {
        return GDML_ERROR_INVALID_ARGUMENT;
    }
    if (host->struct_size < sizeof(GDML_HostApi) ||
        host->api_version != GDML_HOST_API_VERSION) {
        return GDML_ERROR_INCOMPATIBLE_API;
    }

    try {
        auto* loader = new GDML_Loader{};
        loader->host = *host;
        *out_loader = loader;
        return GDML_OK;
    } catch (const std::bad_alloc&) {
        return GDML_ERROR_OUT_OF_MEMORY;
    }
}

GDML_Result GDML_CALL gdml_loader_load_plugin(GDML_Loader* loader, const char* utf8_path) {
    if (loader == nullptr || utf8_path == nullptr || utf8_path[0] == '\0') {
        return GDML_ERROR_INVALID_ARGUMENT;
    }

    if (std::any_of(loader->plugins.begin(), loader->plugins.end(),
                    [utf8_path](const LoadedPlugin& plugin) {
                        return same_plugin_path(plugin.path, utf8_path);
                    })) {
        return GDML_ERROR_ALREADY_LOADED;
    }

    try {
        DynamicLibrary library(utf8_path);
        if (!library) {
            log_formatted(loader->host, GDML_LOG_ERROR,
                          std::string("Could not load the plugin library (system error ") +
                          std::to_string(library.last_error()) + ").");
            return GDML_ERROR_LOAD_FAILED;
        }

        auto init = reinterpret_cast<GDML_PluginInitFn>(
            library.symbol("gdml_plugin_init")
        );
        if (init == nullptr) {
            log_message(loader->host, GDML_LOG_ERROR, "Plugin is missing gdml_plugin_init.");
            return GDML_ERROR_MISSING_ENTRY_POINT;
        }

        auto shutdown = reinterpret_cast<GDML_PluginShutdownFn>(
            library.symbol("gdml_plugin_shutdown")
        );

        loader->plugins.reserve(loader->plugins.size() + 1);
        LoadedPlugin plugin(utf8_path, std::move(library), shutdown);

        // Plugins are third-party code called through a C ABI. An exception
        // escaping them would unwind into the host and terminate the process,
        // so guard the boundary and treat a throw as an init failure.
        bool init_ok = false;
        try {
            init_ok = init(&loader->host) != 0;
        } catch (const std::exception& error) {
            log_formatted(loader->host, GDML_LOG_ERROR,
                          std::string("Plugin init threw an exception: ") + error.what());
        } catch (...) {
            log_message(loader->host, GDML_LOG_ERROR,
                        "Plugin init threw an unknown exception.");
        }

        if (!init_ok) {
            log_message(loader->host, GDML_LOG_ERROR, "Plugin initialization failed.");
            return GDML_ERROR_PLUGIN_INIT_FAILED;
        }

        loader->plugins.push_back(std::move(plugin));
        log_message(loader->host, GDML_LOG_INFO, "Plugin loaded.");
        return GDML_OK;
    } catch (const std::bad_alloc&) {
        return GDML_ERROR_OUT_OF_MEMORY;
    }
}

void GDML_CALL gdml_loader_unload_all(GDML_Loader* loader) {
    if (loader == nullptr) {
        return;
    }

    for (auto plugin = loader->plugins.rbegin(); plugin != loader->plugins.rend(); ++plugin) {
        if (plugin->shutdown == nullptr) {
            continue;
        }
        try {
            plugin->shutdown();
        } catch (const std::exception& error) {
            log_formatted(loader->host, GDML_LOG_ERROR,
                          std::string("Plugin shutdown threw an exception: ") + error.what());
        } catch (...) {
            log_message(loader->host, GDML_LOG_ERROR,
                        "Plugin shutdown threw an unknown exception.");
        }
    }
    loader->plugins.clear();
}

void GDML_CALL gdml_loader_destroy(GDML_Loader* loader) {
    if (loader != nullptr) {
        gdml_loader_unload_all(loader);
        delete loader;
    }
}
