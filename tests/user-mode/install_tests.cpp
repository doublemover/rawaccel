#include <cstring>
#include <map>
#include <iostream>
#define main installer_cli_main
#include "../../installer/installer.cpp"
#undef main
#define main uninstaller_cli_main
#include "../../uninstaller/uninstaller.cpp"
#undef main

static unsigned checks = 0;
#define CHECK(expr) do { ++checks; if (!(expr)) { std::cerr << __LINE__ << ": " #expr "\n"; std::exit(1); } } while (false)
static fs::path root;
static DWORD error_code;
static bool service_exists = true;
static std::wstring service_path = L"old-driver.sys";
static std::vector<wchar_t> filters;
static std::map<std::string, bool> faults;
static unsigned temp_index = 0;
static unsigned scheduled = 0;
static HANDLE handle = reinterpret_cast<HANDLE>(1);
DWORD GetLastError() { return error_code; }
BOOL CloseHandle(HANDLE) { error_code = 999; return TRUE; }
BOOL CloseServiceHandle(SC_HANDLE) { error_code = 999; return TRUE; }
HANDLE CreateMutexW(void*, BOOL, const wchar_t*) { return handle; }
DWORD WaitForSingleObject(HANDLE, DWORD) { return WAIT_OBJECT_0; }
BOOL ReleaseMutex(HANDLE) { return TRUE; }
static DWORD copy_path(const fs::path& path, wchar_t* out, DWORD size) {
    const auto wide = path.wstring();
    if (wide.size() + 1 > size) return static_cast<DWORD>(wide.size() + 1);
    std::copy(wide.begin(), wide.end(), out); out[wide.size()] = 0; return static_cast<DWORD>(wide.size());
}
DWORD GetModuleFileNameW(HANDLE, wchar_t* out, DWORD size) { return copy_path(root / "package" / "installer.exe", out, size); }
UINT GetSystemDirectoryW(wchar_t* out, UINT size) { return copy_path(root / "system", out, size); }
template <class C> UINT GetTempFileNameW(const C* dir, const wchar_t*, UINT, wchar_t* out) {
    const auto path = fs::path(dir) / ("rac" + std::to_string(++temp_index) + ".tmp");
    std::ofstream(path) << ""; copy_path(path, out, MAX_PATH); return 1;
}
void GetNativeSystemInfo(SYSTEM_INFO* info) { info->wProcessorArchitecture = PROCESSOR_ARCHITECTURE_AMD64; }
static bool fail(const char* operation) {
    if (!faults[operation]) return false;
    error_code = ERROR_ACCESS_DENIED; return true;
}
SC_HANDLE OpenSCManagerW(const wchar_t*, const wchar_t*, DWORD) { return handle; }
SC_HANDLE OpenServiceW(SC_HANDLE, const wchar_t*, DWORD) {
    if (!service_exists) { error_code = ERROR_SERVICE_DOES_NOT_EXIST; return nullptr; }
    return handle;
}
BOOL QueryServiceConfigW(SC_HANDLE, QUERY_SERVICE_CONFIGW* config, DWORD size, DWORD* needed) {
    *needed = sizeof(*config);
    if (size < *needed) { error_code = ERROR_INSUFFICIENT_BUFFER; return FALSE; }
    static std::wstring old_path; old_path = service_path;
    *config = {SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, old_path.data(), const_cast<wchar_t*>(L"old name")}; return TRUE;
}
BOOL ChangeServiceConfigW(SC_HANDLE, DWORD, DWORD, DWORD, const wchar_t* path, const wchar_t*, DWORD*, const wchar_t*, const wchar_t*, const wchar_t*, const wchar_t*) {
    if (fail("update_service")) return FALSE;
    service_path = path; return TRUE;
}
SC_HANDLE CreateServiceW(SC_HANDLE, const wchar_t*, const wchar_t*, DWORD, DWORD, DWORD, DWORD, const wchar_t* path, const wchar_t*, DWORD*, const wchar_t*, const wchar_t*, const wchar_t*) {
    if (fail("create_service")) return nullptr;
    service_exists = true; service_path = path; return handle;
}
BOOL DeleteService(SC_HANDLE) { if (fail("delete_service")) return FALSE; service_exists = false; return TRUE; }
BOOL CopyFileW(const char* from, const char* to, BOOL) {
    if (fail("copy")) { std::ofstream(to) << "partial"; return FALSE; }
    return fs::copy_file(from, to, fs::copy_options::overwrite_existing);
}
BOOL MoveFileExW(const char* from, const char* to, DWORD flags) {
    if (flags & MOVEFILE_DELAY_UNTIL_REBOOT) { if (fail("schedule")) return FALSE; ++scheduled; return TRUE; }
    if (fs::path(from).extension() == ".tmp" && fs::path(to).extension() == ".sys" && faults["activate"]) {
        faults["activate"] = false; error_code = ERROR_ACCESS_DENIED; return FALSE;
    }
    if (!(flags & MOVEFILE_REPLACE_EXISTING) && fs::exists(to)) { error_code = ERROR_ACCESS_DENIED; return FALSE; }
    std::error_code ec; fs::rename(from, to, ec); if (ec) { error_code = ERROR_ACCESS_DENIED; return FALSE; } return TRUE;
}
BOOL DeleteFileW(const char* path) { std::error_code ec; const auto removed = fs::remove(path, ec); return removed && !ec; }
BOOL SetupDiGetClassRegistryPropertyW(const GUID*, DWORD, DWORD* type, BYTE* data, DWORD size, DWORD* needed, const wchar_t*, void*) {
    *type = REG_MULTI_SZ; *needed = static_cast<DWORD>(filters.size() * sizeof(wchar_t));
    if (size < *needed) { error_code = ERROR_INSUFFICIENT_BUFFER; return FALSE; }
    if (*needed) std::memcpy(data, filters.data(), *needed);
    return TRUE;
}
BOOL SetupDiSetClassRegistryPropertyW(const GUID*, DWORD, const BYTE* data, DWORD size, const wchar_t*, void*) {
    if (fail("registry_write")) return FALSE;
    filters.resize(size / sizeof(wchar_t)); if (size) std::memcpy(filters.data(), data, size); return TRUE;
}
HKEY SetupDiOpenClassRegKeyExW(const GUID*, DWORD, DWORD, const wchar_t*, void*) { return handle; }
LSTATUS RegQueryValueExW(HKEY, const wchar_t*, DWORD*, DWORD*, BYTE*, DWORD*) { return ERROR_FILE_NOT_FOUND; }
LSTATUS RegCloseKey(HKEY) { return ERROR_SUCCESS; }
int _getwch() { return 0; }

static fs::path target() { return root / "system" / "drivers" / "rawaccel.sys"; }
static fs::path source() { return root / "package" / DRV_SRC_PATH; }
static std::string contents(const fs::path& p) { std::ifstream in(p, std::ios::binary); return {std::istreambuf_iterator<char>(in), {}}; }
static void reset(bool existing = true) {
    fs::remove_all(root); fs::create_directories(target().parent_path()); fs::create_directories((root / "package"));
    faults.clear(); scheduled = 0; service_exists = existing; service_path = L"old-driver.sys";
    filters = {L'm', 0, 0};
    if (existing) std::ofstream(target()) << "original";
    std::ofstream file(source(), std::ios::binary);
    const IMAGE_DOS_HEADER dos{IMAGE_DOS_SIGNATURE, sizeof(IMAGE_DOS_HEADER)};
    const DWORD sig = IMAGE_NT_SIGNATURE;
    const IMAGE_FILE_HEADER header{IMAGE_FILE_MACHINE_AMD64, 1, sizeof(IMAGE_OPTIONAL_HEADER64), IMAGE_FILE_EXECUTABLE_IMAGE};
    const IMAGE_OPTIONAL_HEADER64 optional{IMAGE_NT_OPTIONAL_HDR64_MAGIC, IMAGE_SUBSYSTEM_NATIVE};
    file.write(reinterpret_cast<const char*>(&dos), sizeof(dos)); file.write(reinterpret_cast<const char*>(&sig), sizeof(sig));
    file.write(reinterpret_cast<const char*>(&header), sizeof(header)); file.write(reinterpret_cast<const char*>(&optional), sizeof(optional));
}
static void expect_failure() { bool failed = false; try { install(); } catch (const std::exception&) { failed = true; } CHECK(failed); }
static unsigned file_count() { return static_cast<unsigned>(std::distance(fs::directory_iterator(target().parent_path()), fs::directory_iterator{})); }
int main(int argc, char** argv) {
    CHECK(argc == 2); root = fs::absolute(argv[1]);
    for (const char* fault : {"copy", "activate", "update_service", "registry_write"}) {
        reset(); faults[fault] = true; expect_failure();
        CHECK(contents(target()) == "original"); CHECK(service_exists); CHECK(service_path == L"old-driver.sys"); CHECK(file_count() == 1);
    }
    for (const char* fault : {"copy", "activate", "create_service", "registry_write"}) {
        reset(false); faults[fault] = true; expect_failure(); CHECK(!fs::exists(target())); CHECK(!service_exists); CHECK(file_count() == 0);
    }
    reset(); std::ofstream(source(), std::ios::trunc) << "corrupt"; expect_failure(); CHECK(contents(target()) == "original"); CHECK(file_count() == 1);
    reset(); fs::remove(source()); expect_failure(); CHECK(contents(target()) == "original");
    reset(); install(); CHECK(contents(target()) == contents(source())); CHECK(scheduled == 1); CHECK(service_path == DRV_DST_PATH); CHECK(parse_filter_multisz(filters).front() == DRV_NAME);
    reset(false); install(); CHECK(service_exists); CHECK(scheduled == 0); CHECK(file_count() == 1);
    reset(); faults["schedule"] = true; expect_failure(); CHECK(contents(target()) == contents(source())); CHECK(file_count() == 2);
    reset(); CHECK(uninstaller_cli_main() == EXIT_SUCCESS); CHECK(!service_exists); CHECK(!fs::exists(target())); CHECK(scheduled == 1);
    reset(); faults["delete_service"] = true; CHECK(uninstaller_cli_main() == EXIT_FAILURE); CHECK(contents(target()) == "original");
    reset(); faults["schedule"] = true; CHECK(uninstaller_cli_main() == EXIT_FAILURE); CHECK(contents(target()) == "original"); CHECK(file_count() == 1);
    fs::remove_all(root); std::cout << checks << " installer fault-path checks passed\n";
}
