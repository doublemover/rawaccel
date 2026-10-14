#pragma once
// Deliberately minimal mock API, never used by production Windows projects.
#include <cstdint>
#include <cwchar>
#include <wchar.h>
using DWORD = uint32_t;
using ULONG = uint32_t;
using UINT = unsigned;
using BYTE = unsigned char;
using WORD = uint16_t;
using BOOL = int;
using HANDLE = void*;
using SC_HANDLE = void*;
using HKEY = void*;
using PBYTE = BYTE*;
using LSTATUS = long;
struct GUID { int unused; };
#define INVALID_HANDLE_VALUE reinterpret_cast<HANDLE>(intptr_t(-1))
#define MAX_PATH 260
#define TRUE 1
#define FALSE 0
#define ERROR_SUCCESS 0
#define ERROR_INVALID_FUNCTION 1
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_PATH_NOT_FOUND 3
#define ERROR_ACCESS_DENIED 5
#define ERROR_INVALID_DATA 13
#define ERROR_NOT_SUPPORTED 50
#define ERROR_INSUFFICIENT_BUFFER 122
#define ERROR_BUSY 170
#define OPEN_EXISTING 3
#define REG_MULTI_SZ 7
#define KEY_QUERY_VALUE 1
#define WAIT_OBJECT_0 0
#define WAIT_ABANDONED 0x80
#define WAIT_FAILED 0xffffffff
#define METHOD_BUFFERED 0
#define FILE_ANY_ACCESS 0
#define CTL_CODE(device, function, method, access) ((device << 16) | (access << 14) | (function << 2) | method)
#ifndef _MSC_VER
#define __forceinline inline
#define _copysign copysign
#define _wcsicmp wcscasecmp
#endif
DWORD GetLastError();
HANDLE CreateFileW(const wchar_t*, DWORD, DWORD, void*, DWORD, DWORD, HANDLE);
BOOL DeviceIoControl(HANDLE, DWORD, void*, DWORD, void*, DWORD, DWORD*, void*);
BOOL CloseHandle(HANDLE);
BOOL CloseServiceHandle(SC_HANDLE);
HANDLE CreateMutexW(void*, BOOL, const wchar_t*);
DWORD WaitForSingleObject(HANDLE, DWORD);
BOOL ReleaseMutex(HANDLE);
DWORD GetModuleFileNameW(HANDLE, wchar_t*, DWORD);
UINT GetSystemDirectoryW(wchar_t*, UINT);
template <class C> UINT GetTempFileNameW(const C*, const wchar_t*, UINT, wchar_t*);
LSTATUS RegQueryValueExW(HKEY, const wchar_t*, DWORD*, DWORD*, BYTE*, DWORD*);
LSTATUS RegCloseKey(HKEY);
using LONG = int32_t;
#define ERROR_SERVICE_DOES_NOT_EXIST 1060
#define ERROR_SERVICE_MARKED_FOR_DELETE 1072
#define SERVICE_QUERY_CONFIG 1
#define SERVICE_CHANGE_CONFIG 2
#define DELETE 0x10000
#define SC_MANAGER_CONNECT 1
#define SC_MANAGER_CREATE_SERVICE 2
#define SERVICE_KERNEL_DRIVER 1
#define SERVICE_DEMAND_START 3
#define SERVICE_ERROR_NORMAL 1
#define MOVEFILE_REPLACE_EXISTING 1
#define MOVEFILE_DELAY_UNTIL_REBOOT 4
#define MOVEFILE_WRITE_THROUGH 8
#define IMAGE_DOS_SIGNATURE 0x5a4d
#define IMAGE_NT_SIGNATURE 0x4550
#define IMAGE_NT_OPTIONAL_HDR64_MAGIC 0x20b
#define IMAGE_SUBSYSTEM_NATIVE 1
#define IMAGE_FILE_EXECUTABLE_IMAGE 2
#define PROCESSOR_ARCHITECTURE_AMD64 9
#define PROCESSOR_ARCHITECTURE_ARM64 12
#define IMAGE_FILE_MACHINE_AMD64 0x8664
#define IMAGE_FILE_MACHINE_ARM64 0xaa64
// Only fields read by the tests. These are NOT Windows ABI fixtures.
struct IMAGE_DOS_HEADER { WORD e_magic; LONG e_lfanew; };
struct IMAGE_FILE_HEADER { WORD Machine, NumberOfSections, SizeOfOptionalHeader, Characteristics; };
struct IMAGE_OPTIONAL_HEADER64 { WORD Magic, Subsystem; };
struct SYSTEM_INFO { WORD wProcessorArchitecture; };
struct QUERY_SERVICE_CONFIGW {
    DWORD dwServiceType, dwStartType, dwErrorControl;
    wchar_t* lpBinaryPathName;
    wchar_t* lpDisplayName;
};
void GetNativeSystemInfo(SYSTEM_INFO*);
SC_HANDLE OpenSCManagerW(const wchar_t*, const wchar_t*, DWORD);
SC_HANDLE OpenServiceW(SC_HANDLE, const wchar_t*, DWORD);
BOOL QueryServiceConfigW(SC_HANDLE, QUERY_SERVICE_CONFIGW*, DWORD, DWORD*);
BOOL ChangeServiceConfigW(SC_HANDLE, DWORD, DWORD, DWORD, const wchar_t*, const wchar_t*, DWORD*, const wchar_t*, const wchar_t*, const wchar_t*, const wchar_t*);
SC_HANDLE CreateServiceW(SC_HANDLE, const wchar_t*, const wchar_t*, DWORD, DWORD, DWORD, DWORD, const wchar_t*, const wchar_t*, DWORD*, const wchar_t*, const wchar_t*, const wchar_t*);
BOOL DeleteService(SC_HANDLE);
// std::filesystem::path::value_type is char on the Linux mock host.
BOOL CopyFileW(const char*, const char*, BOOL);
BOOL MoveFileExW(const char*, const char*, DWORD);
BOOL DeleteFileW(const char*);
