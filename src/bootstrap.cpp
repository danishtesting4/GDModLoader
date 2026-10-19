#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "gdml/loader.h"
#include "game_ui.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace {

HMODULE g_module = nullptr;
FILE* g_log_file = nullptr;
SRWLOCK g_log_lock = SRWLOCK_INIT;

void write_log(GDML_LogLevel level, const char* message) {
    const char* label = level == GDML_LOG_ERROR ? "ERROR"
        : level == GDML_LOG_WARNING ? "WARN" : "INFO";
    char line[2048]{};
    sprintf_s(line, "[GDML] %s: %s\n", label, message);

    AcquireSRWLockExclusive(&g_log_lock);
    if (g_log_file != nullptr) {
        fputs(line, g_log_file);
        fflush(g_log_file);
    }
    ReleaseSRWLockExclusive(&g_log_lock);
    OutputDebugStringA(line);
}

void GDML_CALL host_log(void*, GDML_LogLevel level, const char* message) {
    write_log(level, message != nullptr ? message : "(null)");
}

std::wstring module_path() {
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD length = GetModuleFileNameW(g_module, buffer.data(),
                                                static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size() - 1) {
            return std::wstring(buffer.data(), length);
        }
        if (buffer.size() >= 32768) {
            return {};
        }
        buffer.resize(buffer.size() * 2);
    }
}

void signal_ready_event() {
    wchar_t name[64]{};
    swprintf_s(name, L"Local\\GDMLReady-%lu", GetCurrentProcessId());
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, name);
    if (event != nullptr) {
        SetEvent(event);
        CloseHandle(event);
    }
}

DWORD initialize_loader_impl() {
    const std::filesystem::path bootstrap_path(module_path());
    if (bootstrap_path.empty()) {
        write_log(GDML_LOG_ERROR, "Could not determine the bootstrap DLL path.");
        signal_ready_event();
        return 1;
    }

    const auto mods_directory = bootstrap_path.parent_path() / L"GDMods";
    std::error_code error;
    std::filesystem::create_directories(mods_directory, error);
    if (error) {
        write_log(GDML_LOG_ERROR, "Could not create the GDMods directory.");
        signal_ready_event();
        return 1;
    }

    const auto log_path = mods_directory / L"loader.log";
    if (_wfopen_s(&g_log_file, log_path.c_str(), L"w") != 0) {
        write_log(GDML_LOG_ERROR, "Could not open GDMods\\loader.log.");
    }
    write_log(GDML_LOG_INFO, "Starting GDModLoader for Geometry Dash 2.2091.");

    GDML_HostApi host{};
    host.struct_size = sizeof(host);
    host.api_version = GDML_HOST_API_VERSION;
    host.log = host_log;

    GDML_Loader* loader = nullptr;
    const auto create_result = gdml_loader_create(&host, &loader);
    if (create_result != GDML_OK) {
        write_log(GDML_LOG_ERROR, "Could not initialize the plugin loader API.");
        signal_ready_event();
        return 1;
    }

    std::vector<std::filesystem::path> plugins;
    for (std::filesystem::directory_iterator entry(mods_directory, error), end;
         !error && entry != end; entry.increment(error)) {
        if (entry->is_regular_file(error) &&
            _wcsicmp(entry->path().extension().c_str(), L".dll") == 0) {
            plugins.push_back(entry->path());
        }
    }

    if (error) {
        write_log(GDML_LOG_ERROR, "Could not enumerate the GDMods directory.");
    } else {
        std::sort(plugins.begin(), plugins.end());
        std::vector<GDML_ModInfo> mods;
        mods.reserve(plugins.size());
        for (const auto& plugin : plugins) {
            const auto path = plugin.u8string();
            const auto result = gdml_loader_load_plugin(loader, path.c_str());
            mods.push_back({plugin.stem().u8string(), result == GDML_OK});
            if (result != GDML_OK) {
                const auto message = std::string("Could not load plugin: ") + path;
                write_log(GDML_LOG_ERROR, message.c_str());
            }
        }
        const auto icon_path = (mods_directory / L"gdml-button.png").u8string();
        gdml_install_game_ui_hooks(mods, icon_path.c_str(), host_log, nullptr);
    }

    write_log(GDML_LOG_INFO, "Plugin scan complete.");
    signal_ready_event();
    return 0;
}

DWORD WINAPI initialize_loader(void*) {
    try {
        return initialize_loader_impl();
    } catch (const std::exception& error) {
        write_log(GDML_LOG_ERROR, error.what());
        signal_ready_event();
        return 1;
    }
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
        HANDLE thread = CreateThread(nullptr, 0, initialize_loader, nullptr, 0, nullptr);
        if (thread == nullptr) {
            OutputDebugStringA("[GDML] ERROR: Could not start the loader thread.\n");
            return FALSE;
        }
        CloseHandle(thread);
    }
    return TRUE;
}
