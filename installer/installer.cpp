#include <iostream>
#include <fstream>
#include <cstdint>
#include <cstdlib>
#include <utility-install.hpp>
#include <VersionHelpers.h>

// Check the staged file, not the source: no live installation is touched until
// the copy is complete and basic PE/architecture checks have passed. Windows
// still performs the authoritative driver-signature check when loading it.
void validate_driver(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    IMAGE_DOS_HEADER dos{};
    if (!file.read(reinterpret_cast<char*>(&dos), sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < static_cast<LONG>(sizeof(dos)) || static_cast<uintmax_t>(dos.e_lfanew) > fs::file_size(path))
        throw std::runtime_error("Driver binary has an invalid DOS header");
    file.seekg(dos.e_lfanew);
    DWORD signature = 0;
    IMAGE_FILE_HEADER header{};
    IMAGE_OPTIONAL_HEADER64 optional{};
    if (!file.read(reinterpret_cast<char*>(&signature), sizeof(signature)) || signature != IMAGE_NT_SIGNATURE ||
        !file.read(reinterpret_cast<char*>(&header), sizeof(header)) ||
        header.SizeOfOptionalHeader < sizeof(optional) ||
        !file.read(reinterpret_cast<char*>(&optional), sizeof(optional)) ||
        optional.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || optional.Subsystem != IMAGE_SUBSYSTEM_NATIVE ||
        !(header.Characteristics & IMAGE_FILE_EXECUTABLE_IMAGE) || header.NumberOfSections == 0)
        throw std::runtime_error("Driver binary is not a valid native 64-bit PE image");
    SYSTEM_INFO system{};
    GetNativeSystemInfo(&system);
    const WORD expected = system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? IMAGE_FILE_MACHINE_AMD64 :
        system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? IMAGE_FILE_MACHINE_ARM64 : 0;
    if (expected == 0 || header.Machine != expected)
        throw std::runtime_error("Driver binary does not match this computer's architecture");
}

std::vector<BYTE> service_configuration(SC_HANDLE service)
{
    DWORD needed = 0;
    if (!QueryServiceConfigW(service, nullptr, 0, &needed)) {
        const DWORD code = GetLastError();
        if (code != ERROR_INSUFFICIENT_BUFFER) throw sys_error("QueryServiceConfig failed", code);
    }
    std::vector<BYTE> config(needed);
    if (!QueryServiceConfigW(service, reinterpret_cast<QUERY_SERVICE_CONFIGW*>(config.data()), needed, &needed))
        throw sys_error("QueryServiceConfig failed");
    return config;
}

void install()
{
    installation_lock lock;
    if (!IsWindows10OrGreater()) throw std::runtime_error("OS not supported, you need at least Windows 10");
    const fs::path source = executable_directory() / DRV_SRC_PATH;
    if (!fs::is_regular_file(source)) throw std::runtime_error("Can't find driver binary beside installer.exe");
    const fs::path target = driver_destination();
    service_handle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE));
    if (!manager) throw sys_error("OpenSCManager failed");
    const DWORD access = SERVICE_QUERY_CONFIG | SERVICE_CHANGE_CONFIG | DELETE;
    SC_HANDLE opened = OpenServiceW(manager, DRV_NAME, access);
    const DWORD open_error = opened ? ERROR_SUCCESS : GetLastError();
    if (!opened && open_error != ERROR_SERVICE_DOES_NOT_EXIST) throw sys_error("OpenService failed", open_error);
    service_handle existing(opened);
    auto old_config = opened ? service_configuration(existing) : std::vector<BYTE>{};
    // Fail on a malformed filter value before replacing the binary.
    read_registry_filters();

    fs::path staged;
    fs::path backup;
    bool old_moved = false;
    bool new_moved = false;
    bool service_changed = false;
    SC_HANDLE created = nullptr;
    try {
        staged = reserve_temp_path(target);
        if (!CopyFileW(source.c_str(), staged.c_str(), FALSE)) throw sys_error("Stage driver copy failed");
        validate_driver(staged);
        if (fs::exists(target)) {
            backup = reserve_temp_path(target);
            if (!MoveFileExW(target.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw sys_error("Back up installed driver failed");
            old_moved = true;
        }
        if (!MoveFileExW(staged.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH))
            throw sys_error("Activate staged driver failed");
        new_moved = true;
        if (existing) {
            if (!ChangeServiceConfigW(existing, SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
                DRV_DST_PATH, nullptr, nullptr, nullptr, nullptr, nullptr, DRV_NAME))
                throw sys_error("ChangeServiceConfig failed");
            service_changed = true;
        }
        else {
            created = CreateServiceW(manager, DRV_NAME, DRV_NAME, DELETE, SERVICE_KERNEL_DRIVER,
                SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, DRV_DST_PATH, nullptr, nullptr, nullptr, nullptr, nullptr);
            if (!created) throw sys_error("CreateService failed");
        }
        // Commit last: a failed registry update rolls back the binary/service.
        set_registry_filter();
    }
    catch (...) {
        if (created && !DeleteService(created))
            std::cerr << "Rollback could not remove the newly created service (" << GetLastError() << ").\n";
        if (service_changed) {
            auto* config = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(old_config.data());
            if (!ChangeServiceConfigW(existing, config->dwServiceType, config->dwStartType, config->dwErrorControl,
                config->lpBinaryPathName, nullptr, nullptr, nullptr, nullptr, nullptr, config->lpDisplayName))
                std::cerr << "Rollback could not restore service configuration (" << GetLastError() << ").\n";
        }
        if (created) CloseServiceHandle(created);
        if (new_moved && !DeleteFileW(target.c_str()))
            std::cerr << "Rollback could not remove the replacement driver (" << GetLastError() << ").\n";
        if (old_moved && !MoveFileExW(backup.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH))
            std::wcerr << L"Rollback could not restore the driver; the previous file is at " << backup << L".\n";
        if (!staged.empty()) DeleteFileW(staged.c_str());
        if (!old_moved && !backup.empty()) DeleteFileW(backup.c_str());
        throw;
    }
    if (created) CloseServiceHandle(created);
    if (old_moved && !MoveFileExW(backup.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT)) {
        const DWORD code = GetLastError();
        std::wcerr << L"The new driver is installed, but the old backup could not be scheduled for deletion: "
            << backup << L" (" << code << L"). Remove it after restarting.\n";
        throw std::runtime_error("Installation cleanup is incomplete");
    }
}

int main()
{
    int result = EXIT_SUCCESS;
    try {
        install();
        std::cout << "Install complete, change will take effect after restart.\n";
    }
    catch (const std::exception& e) {
        result = EXIT_FAILURE;
        std::cerr << "Error: " << e.what() << '\n';
    }
    std::cout << "Press any key to close this window . . .\n";
    _getwch();
    return result;
}
