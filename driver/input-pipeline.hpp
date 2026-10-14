#pragma once

#include "rawaccel.hpp"

// Platform-independent input transformation. The WDF adapter owns synchronization
// and packet delivery. Everything here is allocation-free after configuration.
namespace rawaccel {
    struct prepared_profile {
        modifier_settings settings;
        modifier transform;
        speed_processor initial_speed;

        void init(const modifier_settings& source)
        {
            settings = source;
            init_data(settings);
            transform = modifier(settings);
            initial_speed.init(settings.prof.speed_processor_args);
        }
    };

    struct input_configuration {
        bool enabled = false;
        bool set_extra_info = false;
        bool mouse1_only = false;
        bool keep_time = true;
        double dpi_factor = 1;
        time_clamp clamp;
        const prepared_profile* base_profile = nullptr;
        const prepared_profile* button_profiles[MOUSE_BUTTON_COUNT] = {};
    };

    struct input_state {
        unsigned short buttons = 0;
        const prepared_profile* active_profile = nullptr;
        speed_processor speed;
        vec2d carry = {};
        long long counter = 0;
    };

    inline void reset_motion(input_state& state)
    {
        state.active_profile = nullptr;
        state.carry = {};
        state.counter = 0;
    }

    inline const prepared_profile* selected_profile(
        const input_configuration& config, unsigned short buttons)
    {
        if (!config.enabled) return nullptr;
        // Explicit bindings override the legacy gate. Highest numbered held
        // binding wins: X2, X1, middle, right, left. Unbound buttons are ignored.
        for (size_t i = MOUSE_BUTTON_COUNT; i != 0; --i) {
            if ((buttons & (1u << (i - 1))) && config.button_profiles[i - 1]) {
                return config.button_profiles[i - 1];
            }
        }
        return (!config.mouse1_only || (buttons & 1)) ? config.base_profile : nullptr;
    }

    // Check before conversion: converting NaN, infinity or an out-of-range double
    // to LONG is undefined C++, even if the fractional carry is checked later.
    // Bit-based infnan remains valid under the driver's /fp:fast setting.
    inline bool representable_motion(double x)
    {
        return !infnan(x) && x >= -2147483648.0 && x <= 2147483647.0;
    }

    inline void update_buttons(input_state& state, unsigned short button_flags)
    {
        for (size_t i = 0; i < MOUSE_BUTTON_COUNT; ++i) {
            const unsigned short held = static_cast<unsigned short>(1u << i);
            if (button_flags & (1u << (2 * i))) state.buttons |= held;
            if (button_flags & (2u << (2 * i))) state.buttons &= ~held;
        }
    }

    template <typename Packet>
    void transform_packet(Packet& packet, const input_configuration& config,
                          input_state& state, milliseconds time)
    {
        if (packet.Flags & 4u) return; // MOUSE_ATTRIBUTES_CHANGED: other fields are unspecified.
        // Five down/up pairs occupy bits 0..9. Combined down+up ends released.
        update_buttons(state, packet.ButtonFlags);
        const auto* profile = selected_profile(config, state.buttons);
        if (profile != state.active_profile) {
            state.active_profile = profile;
            state.carry = {};
            if (profile) state.speed = profile->initial_speed;
        }

        // MOUSE_MOVE_ABSOLUTE == 1. Button state still advances for these packets.
        if (packet.Flags & 1u) return;
        if (config.enabled && config.set_extra_info) {
            packet.ExtraInformation =
                static_cast<unsigned long>(static_cast<unsigned short>(packet.LastX)) |
                (static_cast<unsigned long>(static_cast<unsigned short>(packet.LastY)) << 16);
        }
        if (!profile || (!packet.LastX && !packet.LastY)) return;

        vec2d input = { static_cast<double>(packet.LastX), static_cast<double>(packet.LastY) };
        profile->transform.modify(input, state.speed, profile->settings, config.dpi_factor, time);
        const double x = input.x + state.carry.x;
        const double y = input.y + state.carry.y;
        if (!representable_motion(x) || !representable_motion(y)) {
            // Fail open: preserve original motion and make the next valid packet
            // independent of any poisoned smoothing or fractional state.
            state.carry = {};
            state.speed = profile->initial_speed;
            return;
        }
        const auto out_x = static_cast<decltype(packet.LastX)>(x);
        const auto out_y = static_cast<decltype(packet.LastY)>(y);
        state.carry = { x - out_x, y - out_y };
        packet.LastX = out_x;
        packet.LastY = out_y;
    }
    // Testable delivery transaction. Lock/unlock protect configuration and state;
    // send must synchronously report the accepted prefix and is called unlocked.
    template <typename Device, typename Packet, typename Count,
              typename Lock, typename Unlock, typename Clock, typename Send>
    void deliver_packets(Device& device, Packet* begin, Packet* end, Count* total,
                         double tick_interval, Lock lock, Unlock unlock,
                         Clock clock, Send send)
    {
        *total = 0;
        if (begin == end) return;
        lock();
        if (device.callback_busy) { unlock(); return; }
        device.callback_busy = true;
        const auto generation = device.configuration_generation;
        const auto input_epoch = device.input_epoch;
        const auto before_counter = device.state.counter;
        const auto now = device.config.enabled && device.config.keep_time ? clock() : 0;
        auto time = device.config.clamp.min;
        if (device.config.keep_time && before_counter && now > before_counter) {
            time = clampsd((now - before_counter) * tick_interval / (end - begin),
                           device.config.clamp.min, device.config.clamp.max);
        }
        device.state.counter = now;
        unlock();

        constexpr size_t chunk_capacity = 32;
        Packet scratch[chunk_capacity];
        auto* current = begin;
        while (current != end) {
            const auto count = static_cast<Count>(min<size_t>(end - current, chunk_capacity));
            for (Count i = 0; i < count; ++i) scratch[i] = current[i];
            lock();
            if (device.configuration_generation != generation) { unlock(); break; }
            auto working = device.state;
            for (Count i = 0; i < count; ++i) {
                transform_packet(scratch[i], device.config, working, time);
            }
            unlock();
            Count consumed = 0;
            send(scratch, scratch + count, &consumed);
            consumed = min(consumed, count);
            lock();
            if (device.configuration_generation == generation) {
                if (consumed == count) device.state = working;
                else {
                    // The public state stayed committed while Mouclass ran.
                    for (Count i = 0; i < consumed; ++i) {
                        auto packet = current[i];
                        transform_packet(packet, device.config, device.state, time);
                    }
                    if (*total == 0 && consumed == 0) device.state.counter = before_counter;
                }
            }
            else if (device.input_epoch == input_epoch) {
                // Apply can replace profiles during delivery. Only accepted
                // physical button transitions survive into the new settings.
                // A power transition starts a new epoch and deliberately clears them.
                for (Count i = 0; i < consumed; ++i) {
                    if (!(current[i].Flags & 4u)) update_buttons(device.state, current[i].ButtonFlags);
                }
            }
            unlock();
            *total += consumed;
            current += consumed;
            if (consumed != count) break;
        }
        lock();
        device.callback_busy = false;
        unlock();
    }

}
