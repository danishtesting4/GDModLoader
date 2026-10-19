#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

std::wstring executable_directory() {
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                                static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size() - 1) {
            return std::wstring(buffer.data(), length).substr(
                0, std::wstring(buffer.data(), length).find_last_of(L"\\/")
            );
        }
        if (buffer.size() >= 32768) {
            return {};
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring quote_argument(const std::wstring& argument) {
    std::wstring quoted = L"\"";
    size_t slashes = 0;
    for (wchar_t character : argument) {
        if (character == L'\\') {
            ++slashes;
        } else if (character == L'"') {
            quoted.append(slashes * 2 + 1, L'\\');
            quoted.push_back(L'"');
            slashes = 0;
        } else {
            quoted.append(slashes, L'\\');
            slashes = 0;
            quoted.push_back(character);
        }
    }
    quoted.append(slashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

bool is_x64_executable(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    IMAGE_DOS_HEADER dos{};
    DWORD bytes_read = 0;
    bool valid = ReadFile(file, &dos, sizeof(dos), &bytes_read, nullptr) &&
                 bytes_read == sizeof(dos) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
                 dos.e_lfanew > 0 && dos.e_lfanew < 16 * 1024 * 1024;
    if (valid) {
        LARGE_INTEGER offset{};
        offset.QuadPart = dos.e_lfanew;
        valid = SetFilePointerEx(file, offset, nullptr, FILE_BEGIN) != FALSE;
    }
    DWORD signature = 0;
    IMAGE_FILE_HEADER header{};
    if (valid) {
        valid = ReadFile(file, &signature, sizeof(signature), &bytes_read, nullptr) &&
                bytes_read == sizeof(signature) && signature == IMAGE_NT_SIGNATURE &&
                ReadFile(file, &header, sizeof(header), &bytes_read, nullptr) &&
                bytes_read == sizeof(header) && header.Machine == IMAGE_FILE_MACHINE_AMD64;
    }
    CloseHandle(file);
    return valid;
}

std::uintptr_t load_library_address() {
    const HMODULE local_kernel = GetModuleHandleW(L"kernel32.dll");
    if (local_kernel == nullptr) {
        return 0;
    }
    const auto local_function = reinterpret_cast<std::uintptr_t>(
        GetProcAddress(local_kernel, "LoadLibraryW")
    );
    return local_function;
}

bool inject_bootstrap(HANDLE process, const std::wstring& bootstrap_path) {
    const size_t byte_count = (bootstrap_path.size() + 1) * sizeof(wchar_t);
    void* remote_path = VirtualAllocEx(process, nullptr, byte_count,
                                        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remote_path == nullptr) {
        return false;
    }

    SIZE_T bytes_written = 0;
    const bool wrote_path = WriteProcessMemory(process, remote_path, bootstrap_path.c_str(),
                                               byte_count, &bytes_written) != FALSE &&
                            bytes_written == byte_count;
    const auto load_library = load_library_address();
    if (!wrote_path) {
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        SetLastError(ERROR_WRITE_FAULT);
        return false;
    }
    if (load_library == 0) {
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        SetLastError(ERROR_PROC_NOT_FOUND);
        return false;
    }

    HANDLE thread = CreateRemoteThread(
        process, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(load_library),
        remote_path, 0, nullptr
    );
    if (thread == nullptr) {
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        return false;
    }

    const DWORD wait_result = WaitForSingleObject(thread, INFINITE);
    DWORD module_handle = 0;
    const BOOL got_exit_code = wait_result == WAIT_OBJECT_0
        ? GetExitCodeThread(thread, &module_handle)
        : FALSE;
    const bool loaded = got_exit_code != FALSE && module_handle != 0;
    CloseHandle(thread);
    VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
    if (wait_result != WAIT_OBJECT_0) {
        SetLastError(ERROR_TIMEOUT);
    } else if (got_exit_code == FALSE) {
        SetLastError(ERROR_GEN_FAILURE);
    } else if (module_handle == 0) {
        SetLastError(ERROR_MOD_NOT_FOUND);
    }
    return loaded;
}

int fail_start(HANDLE process, HANDLE thread, HANDLE ready_event, const wchar_t* message) {
    const DWORD error = GetLastError();
    fwprintf(stderr, L"GDModLoader: %ls (error %lu)\n", message, error);
    if (process != nullptr) {
        TerminateProcess(process, 1);
        WaitForSingleObject(process, 5000);
    }
    if (ready_event != nullptr) CloseHandle(ready_event);
    if (thread != nullptr) CloseHandle(thread);
    if (process != nullptr) CloseHandle(process);
    return 1;
}

} // namespace

int wmain() {
    const auto directory = executable_directory();
    if (directory.empty()) {
        fwprintf(stderr, L"GDModLoader: Could not determine the launcher directory.\n");
        return 1;
    }

    int argument_count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (arguments == nullptr) {
        fwprintf(stderr, L"GDModLoader: Could not parse command-line arguments.\n");
        return 1;
    }

    const std::wstring game_path = argument_count > 1
        ? (std::wstring(arguments[1]).find(L":") != std::wstring::npos ||
           arguments[1][0] == L'\\' || arguments[1][0] == L'/'
              ? std::wstring(arguments[1])
              : directory + L"\\" + arguments[1])
        : directory + L"\\GeometryDash.exe";
    const std::wstring bootstrap_path = directory + L"\\gdml_bootstrap.dll";
    if (!is_x64_executable(game_path)) {
        LocalFree(arguments);
        fwprintf(stderr, L"GDModLoader: The target is missing or is not a valid x64 executable: %ls\n",
                 game_path.c_str());
        return 1;
    }
    if (GetFileAttributesW(bootstrap_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        LocalFree(arguments);
        fwprintf(stderr, L"GDModLoader: gdml_bootstrap.dll is missing beside the launcher.\n");
        return 1;
    }

    std::wstring command_line = quote_argument(game_path);
    for (int index = 2; index < argument_count; ++index) {
        command_line.push_back(L' ');
        command_line += quote_argument(arguments[index]);
    }
    LocalFree(arguments);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(game_path.c_str(), command_line.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED, nullptr, directory.c_str(), &startup, &child)) {
        fwprintf(stderr, L"GDModLoader: Could not start Geometry Dash (error %lu).\n",
                 GetLastError());
        return 1;
    }

    wchar_t event_name[64]{};
    swprintf_s(event_name, L"Local\\GDMLReady-%lu", child.dwProcessId);
    HANDLE ready_event = CreateEventW(nullptr, TRUE, FALSE, event_name);
    if (ready_event == nullptr) {
        return fail_start(child.hProcess, child.hThread, nullptr,
                          L"Could not create the loader-ready event");
    }
    if (!inject_bootstrap(child.hProcess, bootstrap_path)) {
        return fail_start(child.hProcess, child.hThread, ready_event,
                          L"Could not inject the bootstrap DLL");
    }
    if (WaitForSingleObject(ready_event, 60000) != WAIT_OBJECT_0) {
        SetLastError(ERROR_TIMEOUT);
        return fail_start(child.hProcess, child.hThread, ready_event,
                          L"Loader did not finish initialization");
    }

    CloseHandle(ready_event);
    if (ResumeThread(child.hThread) == static_cast<DWORD>(-1)) {
        return fail_start(child.hProcess, child.hThread, nullptr,
                          L"Could not resume Geometry Dash");
    }
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    return 0;
}
