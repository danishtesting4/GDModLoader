#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

int wmain() {
    std::vector<wchar_t> executable(512);
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(),
                                             static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size()) {
        return 1;
    }

    const std::wstring path(executable.data(), length);
    const auto separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos) {
        return 1;
    }
    const std::wstring log_path = path.substr(0, separator) +
                                  L"\\GDMods\\loader.log";
    std::ifstream log(log_path);
    if (!log) {
        return 1;
    }

    const std::string content((std::istreambuf_iterator<char>(log)),
                              std::istreambuf_iterator<char>());
    return content.find("Example mod initialized.") != std::string::npos &&
                   content.find("Plugin loaded.") != std::string::npos
        ? 0
        : 1;
}
