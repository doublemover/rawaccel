#pragma once

#pragma comment(lib, "setupapi")
#pragma comment(lib, "advapi32")

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <system_error>
#include <stdexcept>
#include <Windows.h>
#include <initguid.h>
#include <setupapi.h>
#include <Devguid.h>
#include <conio.h>

#define DRV_NAME L"rawaccel"
#define DRV_FILE_NAME DRV_NAME L".sys"
#define DRV_SRC_PATH L"driver\\" DRV_FILE_NAME
#define DRV_DST_PATH L"%systemroot%\\system32\\drivers\\" DRV_FILE_NAME

namespace fs = std::filesystem;

template <typename String>
std::system_error sys_error(String what, DWORD code = GetLastError())
{
    return std::system_error(code, std::system_category(), what);
}

class service_handle {
    SC_HANDLE value_;
public:
    explicit service_handle(SC_HANDLE value = nullptr) noexcept : value_(value) {}
    ~service_handle() { if (value_) CloseServiceHandle(value_); }
    service_handle(const service_handle&) = delete;
    service_handle& operator=(const service_handle&) = delete;
    operator SC_HANDLE() const noexcept { return value_; }
};

// Serialize this installer's file/service/filter transaction across processes.
class installation_lock {
    HANDLE mutex_;
public:
    installation_lock() : mutex_(CreateMutexW(nullptr, FALSE, L"Global\\RawAccelInstaller")) {
        if (!mutex_) throw sys_error("Create installation mutex failed");
        const DWORD result = WaitForSingleObject(mutex_, 0);
        if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) {
            const DWORD code = result == WAIT_FAILED ? GetLastError() : ERROR_BUSY;
            CloseHandle(mutex_);
            throw sys_error("Another installation may be in progress", code);
        }
    }
    ~installation_lock() { ReleaseMutex(mutex_); CloseHandle(mutex_); }
    installation_lock(const installation_lock&) = delete;
    installation_lock& operator=(const installation_lock&) = delete;
};

inline fs::path executable_directory()
{
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!length) throw sys_error("GetModuleFileName failed");
        if (length < path.size()) {
            path.resize(length);
            return fs::path(path).parent_path();
        }
        if (path.size() >= 32768) throw std::runtime_error("Executable path is too long");
        path.resize(path.size() * 2);
    }
}

inline fs::path driver_destination()
{
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const UINT length = GetSystemDirectoryW(path.data(), static_cast<UINT>(path.size()));
        if (!length) throw sys_error("GetSystemDirectory failed");
        if (length < path.size()) {
            path.resize(length);
            return fs::path(path) / L"drivers" / DRV_FILE_NAME;
        }
        path.resize(static_cast<size_t>(length) + 1);
    }
}

// Reserve a unique filename in the protected destination directory. Never
// reuse a fixed .tmp name: it may belong to a driver still loaded by Windows.
inline fs::path reserve_temp_path(const fs::path& target)
{
    wchar_t path[MAX_PATH];
    if (!GetTempFileNameW(target.parent_path().c_str(), L"rac", 0, path))
        throw sys_error("GetTempFileName failed");
    return path;
}

inline std::vector<std::wstring> parse_filter_multisz(const std::vector<wchar_t>& data)
{
    std::vector<std::wstring> filters;
    size_t start = 0;
    while (start < data.size() && data[start] != L'\0') {
        const auto end = std::find(data.begin() + start, data.end(), L'\0');
        if (end == data.end()) throw std::runtime_error("Mouse UpperFilters is not terminated");
        filters.emplace_back(data.begin() + start, end);
        start = static_cast<size_t>(end - data.begin()) + 1;
    }
    if (start == data.size() ||
        std::any_of(data.begin() + start, data.end(), [](wchar_t c) { return c != L'\0'; }))
        throw std::runtime_error("Mouse UpperFilters has an invalid MULTI_SZ value");
    return filters;
}

inline std::vector<std::wstring> read_registry_filters()
{
    DWORD needed = 0;
    DWORD type = 0;
    std::vector<wchar_t> buffer;
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        if (SetupDiGetClassRegistryPropertyW(&GUID_DEVCLASS_MOUSE, SPCRP_UPPERFILTERS,
            &type, reinterpret_cast<PBYTE>(buffer.data()),
            static_cast<DWORD>(buffer.size() * sizeof(wchar_t)), &needed, nullptr, nullptr)) {
            if (type != REG_MULTI_SZ || needed == 0 || needed % sizeof(wchar_t) != 0 ||
                needed > buffer.size() * sizeof(wchar_t))
                throw std::runtime_error("Mouse UpperFilters has an invalid registry type or size");
            buffer.resize(needed / sizeof(wchar_t));
            return parse_filter_multisz(buffer);
        }
        const DWORD code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND) return {};
        if (code == ERROR_INVALID_DATA) {
            // SetupAPI can map a missing property to ERROR_INVALID_DATA. Check
            // actual absence before treating it as empty; never overwrite an
            // existing malformed value as if it had not been present.
            const HKEY key = SetupDiOpenClassRegKeyExW(&GUID_DEVCLASS_MOUSE, KEY_QUERY_VALUE,
                DIOCR_INSTALLER, nullptr, nullptr);
            if (key != INVALID_HANDLE_VALUE) {
                const LSTATUS status = RegQueryValueExW(key, L"UpperFilters", nullptr, nullptr, nullptr, nullptr);
                RegCloseKey(key);
                if (status == ERROR_FILE_NOT_FOUND) return {};
            }
        }
        if (code != ERROR_INSUFFICIENT_BUFFER) throw sys_error("Read mouse UpperFilters failed", code);
        // A class filter list should be tiny; reject corrupt or hostile lengths.
        if (needed == 0 || needed > 1024 * 1024 || needed % sizeof(wchar_t) != 0)
            throw std::runtime_error("Mouse UpperFilters has an invalid registry size");
        buffer.resize(needed / sizeof(wchar_t));
    }
    throw std::runtime_error("Mouse UpperFilters kept changing; retry installation");
}

template <bool Install = true>
bool set_registry_filter()
{
    auto filters = read_registry_filters();
    const auto old_size = filters.size();
    filters.erase(std::remove_if(filters.begin(), filters.end(), [](const std::wstring& name) {
        return _wcsicmp(name.c_str(), DRV_NAME) == 0;
    }), filters.end());
    const bool found = filters.size() != old_size;
    if constexpr (Install) filters.insert(filters.begin(), DRV_NAME);
    else if (!found) return false;

    std::vector<wchar_t> buffer;
    for (const auto& filter : filters) {
        buffer.insert(buffer.end(), filter.begin(), filter.end());
        buffer.push_back(L'\0');
    }
    buffer.push_back(L'\0');
    // With no remaining filters, delete the property rather than install an
    // empty invalid list. All unrelated filters retain their original order.
    if (!SetupDiSetClassRegistryPropertyW(&GUID_DEVCLASS_MOUSE, SPCRP_UPPERFILTERS,
        filters.empty() ? nullptr : reinterpret_cast<const BYTE*>(buffer.data()),
        filters.empty() ? 0 : static_cast<DWORD>(buffer.size() * sizeof(wchar_t)), nullptr, nullptr))
        throw sys_error("Write mouse UpperFilters failed");
    return found;
}
