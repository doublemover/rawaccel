#pragma once
#include "Windows.h"
#define SPCRP_UPPERFILTERS 0x12
#define DIOCR_INSTALLER 1
BOOL SetupDiGetClassRegistryPropertyW(const GUID*, DWORD, DWORD*, BYTE*, DWORD, DWORD*, const wchar_t*, void*);
BOOL SetupDiSetClassRegistryPropertyW(const GUID*, DWORD, const BYTE*, DWORD, const wchar_t*, void*);
HKEY SetupDiOpenClassRegKeyExW(const GUID*, DWORD, DWORD, const wchar_t*, void*);
