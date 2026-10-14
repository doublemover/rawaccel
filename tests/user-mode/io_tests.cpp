#include <functional>
#include <iostream>
#include <limits>
#include <vector>
#include <cstring>
#include "../../common/rawaccel-io.hpp"
#include "../../common/utility-install.hpp"

namespace ra = rawaccel;
static unsigned checks = 0;
#define CHECK(expr) do { ++checks; if (!(expr)) { std::cerr << __LINE__ << ": " #expr "\n"; std::exit(1); } } while (false)
template <class F> void rejects(F f) { bool threw = false; try { f(); } catch (const std::exception&) { threw = true; } CHECK(threw); }
static DWORD last_error = 0;
static bool open_ok = true;
static unsigned calls = 0, closes = 0;
static std::function<BOOL(DWORD, void*, DWORD, void*, DWORD, DWORD*)> control;
DWORD GetLastError() { return last_error; }
HANDLE CreateFileW(const wchar_t*, DWORD, DWORD, void*, DWORD, DWORD, HANDLE) {
    return open_ok ? reinterpret_cast<HANDLE>(1) : INVALID_HANDLE_VALUE;
}
BOOL CloseHandle(HANDLE) { ++closes; last_error = 999; return TRUE; }
BOOL DeviceIoControl(HANDLE, DWORD code, void* in, DWORD in_size, void* out, DWORD size, DWORD* returned, void*) {
    ++calls; return control(code, in, in_size, out, size, returned);
}
static BOOL snapshot(void* out, DWORD capacity, DWORD* returned, unsigned profiles, unsigned devices = 0) {
    ra::io_base base{}; base.modifier_data_size = profiles; base.device_data_size = devices;
    const size_t required = ra::snapshot_size(base);
    *returned = static_cast<DWORD>(required <= capacity ? required : sizeof(base));
    std::memset(out, 0, *returned); std::memcpy(out, &base, sizeof(base)); return TRUE;
}
static void io_tests() {
    control = [](DWORD, void*, DWORD, void* out, DWORD size, DWORD* returned) { return snapshot(out, size, returned, 0); };
    auto empty = ra::read(); auto* base = reinterpret_cast<ra::io_base*>(empty.get());
    CHECK(base->modifier_data_size == 1 && base->device_data_size == 0);
    const auto* profile = reinterpret_cast<ra::modifier_settings*>(empty.get() + sizeof(*base));
    CHECK(profile->prof.output_dpi == ra::modifier_settings{}.prof.output_dpi);

    unsigned step = 0;
    control = [&](DWORD, void*, DWORD, void* out, DWORD size, DWORD* returned) { return snapshot(out, size, returned, ++step == 1 ? 1 : 3, 2); };
    auto grown = ra::read(); base = reinterpret_cast<ra::io_base*>(grown.get());
    CHECK(step == 3 && base->modifier_data_size == 3 && base->device_data_size == 2);
    step = 0;
    control = [&](DWORD, void*, DWORD, void* out, DWORD size, DWORD* returned) { return snapshot(out, size, returned, ++step == 1 ? 3 : 1); };
    auto shrunk = ra::read(); CHECK(reinterpret_cast<ra::io_base*>(shrunk.get())->modifier_data_size == 1);
    step = 0;
    control = [&](DWORD, void*, DWORD, void* out, DWORD size, DWORD* returned) { return snapshot(out, size, returned, ++step == 1 ? 3 : 0); };
    auto reset = ra::read(); CHECK(reinterpret_cast<ra::io_base*>(reset.get())->modifier_data_size == 1);

    control = [](DWORD, void*, DWORD, void*, DWORD, DWORD* returned) { *returned = sizeof(ra::io_base) - 1; return TRUE; };
    rejects([] { ra::read(); });
    control = [](DWORD, void*, DWORD, void* out, DWORD size, DWORD* returned) { snapshot(out, size, returned, 1); if (size > sizeof(ra::io_base)) --*returned; return TRUE; };
    rejects([] { ra::read(); });
    control = [](DWORD, void*, DWORD, void* out, DWORD size, DWORD* returned) { return snapshot(out, size, returned, 0, 1); };
    rejects([] { ra::read(); });
    step = 0;
    control = [&](DWORD, void*, DWORD, void* out, DWORD size, DWORD* returned) { return snapshot(out, size, returned, ++step); };
    rejects([] { ra::read(); }); CHECK(step == 8);
    control = [](DWORD, void*, DWORD, void*, DWORD size, DWORD* returned) { *returned = size + 1; return TRUE; };
    rejects([] { ra::read(); });
    ra::io_base huge{}; huge.modifier_data_size = std::numeric_limits<unsigned>::max();
    rejects([&] { ra::snapshot_size(huge); });
    huge = {}; huge.device_data_size = std::numeric_limits<unsigned>::max();
    rejects([&] { ra::snapshot_size(huge); });

    control = [](DWORD, void*, DWORD, void*, DWORD, DWORD*) { last_error = ERROR_ACCESS_DENIED; return FALSE; };
    try { ra::get_version(); CHECK(false); } catch (const ra::sys_error& e) { CHECK(e.code() == ERROR_ACCESS_DENIED); }
    control = [](DWORD, void*, DWORD, void*, DWORD, DWORD*) { last_error = ERROR_INVALID_FUNCTION; return FALSE; };
    CHECK(ra::get_version().major == 0);
    control = [](DWORD, void*, DWORD, void*, DWORD, DWORD* returned) { *returned = 1; return TRUE; };
    rejects([] { ra::get_version(); });
    control = [](DWORD, void*, DWORD, void* out, DWORD, DWORD* returned) { std::memcpy(out, &ra::version, sizeof(ra::version)); *returned = sizeof(ra::version); return TRUE; };
    CHECK(ra::get_version().minor == ra::version.minor);
    open_ok = false; last_error = ERROR_ACCESS_DENIED;
    try { ra::get_version(); CHECK(false); } catch (const ra::sys_error& e) { CHECK(e.code() == ERROR_ACCESS_DENIED); }
    last_error = ERROR_FILE_NOT_FOUND;
    try { ra::read(); CHECK(false); } catch (const ra::install_error&) { CHECK(true); }
    open_ok = true;

    ra::io_base blank{};
    control = [](DWORD code, void*, DWORD in_size, void*, DWORD, DWORD* returned) { CHECK(code == ra::WRITE && in_size == sizeof(ra::io_base)); *returned = 0; return TRUE; };
    ra::write(&blank, sizeof(blank));
    const unsigned before = calls;
    rejects([&] { ra::write(&blank, sizeof(blank) - 1); });
    blank.modifier_data_size = 1;
    rejects([&] { ra::write(&blank, sizeof(blank)); });
    rejects([&] { ra::write(nullptr, 0); });
    CHECK(calls == before);
    CHECK(closes == calls);
}

static std::vector<wchar_t> registry;
static DWORD registry_type = REG_MULTI_SZ;
static DWORD registry_error = ERROR_SUCCESS;
static LSTATUS query_status = ERROR_FILE_NOT_FOUND;
static unsigned writes = 0;
static bool deleted = false;
BOOL SetupDiGetClassRegistryPropertyW(const GUID*, DWORD, DWORD* type, BYTE* data, DWORD capacity, DWORD* needed, const wchar_t*, void*) {
    if (registry_error) { last_error = registry_error; return FALSE; }
    *type = registry_type; *needed = static_cast<DWORD>(registry.size() * sizeof(wchar_t));
    if (capacity < *needed) { last_error = ERROR_INSUFFICIENT_BUFFER; return FALSE; }
    if (*needed) std::memcpy(data, registry.data(), *needed);
    return TRUE;
}
BOOL SetupDiSetClassRegistryPropertyW(const GUID*, DWORD, const BYTE* data, DWORD size, const wchar_t*, void*) {
    ++writes; deleted = !data && size == 0;
    if (deleted) registry.clear();
    else { registry.resize(size / sizeof(wchar_t)); std::memcpy(registry.data(), data, size); }
    return TRUE;
}
HKEY SetupDiOpenClassRegKeyExW(const GUID*, DWORD, DWORD, const wchar_t*, void*) { return reinterpret_cast<HKEY>(1); }
LSTATUS RegQueryValueExW(HKEY, const wchar_t*, DWORD*, DWORD*, BYTE*, DWORD*) { return query_status; }
LSTATUS RegCloseKey(HKEY) { return ERROR_SUCCESS; }
static std::vector<wchar_t> multi(std::initializer_list<std::wstring> strings) {
    std::vector<wchar_t> result;
    for (const auto& s : strings) { result.insert(result.end(), s.begin(), s.end()); result.push_back(0); }
    result.push_back(0); return result;
}
static void filter_tests() {
    registry = multi({L"mouclass", L"RawAccel", L"other", L"RAWACCEL"});
    CHECK(set_registry_filter<true>());
    CHECK(parse_filter_multisz(registry) == std::vector<std::wstring>({L"rawaccel", L"mouclass", L"other"}));
    CHECK(set_registry_filter<false>());
    CHECK(parse_filter_multisz(registry) == std::vector<std::wstring>({L"mouclass", L"other"}));
    unsigned before = writes; CHECK(!set_registry_filter<false>()); CHECK(writes == before);
    registry = multi({L"rawaccel"}); CHECK(set_registry_filter<false>()); CHECK(deleted);
    registry_error = ERROR_FILE_NOT_FOUND; CHECK(read_registry_filters().empty());
    CHECK(!set_registry_filter<true>()); CHECK(parse_filter_multisz(registry) == std::vector<std::wstring>({L"rawaccel"}));
    registry_error = ERROR_INVALID_DATA; CHECK(read_registry_filters().empty());
    query_status = ERROR_SUCCESS; rejects([] { read_registry_filters(); });
    registry_error = ERROR_ACCESS_DENIED; rejects([] { read_registry_filters(); });
    registry_error = ERROR_SUCCESS; registry_type = 1; rejects([] { read_registry_filters(); }); registry_type = REG_MULTI_SZ;
    registry = {L'x'}; rejects([] { read_registry_filters(); });
    registry = {L'x', 0}; rejects([] { read_registry_filters(); });
    registry = {0, L'x', 0}; rejects([] { read_registry_filters(); });
    registry = {}; rejects([] { read_registry_filters(); });
    registry = {0}; CHECK(read_registry_filters().empty());
}
int main() { io_tests(); filter_tests(); std::cout << checks << " user-mode checks passed\n"; }
