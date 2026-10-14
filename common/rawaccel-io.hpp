#pragma once

#include "rawaccel-io-def.h"
#include "rawaccel-version.h"
#include "rawaccel-error.hpp"
#include "rawaccel.hpp"

#include <memory>
#include <cstring>

namespace rawaccel {

    // Capture the Win32 error before closing the handle: CloseHandle may change it.
    inline DWORD io_control(DWORD code, void* in, DWORD in_size, void* out, DWORD out_size)
    {
        HANDLE handle = CreateFileW(L"\\\\.\\rawaccel", 0, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            const DWORD error_code = GetLastError();
            if (error_code == ERROR_FILE_NOT_FOUND || error_code == ERROR_PATH_NOT_FOUND)
                throw install_error();
            throw sys_error("CreateFile failed", error_code);
        }

        DWORD returned = 0;
        const BOOL success = DeviceIoControl(handle, code, in, in_size, out, out_size,
            &returned, nullptr);
        const DWORD error_code = success ? ERROR_SUCCESS : GetLastError();
        CloseHandle(handle);
        if (!success) throw sys_error("DeviceIoControl failed", error_code);
        if (returned > out_size) throw io_error("driver returned an invalid byte count");
        return returned;
    }

    inline size_t snapshot_size(const io_base& base)
    {
        // Check before multiplying/adding, including on 32-bit clients.
        const size_t limit = DWORD(-1);
        size_t size = sizeof(io_base);
        if (base.modifier_data_size > (limit - size) / sizeof(modifier_settings))
            throw io_error("modifier data is too large");
        size += size_t(base.modifier_data_size) * sizeof(modifier_settings);
        if (base.device_data_size > (limit - size) / sizeof(device_settings))
            throw io_error("device data is too large");
        size += size_t(base.device_data_size) * sizeof(device_settings);
        return size;
    }

    inline std::unique_ptr<std::byte[]> read()
    {
        size_t capacity = sizeof(io_base);
        // READ returns a header-only success when its buffer is too small. A
        // concurrent writer can change the required size between any two calls.
        for (unsigned attempt = 0; attempt < 8; ++attempt) {
            auto bytes = std::make_unique<std::byte[]>(capacity);
            const DWORD returned = io_control(READ, nullptr, 0, bytes.get(), DWORD(capacity));
            if (returned < sizeof(io_base)) throw io_error("driver returned a truncated header");
            io_base base;
            std::memcpy(&base, bytes.get(), sizeof(base));
            const size_t required = snapshot_size(base);
            if (base.modifier_data_size == 0 && base.device_data_size != 0)
                throw io_error("driver returned device data without a profile");
            if (required > capacity) {
                if (returned != sizeof(io_base)) throw io_error("driver returned a truncated snapshot");
                capacity = required;
                continue;
            }
            if (returned != required) throw io_error("driver returned a truncated snapshot");
            if (base.modifier_data_size != 0) return bytes;

            // Expose the empty driver configuration as one default profile.
            auto defaults = std::make_unique<std::byte[]>(sizeof(base) + sizeof(modifier_settings));
            base.modifier_data_size = 1;
            const modifier_settings settings{};
            std::memcpy(defaults.get(), &base, sizeof(base));
            std::memcpy(defaults.get() + sizeof(base), &settings, sizeof(settings));
            return defaults;
        }
        throw io_error("driver configuration kept changing; retry the read");
    }

    inline void write(const void* buffer, size_t buffer_size)
    {
        if (!buffer) throw io_error("write buffer is null");
        if (buffer_size < sizeof(io_base)) throw io_error("write buffer is too small");
        io_base base;
        std::memcpy(&base, buffer, sizeof(base));
        const size_t size = snapshot_size(base);
        if (size != buffer_size) throw io_error("write buffer length does not match its header");
        if (base.modifier_data_size == 0 && base.device_data_size != 0)
            throw io_error("device data requires a profile");
        io_control(WRITE, const_cast<void*>(buffer), DWORD(size), nullptr, 0);
    }

    // Compatibility overload: the caller must provide the complete buffer
    // described by its header. New callers should pass the allocation length.
    inline void write(const void* buffer)
    {
        if (!buffer) throw io_error("write buffer is null");
        io_base base;
        std::memcpy(&base, buffer, sizeof(base));
        write(buffer, snapshot_size(base));
    }

    inline void reset()
    {
        io_base base_data{};
        // all modifier/device data is cleared when a default io_base is passed
        io_control(WRITE, &base_data, sizeof(io_base), NULL, 0);
    }

    inline version_t get_version() 
    {
        version_t v{};
        try {
            if (io_control(GET_VERSION, nullptr, 0, &v, sizeof(v)) != sizeof(v))
                throw io_error("driver returned a truncated version");
        }
        catch (const sys_error& e) {
            // Only old drivers' unsupported IOCTL responses imply version < 1.3.
            if (e.code() != ERROR_INVALID_FUNCTION && e.code() != ERROR_NOT_SUPPORTED)
                throw;
            v = {};
        }

        return v;
    }

    inline version_t valid_version_or_throw()
    {
        auto v = get_version();

        if (v < min_driver_version) {
            throw error("reinstallation required");
        }

        if (version < v) {
            throw error("newer driver is installed");
        }

        return v;
    }

}
