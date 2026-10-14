#include <iostream>
#include <cstdlib>
#include <utility-install.hpp>

bool remove_service()
{
    service_handle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!manager) throw sys_error("OpenSCManager failed");
    service_handle service(OpenServiceW(manager, DRV_NAME, DELETE));
    if (!service) {
        const DWORD code = GetLastError();
        if (code == ERROR_SERVICE_DOES_NOT_EXIST) return false;
        if (code == ERROR_SERVICE_MARKED_FOR_DELETE) return true;
        throw sys_error("OpenService failed", code);
    }
    if (!DeleteService(service)) {
        const DWORD code = GetLastError();
        if (code != ERROR_SERVICE_MARKED_FOR_DELETE) throw sys_error("DeleteService failed", code);
    }
    return true;
}

int main()
{
    int result = EXIT_SUCCESS;
    try {
        installation_lock lock;
        bool reboot_required = set_registry_filter<false>();
        // Do not remove the binary until the filter reference and service have
        // been removed, so a failed earlier step cannot leave a dangling filter.
        reboot_required = remove_service() || reboot_required;
        const fs::path target = driver_destination();
        if (fs::exists(target)) {
            const fs::path retired = reserve_temp_path(target);
            if (!MoveFileExW(target.c_str(), retired.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                const DWORD code = GetLastError();
                DeleteFileW(retired.c_str());
                throw sys_error("Retire driver binary failed", code);
            }
            // Schedule only the unique retired file. Scheduling the live path
            // could delete a later reinstall at the next reboot.
            if (!MoveFileExW(retired.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT)) {
                const DWORD code = GetLastError();
                if (!MoveFileExW(retired.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH))
                    std::wcerr << L"Driver cleanup is incomplete; the retained file is at " << retired << L".\n";
                throw sys_error("Schedule driver deletion failed", code);
            }
            reboot_required = true;
        }
        std::cout << (reboot_required ?
            "Removal complete, change will take effect after restart.\n" : "No installed driver found.\n");
    }
    catch (const std::exception& e) {
        result = EXIT_FAILURE;
        std::cerr << "Error: " << e.what() << "\nRemoval may be incomplete; resolve the error and rerun the uninstaller.\n";
    }
    std::cout << "Press any key to close this window . . .\n";
    _getwch();
    return result;
}
