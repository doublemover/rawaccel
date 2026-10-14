#pragma once

#include "rawaccel.hpp"
#include "input-pipeline.hpp"
#include "rawaccel-io-def.h"

#include <kbdmou.h>
#include <wdf.h>

#if DBG
#define DebugPrint(_x_) DbgPrint _x_
#else
#define DebugPrint(_x_)
#endif

#define NTDEVICE_NAME         L"\\Device\\rawaccel"
#define SYMBOLIC_NAME_STRING  L"\\DosDevices\\rawaccel"

using counter_t = long long;
namespace ra = rawaccel;

typedef struct _DEVICE_EXTENSION {
    KSPIN_LOCK state_lock;
    ra::input_configuration config;
    ra::input_state state;
    unsigned long long configuration_generation;
    unsigned long long input_epoch;
    bool callback_busy;
    bool in_collection;
    CONNECT_DATA UpperConnectData;
    WCHAR instance_id[ra::MAX_DEV_ID_LEN];
    WCHAR dev_id[ra::MAX_DEV_ID_LEN];
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_EXTENSION, FilterGetData)

EXTERN_C_START

DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_DEVICE_ADD EvtDeviceAdd;
EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL EvtIoInternalDeviceControl;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL RawaccelControl;
EVT_WDF_OBJECT_CONTEXT_CLEANUP DeviceCleanup;
EVT_WDF_DRIVER_UNLOAD DriverUnload;
EVT_WDF_DEVICE_D0_ENTRY DeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT DeviceD0Exit;
EVT_WDF_REQUEST_COMPLETION_ROUTINE ConnectComplete;

VOID DeviceSetup(WDFOBJECT);
VOID WriteDelay(VOID);
NTSTATUS RawaccelInit(WDFDRIVER);
NTSTATUS CreateControlDevice(WDFDRIVER);

EXTERN_C_END

VOID RawaccelCallback(
    IN PDEVICE_OBJECT DeviceObject,
    IN PMOUSE_INPUT_DATA InputDataStart,
    IN PMOUSE_INPUT_DATA InputDataEnd,
    IN OUT PULONG InputDataConsumed
);

VOID DispatchPassThrough(
    _In_ WDFREQUEST Request,
    _In_ WDFIOTARGET Target
);
