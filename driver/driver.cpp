#include <math.h>
// Keep rotation setup on the WDK kernel math implementation. MSVC can fuse
// sin/cos into a user-CRT-only __libm_sse2_sincos_ helper under /fp:fast.
#pragma function(sin, cos)

#include "driver.h"

#include <rawaccel.hpp>
#include <rawaccel-io-def.h>
#include <rawaccel-version.h>
#include <rawaccel-validate.hpp>
#include <initguid.h>
#include <devpkey.h>


#ifdef ALLOC_PRAGMA
#pragma alloc_text (INIT, DriverEntry)
#pragma alloc_text (INIT, RawaccelInit)
#pragma alloc_text (PAGE, CreateControlDevice)
#pragma alloc_text (PAGE, EvtDeviceAdd)
#pragma alloc_text (PAGE, RawaccelControl)
#pragma alloc_text (PAGE, DeviceCleanup)
#pragma alloc_text (PAGE, DeviceSetup)
#pragma alloc_text (PAGE, WriteDelay)
#endif

using milliseconds = double;

struct {
    bool initialized;
    WDFCOLLECTION device_collection;
    WDFDEVICE control_device;
    WDFWAITLOCK collection_lock;
    ra::io_base base_data;
    ra::modifier_settings* modifier_data;
    ra::device_settings* device_data;
    ra::prepared_profile* prepared_profiles;
    milliseconds tick_interval;
} global = {};

extern "C" PULONG InitSafeBootMode;

// A callback owns the per-device transaction until delivery finishes. The
// spin lock protects only driver-owned memory; it is never held across Mouclass.
// Fixed scratch space preserves original port packets when Mouclass is full.
VOID
RawaccelCallback(
    IN PDEVICE_OBJECT DeviceObject,
    IN PMOUSE_INPUT_DATA InputDataStart,
    IN PMOUSE_INPUT_DATA InputDataEnd,
    IN OUT PULONG InputDataConsumed
)
{
    auto* devExt = FilterGetData(WdfWdmDeviceGetWdfDeviceHandle(DeviceObject));
    KIRQL irql;
    auto lock = [&]() { KeAcquireSpinLock(&devExt->state_lock, &irql); };
    auto unlock = [&]() { KeReleaseSpinLock(&devExt->state_lock, irql); };
    auto clock = []() { return KeQueryPerformanceCounter(NULL).QuadPart; };
    auto send = [&](PMOUSE_INPUT_DATA start, PMOUSE_INPUT_DATA end, PULONG consumed) {
        // Connection lifetime is owned by the device stack, independently of
        // configuration replacement. No driver spin lock is held here.
        const auto& connect = devExt->UpperConnectData;
        if (connect.ClassService) {
            (*(PSERVICE_CALLBACK_ROUTINE)connect.ClassService)(
                connect.ClassDeviceObject, start, end, consumed);
        }
    };
    ra::deliver_packets(*devExt, InputDataStart, InputDataEnd, InputDataConsumed,
                        global.tick_interval, lock, unlock, clock, send);
}


// Reject invalid bool representations before evaluating typed wire fields.
static bool WireBool(const void* object, size_t offset)
{
    return static_cast<const unsigned char*>(object)[offset] <= 1;
}

static bool WireDeviceConfig(const ra::device_config& cfg)
{
    return WireBool(&cfg, FIELD_OFFSET(ra::device_config, disable)) &&
        WireBool(&cfg, FIELD_OFFSET(ra::device_config, set_extra_info)) &&
        WireBool(&cfg, FIELD_OFFSET(ra::device_config, poll_time_lock)) &&
        WireBool(&cfg, FIELD_OFFSET(ra::device_config, accel_only_while_mouse1));
}

static bool WireProfile(const ra::profile& profile)
{
    return WireBool(&profile.accel_x, FIELD_OFFSET(ra::accel_args, gain)) &&
        WireBool(&profile.accel_y, FIELD_OFFSET(ra::accel_args, gain)) &&
        WireBool(&profile.speed_processor_args, FIELD_OFFSET(ra::speed_args, whole));
}

static bool ConfigReferencesExist(const ra::device_config& cfg,
                                 const ra::modifier_settings* profiles, unsigned count)
{
    for (size_t button = 0; button < ra::MOUSE_BUTTON_COUNT; ++button) {
        if (!cfg.button_profiles[button][0]) continue;
        bool found = false;
        for (unsigned i = 0; i < count; ++i) {
            if (wcsncmp(cfg.button_profiles[button], profiles[i].prof.name, ra::MAX_NAME_LEN) == 0) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

_Use_decl_annotations_
VOID
RawaccelControl(
    WDFQUEUE         Queue,
    WDFREQUEST       Request,
    size_t           OutputBufferLength,
    size_t           InputBufferLength,
    ULONG            IoControlCode
)
/*++
Routine Description:
    This event is called when the framework receives IRP_MJ_DEVICE_CONTROL
    requests from the system.
Arguments:
    Queue - Handle to the framework queue object that is associated
            with the I/O request.
    Request - Handle to a framework request object.
    OutputBufferLength - length of the request's output buffer,
                        if an output buffer is available.
    InputBufferLength - length of the request's input buffer,
                        if an input buffer is available.
    IoControlCode - the driver-defined or system-defined I/O control code
                    (IOCTL) that is associated with the request.
Return Value:
   VOID
--*/
{
    NTSTATUS status;
    void* buffer;
    size_t buffer_length;
    size_t bytes_out = 0;

    UNREFERENCED_PARAMETER(Queue);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);
    PAGED_CODE();

    DebugPrint(("Ioctl received into filter control object.\n"));

    if (!global.initialized) {
        WdfRequestCompleteWithInformation(Request, STATUS_CANCELLED, 0);
        return;
    }

    const auto SIZEOF_BASE = sizeof(ra::io_base);

    switch (IoControlCode) {
    case ra::READ:
        status = WdfRequestRetrieveOutputBuffer(
            Request,
            SIZEOF_BASE,
            &buffer,
            &buffer_length
        );
        if (!NT_SUCCESS(status)) {
            DebugPrint(("RetrieveOutputBuffer failed: 0x%x\n", status));
        }
        else {
            *static_cast<ra::io_base*>(buffer) = global.base_data;

            size_t modifier_bytes = global.base_data.modifier_data_size * sizeof(ra::modifier_settings);
            size_t device_bytes = global.base_data.device_data_size * sizeof(ra::device_settings);
            size_t total_bytes = SIZEOF_BASE + modifier_bytes + device_bytes;

            if (buffer_length < total_bytes) {
                bytes_out = SIZEOF_BASE;
            }
            else {
                BYTE* output_ptr = static_cast<BYTE*>(buffer) + SIZEOF_BASE;

                if (global.modifier_data) RtlCopyMemory(output_ptr, global.modifier_data, modifier_bytes);
                output_ptr += modifier_bytes;
                if (global.device_data) RtlCopyMemory(output_ptr, global.device_data, device_bytes);
                bytes_out = total_bytes;
            }
        }
        break;
    case ra::WRITE: {
        status = WdfRequestRetrieveInputBuffer(Request, SIZEOF_BASE, &buffer, &buffer_length);
        if (!NT_SUCCESS(status)) break;
        const auto& input = *static_cast<const ra::io_base*>(buffer);
        const size_t modifier_bytes = size_t(input.modifier_data_size) * sizeof(ra::modifier_settings);
        const size_t device_bytes = size_t(input.device_data_size) * sizeof(ra::device_settings);
        const size_t allocation_bytes = modifier_bytes + device_bytes;
        const size_t total_bytes = SIZEOF_BASE + allocation_bytes;
        constexpr size_t max_config_bytes = 16 * 1024 * 1024;
        if (input.modifier_data_size > 1024 || input.device_data_size > 4096 ||
            total_bytes > max_config_bytes || total_bytes != buffer_length ||
            (input.modifier_data_size == 0 && input.device_data_size != 0) ||
            !WireDeviceConfig(input.default_dev_cfg) || !ra::valid(input.default_dev_cfg)) {
            status = STATUS_INVALID_PARAMETER;
            break;
        }
        ra::modifier_settings* profiles = nullptr;
        ra::device_settings* devices = nullptr;
        ra::prepared_profile* prepared = nullptr;
        if (input.modifier_data_size != 0) {
            profiles = static_cast<ra::modifier_settings*>(
                ExAllocatePoolWithTag(PagedPool, allocation_bytes, 'g'));
            if (!profiles) { status = STATUS_INSUFFICIENT_RESOURCES; break; }
            RtlCopyMemory(profiles, static_cast<BYTE*>(buffer) + SIZEOF_BASE, allocation_bytes);
            devices = reinterpret_cast<ra::device_settings*>(reinterpret_cast<BYTE*>(profiles) + modifier_bytes);
            bool valid = ConfigReferencesExist(input.default_dev_cfg, profiles, input.modifier_data_size);
            for (unsigned i = 0; valid && i < input.modifier_data_size; ++i) {
                valid = WireProfile(profiles[i].prof) && bool(ra::valid(profiles[i].prof));
                for (unsigned j = 0; valid && j < i; ++j) {
                    valid = wcsncmp(profiles[i].prof.name, profiles[j].prof.name, ra::MAX_NAME_LEN) != 0;
                }
            }
            for (unsigned i = 0; valid && i < input.device_data_size; ++i) {
                valid = WireDeviceConfig(devices[i].config) && bool(ra::valid(devices[i])) &&
                    ConfigReferencesExist(devices[i].config, profiles, input.modifier_data_size);
                if (valid && devices[i].profile[0]) {
                    bool found = false;
                    for (unsigned j = 0; j < input.modifier_data_size; ++j) {
                        if (wcsncmp(devices[i].profile, profiles[j].prof.name, ra::MAX_NAME_LEN) == 0) found = true;
                    }
                    valid = found;
                }
                for (unsigned j = 0; valid && j < i; ++j) {
                    valid = _wcsnicmp(devices[i].id, devices[j].id, ra::MAX_DEV_ID_LEN) != 0;
                }
            }
            if (!valid) {
                ExFreePoolWithTag(profiles, 'g');
                status = STATUS_INVALID_PARAMETER;
                break;
            }
            prepared = static_cast<ra::prepared_profile*>(ExAllocatePoolWithTag(
                NonPagedPoolNx, size_t(input.modifier_data_size) * sizeof(ra::prepared_profile), 'p'));
            if (!prepared) {
                ExFreePoolWithTag(profiles, 'g');
                status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }
            RtlZeroMemory(prepared, size_t(input.modifier_data_size) * sizeof(ra::prepared_profile));
            for (unsigned i = 0; i < input.modifier_data_size; ++i) {
                // Never trust derived union fields or lookup ranges sent by user
                // mode. Compute them once at PASSIVE_LEVEL from validated args.
                RtlZeroMemory(&profiles[i].data, sizeof(profiles[i].data));
                prepared[i].init(profiles[i]);
            }
        }
        WriteDelay(); // Preserve the existing one-second settings activation delay.
        WdfWaitLockAcquire(global.collection_lock, NULL);
        auto* old_profiles = global.modifier_data;
        auto* old_prepared = global.prepared_profiles;
        global.modifier_data = profiles;
        global.device_data = input.device_data_size ? devices : nullptr;
        global.prepared_profiles = prepared;
        global.base_data = input;
        const auto count = WdfCollectionGetCount(global.device_collection);
        for (unsigned i = 0; i < count; ++i) DeviceSetup(WdfCollectionGetItem(global.device_collection, i));
        // Every callback that could reference the old nonpaged bank has now
        // passed its device spin lock. No old-bank pointer is used after upcall.
        if (old_profiles) ExFreePoolWithTag(old_profiles, 'g');
        if (old_prepared) ExFreePoolWithTag(old_prepared, 'p');
        WdfWaitLockRelease(global.collection_lock);
        status = STATUS_SUCCESS;
        break;
    }
    case ra::GET_VERSION:
        status = WdfRequestRetrieveOutputBuffer(
            Request,
            sizeof(ra::version_t),
            &buffer,
            NULL
        );
        if (!NT_SUCCESS(status)) {
            DebugPrint(("RetrieveOutputBuffer failed: 0x%x\n", status));
        }
        else {
            *static_cast<ra::version_t*>(buffer) = ra::version;
            bytes_out = sizeof(ra::version_t);
        }
        break;
    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    WdfRequestCompleteWithInformation(Request, status, bytes_out);

}

NTSTATUS
RawaccelInit(WDFDRIVER driver)
{
    NTSTATUS status = WdfCollectionCreate(WDF_NO_OBJECT_ATTRIBUTES, &global.device_collection);
    if (!NT_SUCCESS(status)) return status;
    status = WdfWaitLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &global.collection_lock);
    if (!NT_SUCCESS(status)) return status;
    LARGE_INTEGER frequency;
    KeQueryPerformanceCounter(&frequency);
    global.tick_interval = 1e3 / frequency.QuadPart;
    UNREFERENCED_PARAMETER(driver);
    global.initialized = true;
    return STATUS_SUCCESS;
}

// Called at PASSIVE_LEVEL with collection_lock held. Profile preparation and
// matching happen outside the device spin lock; publication is a small copy.
VOID
DeviceSetup(WDFOBJECT hDevice)
{
    PAGED_CODE();
    auto* devExt = FilterGetData(hDevice);
    ra::input_configuration next = {};
    if (global.prepared_profiles) {
        const auto* cfg = &global.base_data.default_dev_cfg;
        const ra::device_settings* match = nullptr;
        // New configurations identify an individual device instance. Legacy
        // hardware-ID configurations still apply to all matching instances.
        for (auto i = 0u; i < global.base_data.device_data_size; ++i) {
            auto* candidate = &global.device_data[i];
            if (devExt->instance_id[0] && _wcsnicmp(devExt->instance_id, candidate->id, ra::MAX_DEV_ID_LEN) == 0) {
                match = candidate;
                break;
            }
            if (!match && _wcsnicmp(devExt->dev_id, candidate->id, ra::MAX_DEV_ID_LEN) == 0) match = candidate;
        }
        auto find_profile = [](const WCHAR* name) -> const ra::prepared_profile* {
            for (auto i = 0u; i < global.base_data.modifier_data_size; ++i) {
                if (wcsncmp(name, global.modifier_data[i].prof.name, ra::MAX_NAME_LEN) == 0) {
                    return &global.prepared_profiles[i];
                }
            }
            return nullptr;
        };
        next.base_profile = &global.prepared_profiles[0];
        if (match) {
            cfg = &match->config;
            if (match->profile[0]) next.base_profile = find_profile(match->profile);
        }
        next.enabled = !cfg->disable;
        next.set_extra_info = cfg->set_extra_info;
        next.mouse1_only = cfg->accel_only_while_mouse1;
        next.dpi_factor = cfg->dpi > 0 ? ra::NORMALIZED_DPI / cfg->dpi : 1;
        next.keep_time = !(cfg->poll_time_lock && cfg->polling_rate > 0);
        next.clamp = cfg->clamp;
        if (cfg->polling_rate > 0) {
            next.clamp.min = 1000.0 / cfg->polling_rate;
            if (!next.keep_time) next.clamp.max = next.clamp.min;
            else next.clamp.max = ra::maxsd(next.clamp.min, next.clamp.max);
        }
        for (size_t i = 0; i < ra::MOUSE_BUTTON_COUNT; ++i) {
            if (cfg->button_profiles[i][0]) next.button_profiles[i] = find_profile(cfg->button_profiles[i]);
        }
    }
    KIRQL irql;
    KeAcquireSpinLock(&devExt->state_lock, &irql);
    devExt->config = next;
    ra::reset_motion(devExt->state); // Preserve known held buttons across Apply.
    ++devExt->configuration_generation;
    KeReleaseSpinLock(&devExt->state_lock, irql);
}

VOID
DriverUnload(WDFDRIVER Driver)
{
    UNREFERENCED_PARAMETER(Driver);
    if (global.modifier_data) ExFreePoolWithTag(global.modifier_data, 'g');
    if (global.prepared_profiles) ExFreePoolWithTag(global.prepared_profiles, 'p');
    global.modifier_data = nullptr;
    global.prepared_profiles = nullptr;
    global.initialized = false;
}

static VOID ResetDeviceInput(WDFDEVICE Device)
{
    auto* devExt = FilterGetData(Device);
    KIRQL irql;
    KeAcquireSpinLock(&devExt->state_lock, &irql);
    devExt->state = {};
    ++devExt->input_epoch;
    ++devExt->configuration_generation;
    KeReleaseSpinLock(&devExt->state_lock, irql);
}

NTSTATUS DeviceD0Entry(WDFDEVICE Device, WDF_POWER_DEVICE_STATE PreviousState)
{
    UNREFERENCED_PARAMETER(PreviousState);
    ResetDeviceInput(Device);
    return STATUS_SUCCESS;
}

NTSTATUS DeviceD0Exit(WDFDEVICE Device, WDF_POWER_DEVICE_STATE TargetState)
{
    UNREFERENCED_PARAMETER(TargetState);
    ResetDeviceInput(Device);
    return STATUS_SUCCESS;
}

VOID
DeviceCleanup(WDFOBJECT hDevice)
{
    PAGED_CODE();
    DebugPrint(("Removing device from collection\n"));

    WdfWaitLockAcquire(global.collection_lock, NULL);
    if (FilterGetData(hDevice)->in_collection) {
        if (WdfCollectionGetCount(global.device_collection) == 1 && global.control_device) {
            // KMDF cannot unload a PnP filter while its control device exists.
            WdfObjectDelete(global.control_device);
            global.control_device = nullptr;
        }
        WdfCollectionRemove(global.device_collection, hDevice);
        FilterGetData(hDevice)->in_collection = false;
    }
    WdfWaitLockRelease(global.collection_lock);
}

VOID 
WriteDelay()
{
    LARGE_INTEGER interval;
    interval.QuadPart = static_cast<LONGLONG>(ra::WRITE_DELAY) * -10000;
    KeDelayExecutionThread(KernelMode, FALSE, &interval);
}

NTSTATUS
DriverEntry(
    IN  PDRIVER_OBJECT  DriverObject,
    IN  PUNICODE_STRING RegistryPath
)
/*++
Routine Description:

     Installable driver initialization entry point.
    This entry point is called directly by the I/O system.

--*/
{

    WDF_DRIVER_CONFIG config;
    NTSTATUS status;
    WDFDRIVER driver;

    DebugPrint(("km accel filter.\n"));
    DebugPrint(("Built %s %s\n", __DATE__, __TIME__));

    // Initialize driver config to control the attributes that
    // are global to the driver. Note that framework by default
    // provides a driver unload routine. If you create any resources
    // in the DriverEntry and want to be cleaned in driver unload,
    // you can override that by manually setting the EvtDriverUnload in the
    // config structure. In general xxx_CONFIG_INIT macros are provided to
    // initialize most commonly used members.

    WDF_DRIVER_CONFIG_INIT(
        &config,
        EvtDeviceAdd
    );

    config.EvtDriverUnload = DriverUnload;

    //
    // Create a framework driver object to represent our driver.
    //
    status = WdfDriverCreate(DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        &driver);


    if (NT_SUCCESS(status)) {
        if (*InitSafeBootMode == 0) status = RawaccelInit(driver);
    }
    else {
        DebugPrint(("WdfDriverCreate failed with status 0x%x\n", status));
    }

    return status;
}


NTSTATUS
CreateControlDevice(WDFDRIVER Driver)
/*++
Routine Description:
    This routine is called to create a control device object so that application
    can talk to the filter driver directly instead of going through the entire
    device stack. This kind of control device object is useful if the filter
    driver is underneath another driver which prevents ioctls not known to it
    or if the driver's dispatch routine is owned by some other (port/class)
    driver and it doesn't allow any custom ioctls.
Arguments:
    Driver - Handle to wdf driver object.
Return Value:
    WDF status code
--*/
{
    PWDFDEVICE_INIT             pInit = NULL;
    WDFDEVICE                   controlDevice = NULL;
    WDF_IO_QUEUE_CONFIG         ioQueueConfig;
    NTSTATUS                    status;
    WDFQUEUE                    queue;
    WDF_OBJECT_ATTRIBUTES controlAttributes;
    DECLARE_CONST_UNICODE_STRING(ntDeviceName, NTDEVICE_NAME);
    DECLARE_CONST_UNICODE_STRING(symbolicLinkName, SYMBOLIC_NAME_STRING);

    DebugPrint(("Creating Control Device\n"));

    //
    //
    // In order to create a control device, we first need to allocate a
    // WDFDEVICE_INIT structure and set all properties.
    //
    pInit = WdfControlDeviceInitAllocate(
        Driver,
        &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R
    );

    if (pInit == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Error;
    }

    //
    // Set exclusive to false so that more than one app can talk to the
    // control device simultaneously.
    //
    WdfDeviceInitSetExclusive(pInit, FALSE);

    status = WdfDeviceInitAssignName(pInit, &ntDeviceName);

    if (!NT_SUCCESS(status)) {
        goto Error;
    }

    WDF_OBJECT_ATTRIBUTES_INIT(&controlAttributes);
    controlAttributes.ExecutionLevel = WdfExecutionLevelPassive;
    status = WdfDeviceCreate(&pInit,
        &controlAttributes,
        &controlDevice);

    if (!NT_SUCCESS(status)) {
        goto Error;
    }

    //
    // Create a symbolic link for the control object so that usermode can open
    // the device.
    //

    status = WdfDeviceCreateSymbolicLink(controlDevice, &symbolicLinkName);

    if (!NT_SUCCESS(status)) {
        goto Error;
    }

    //
    // Configure the default queue associated with the control device object
    // to be Serial so that request passed to RawaccelControl are serialized.
    //

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioQueueConfig,
        WdfIoQueueDispatchSequential);

    ioQueueConfig.EvtIoDeviceControl = RawaccelControl;

    //
    // Framework by default creates non-power managed queues for
    // filter drivers.
    //
    status = WdfIoQueueCreate(controlDevice,
        &ioQueueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &queue // pointer to default queue
    );
    if (!NT_SUCCESS(status)) {
        goto Error;
    }

    //
    // Control devices must notify WDF when they are done initializing.   I/O is
    // rejected until this call is made.
    //
    global.control_device = controlDevice;
    WdfControlFinishInitializing(controlDevice);

    return STATUS_SUCCESS;

Error:

    if (pInit != NULL) WdfDeviceInitFree(pInit);

    if (controlDevice != NULL) {
        //
        // Release the reference on the newly created object, since
        // we couldn't initialize it.
        //
        WdfObjectDelete(controlDevice);
    }

    DebugPrint(("CreateControlDevice failed with status code 0x%x\n", status));

    return status;
}

NTSTATUS
EvtDeviceAdd(
    IN WDFDRIVER        Driver,
    IN PWDFDEVICE_INIT  DeviceInit
    )
/*++
Routine Description:

    EvtDeviceAdd is called by the framework in response to AddDevice
    call from the PnP manager. Here you can query the device properties
    using WdfFdoInitWdmGetPhysicalDevice/IoGetDeviceProperty and based
    on that, decide to create a filter device object and attach to the
    function stack.

    If you are not interested in filtering this particular instance of the
    device, you can just return STATUS_SUCCESS without creating a framework
    device.

Arguments:

    Driver - Handle to a framework driver object created in DriverEntry

    DeviceInit - Pointer to a framework-allocated WDFDEVICE_INIT structure.

Return Value:

    NTSTATUS

--*/   
{
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    NTSTATUS status;
    WDFDEVICE hDevice;
    WDF_IO_QUEUE_CONFIG ioQueueConfig;

    UNREFERENCED_PARAMETER(Driver);

    PAGED_CODE();

    DebugPrint(("Enter FilterEvtDeviceAdd \n"));

    if (!global.initialized) {
        return STATUS_SUCCESS;
    }

    //
    // Tell the framework that you are filter driver. Framework
    // takes care of inherting all the device flags & characterstics
    // from the lower device you are attaching to.
    //
    WdfFdoInitSetFilter(DeviceInit);

    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_MOUSE);
    WDF_PNPPOWER_EVENT_CALLBACKS powerCallbacks;
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&powerCallbacks);
    powerCallbacks.EvtDeviceD0Entry = DeviceD0Entry;
    powerCallbacks.EvtDeviceD0Exit = DeviceD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &powerCallbacks);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes,
        DEVICE_EXTENSION);

    deviceAttributes.EvtCleanupCallback = DeviceCleanup;
    deviceAttributes.ExecutionLevel = WdfExecutionLevelPassive;

    //
    // Create a framework device object.  This call will in turn create
    // a WDM deviceobject, attach to the lower stack and set the
    // appropriate flags and attributes.
    //
    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &hDevice);
    if (!NT_SUCCESS(status)) {
        DebugPrint(("WdfDeviceCreate failed with status code 0x%x\n", status));
        return status;
    }

    auto* devExt = FilterGetData(hDevice);
    KeInitializeSpinLock(&devExt->state_lock);
    devExt->config = {};
    devExt->state = {};

    //
    // get device id from bus driver
    //
    DEVICE_OBJECT* pdo = WdfDeviceWdmGetPhysicalDevice(hDevice);
    
    KEVENT ke;
    KeInitializeEvent(&ke, NotificationEvent, FALSE);
    IO_STATUS_BLOCK iosb = {};
    PIRP Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP,
        pdo, NULL, 0, NULL, &ke, &iosb);
    if (!Irp) return STATUS_INSUFFICIENT_RESOURCES;
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    PIO_STACK_LOCATION stack = IoGetNextIrpStackLocation(Irp);
    stack->MinorFunction = IRP_MN_QUERY_ID;
    stack->Parameters.QueryId.IdType = BusQueryDeviceID;

    NTSTATUS tmp = IoCallDriver(pdo, Irp);

    if (tmp == STATUS_PENDING) {
        KeWaitForSingleObject(&ke, Executive, KernelMode, FALSE, NULL);
        tmp = iosb.Status;
    }

    if (NT_SUCCESS(tmp) && iosb.Information) {
        auto* id_ptr = reinterpret_cast<WCHAR*>(iosb.Information);
        wcsncpy(devExt->dev_id, id_ptr, ra::MAX_DEV_ID_LEN - 1);
        devExt->dev_id[ra::MAX_DEV_ID_LEN - 1] = L'\0';
        ExFreePool(id_ptr);
    }
    else {
        DebugPrint(("IoCallDriver failed with status 0x%x\n", tmp));
        *devExt->dev_id = L'\0';
    }

    ULONG propertySize = 0;
    DEVPROPTYPE propertyType = 0;
    NTSTATUS propertyStatus = IoGetDevicePropertyData(pdo, &DEVPKEY_Device_InstanceId,
        LOCALE_NEUTRAL, 0, sizeof(devExt->instance_id), devExt->instance_id,
        &propertySize, &propertyType);
    if (!NT_SUCCESS(propertyStatus) || propertyType != DEVPROP_TYPE_STRING ||
        propertySize < sizeof(WCHAR) || propertySize > sizeof(devExt->instance_id)) {
        devExt->instance_id[0] = L'\0';
    }
    devExt->instance_id[ra::MAX_DEV_ID_LEN - 1] = L'\0';

    WdfWaitLockAcquire(global.collection_lock, NULL);

    DeviceSetup(hDevice);

    tmp = WdfCollectionAdd(global.device_collection, hDevice);

    if (!NT_SUCCESS(tmp)) {
        DebugPrint(("WdfCollectionAdd failed with status 0x%x\n", tmp));
    }

    devExt->in_collection = NT_SUCCESS(tmp);
    if (NT_SUCCESS(tmp) && !global.control_device) {
        NTSTATUS control_status = CreateControlDevice(Driver);
        if (!NT_SUCCESS(control_status)) {
            // A control-interface failure must never prevent normal mouse input.
            DebugPrint(("CreateControlDevice failed: 0x%x\n", control_status));
        }
    }
    WdfWaitLockRelease(global.collection_lock);
    if (!NT_SUCCESS(tmp)) return tmp;

    //
    // Configure the default queue to be Parallel. Do not use sequential queue
    // if this driver is going to be filtering PS2 ports because it can lead to
    // deadlock. The PS2 port driver sends a request to the top of the stack when it
    // receives an ioctl request and waits for it to be completed. If you use a
    // a sequential queue, this request will be stuck in the queue because of the 
    // outstanding ioctl request sent earlier to the port driver.
    //
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioQueueConfig,
                             WdfIoQueueDispatchParallel);

    //
    // Framework by default creates non-power managed queues for
    // filter drivers.
    //
    ioQueueConfig.EvtIoInternalDeviceControl = EvtIoInternalDeviceControl;

    status = WdfIoQueueCreate(hDevice,
                            &ioQueueConfig,
                            WDF_NO_OBJECT_ATTRIBUTES,
                            WDF_NO_HANDLE // pointer to default queue
                            );
    if (!NT_SUCCESS(status)) {
        DebugPrint( ("WdfIoQueueCreate failed 0x%x\n", status));
        return status;
    }

    return status;
}


VOID
EvtIoInternalDeviceControl(
    IN WDFQUEUE      Queue,
    IN WDFREQUEST    Request,
    IN size_t        OutputBufferLength,
    IN size_t        InputBufferLength,
    IN ULONG         IoControlCode
    )
/*++

Routine Description:

    This routine is the dispatch routine for internal device control requests.
    There are two specific control codes that are of interest:
    
    IOCTL_INTERNAL_MOUSE_CONNECT:
        Store the old context and function pointer and replace it with our own.
        This makes life much simpler than intercepting IRPs sent by the RIT and
        modifying them on the way back up.
                                      
    IOCTL_INTERNAL_I8042_HOOK_MOUSE:
        Add in the necessary function pointers and context values so that we can
        alter how the ps/2 mouse is initialized.
                                            
    NOTE:  Handling IOCTL_INTERNAL_I8042_HOOK_MOUSE is *NOT* necessary if 
           all you want to do is filter MOUSE_INPUT_DATAs.  You can remove
           the handling code and all related device extension fields and 
           functions to conserve space.
                                         

--*/
{
    
    PDEVICE_EXTENSION devExt;
    PCONNECT_DATA connectData;
    NTSTATUS status = STATUS_SUCCESS;
    WDFDEVICE hDevice;
    size_t length;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);


    hDevice = WdfIoQueueGetDevice(Queue);
    devExt = FilterGetData(hDevice);

    switch (IoControlCode) {

    //
    // Connect a mouse class device driver to the port driver.
    //
    case IOCTL_INTERNAL_MOUSE_CONNECT: {
        //
        // Only allow one connection.
        //
        //
        // Copy the connection parameters to the device extension.
        //
        status = WdfRequestRetrieveInputBuffer(Request,
            sizeof(CONNECT_DATA),
            reinterpret_cast<PVOID*>(&connectData),
            &length);
        if (!NT_SUCCESS(status)) {
            DebugPrint(("WdfRequestRetrieveInputBuffer failed %x\n", status));
            break;
        }

        if (!connectData->ClassDeviceObject || !connectData->ClassService) {
            status = STATUS_INVALID_PARAMETER;
            break;
        }
        KIRQL irql;
        KeAcquireSpinLock(&devExt->state_lock, &irql);
        if (devExt->UpperConnectData.ClassService != NULL) {
            KeReleaseSpinLock(&devExt->state_lock, irql);
            status = STATUS_SHARING_VIOLATION;
            break;
        }
        devExt->UpperConnectData = *connectData;
        KeReleaseSpinLock(&devExt->state_lock, irql);

        //
        // Hook into the report chain.  Everytime a mouse packet is reported to
        // the system, RawaccelCallback will be called
        //

        connectData->ClassDeviceObject = WdfDeviceWdmGetDeviceObject(hDevice);
        connectData->ClassService = RawaccelCallback;
        WdfRequestFormatRequestUsingCurrentType(Request);
        WdfRequestSetCompletionRoutine(Request, ConnectComplete, devExt);
        if (!WdfRequestSend(Request, WdfDeviceGetIoTarget(hDevice), WDF_NO_SEND_OPTIONS)) {
            status = WdfRequestGetStatus(Request);
            KeAcquireSpinLock(&devExt->state_lock, &irql);
            *connectData = devExt->UpperConnectData;
            devExt->UpperConnectData = {};
            KeReleaseSpinLock(&devExt->state_lock, irql);
            WdfRequestComplete(Request, status);
        }
        return;
    }
    //
    // Disconnect a mouse class device driver from the port driver.
    //
    case IOCTL_INTERNAL_MOUSE_DISCONNECT: 
        //
        // Clear the connection parameters in the device extension.
        //
        // devExt->UpperConnectData.ClassDeviceObject = NULL;
        // devExt->UpperConnectData.ClassService = NULL;
        status = STATUS_NOT_IMPLEMENTED;
        break;
    case IOCTL_MOUSE_QUERY_ATTRIBUTES:
    default:
        break;
    }

    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    DispatchPassThrough(Request, WdfDeviceGetIoTarget(hDevice));
}


VOID
ConnectComplete(WDFREQUEST Request, WDFIOTARGET Target,
                PWDF_REQUEST_COMPLETION_PARAMS Params, WDFCONTEXT Context)
{
    UNREFERENCED_PARAMETER(Target);
    auto* devExt = static_cast<PDEVICE_EXTENSION>(Context);
    if (!NT_SUCCESS(Params->IoStatus.Status)) {
        PCONNECT_DATA connectData = nullptr;
        // The METHOD_NEITHER connect buffer remains owned by the pending request.
        WdfRequestRetrieveInputBuffer(Request, sizeof(CONNECT_DATA),
            reinterpret_cast<PVOID*>(&connectData), nullptr);
        KIRQL irql;
        KeAcquireSpinLock(&devExt->state_lock, &irql);
        if (connectData) *connectData = devExt->UpperConnectData;
        devExt->UpperConnectData = {};
        KeReleaseSpinLock(&devExt->state_lock, irql);
    }
    WdfRequestCompleteWithInformation(Request, Params->IoStatus.Status, Params->IoStatus.Information);
}

VOID
DispatchPassThrough(
    _In_ WDFREQUEST Request,
    _In_ WDFIOTARGET Target
)
/*++
Routine Description:

    Passes a request on to the lower driver.


--*/
{
    //
    // Pass the IRP to the target
    //

    WDF_REQUEST_SEND_OPTIONS options;
    BOOLEAN ret;
    NTSTATUS status = STATUS_SUCCESS;

    //
    // We are not interested in post processing the IRP so 
    // fire and forget.
    //
    WDF_REQUEST_SEND_OPTIONS_INIT(&options,
        WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);

    ret = WdfRequestSend(Request, Target, &options);

    if (ret == FALSE) {
        status = WdfRequestGetStatus(Request);
        DebugPrint(("WdfRequestSend failed: 0x%x\n", status));
        WdfRequestComplete(Request, status);
    }

    return;
}
