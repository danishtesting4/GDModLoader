#include "gdml/loader.h"

#include <algorithm>
#include <new>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

void log_message(const GDML_HostApi& host, GDML_LogLevel level, const char* message) {
    if (host.log != nullptr) {
        host.log(host.context, level, message);
    }
}

class DynamicLibrary {
public:
    explicit DynamicLibrary(const char* utf8_path) {
#if defined(_WIN32)
        const int required = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, utf8_path, -1, nullptr, 0
        );
        if (required <= 0) {
            return;
        }

        std::wstring wide_path(static_cast<size_t>(required), L'\0');
        if (MultiByteToWideChar(
                CP_UTF8, MB_ERR_INVALID_CHARS, utf8_path, -1, wide_path.data(), required
            ) <= 0) {
            return;
        }
        handle_ = LoadLibraryW(wide_path.c_str());
#else
        handle_ = dlopen(utf8_path, RTLD_NOW | RTLD_LOCAL);
#endif
    }

    ~DynamicLibrary() {
        close();
    }

    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;

    DynamicLibrary(DynamicLibrary&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

    DynamicLibrary& operator=(DynamicLibrary&& other) noexcept {
        if (this != &other) {
            close();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    explicit operator bool() const {
        return handle_ != nullptr;
    }

    void* symbol(const char* name) const {
#if defined(_WIN32)
        return reinterpret_cast<void*>(GetProcAddress(handle_, name));
#else
        return dlsym(handle_, name);
#endif
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
                        return plugin.path == utf8_path;
                    })) {
        return GDML_ERROR_ALREADY_LOADED;
    }

    try {
        DynamicLibrary library(utf8_path);
        if (!library) {
            log_message(loader->host, GDML_LOG_ERROR, "Could not load the plugin library.");
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
        if (init(&loader->host) == 0) {
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
        if (plugin->shutdown != nullptr) {
            plugin->shutdown();
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
