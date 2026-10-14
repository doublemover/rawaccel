#pragma once

#include "interop-exception.h"

#include <rawaccel-io.hpp>
#include <rawaccel-validate.hpp>

using namespace System;
using namespace System::Collections::Generic;
using namespace System::Runtime::InteropServices;
using namespace System::Reflection;
using namespace System::Runtime::Serialization;
using namespace Newtonsoft::Json;
using namespace Newtonsoft::Json::Linq;

namespace ra = rawaccel;

ra::modifier_settings default_modifier_settings;
ra::device_settings default_device_settings;

public ref struct VersionHelper
{
    literal String^ VersionString = RA_VER_STRING;

    static Version^ ValidOrThrow()
    {
        try {
            ra::version_t v = ra::valid_version_or_throw();
            return gcnew Version(v.major, v.minor, v.patch, 0);
        }
        catch (const ra::error& e) {
            throw gcnew InteropException(e);
        }
    }
};

[JsonConverter(Converters::StringEnumConverter::typeid)]
public enum class AccelMode
{
    classic, jump, natural, synchronous, power, lut, noaccel
};

[JsonConverter(Converters::StringEnumConverter::typeid)]
public enum class CapMode
{
    in_out, input, output
};

generic <typename T>
[StructLayout(LayoutKind::Sequential)]
public value struct Vec2
{
    T x;
    T y;
};

[StructLayout(LayoutKind::Sequential)]
public value struct AccelArgs
{
    literal int MaxLutPoints = ra::LUT_POINTS_CAPACITY;

    AccelMode mode;

    [JsonProperty("Gain / Velocity")]
    [MarshalAs(UnmanagedType::U1)]
    bool gain;

    double inputOffset;
    double outputOffset;
    double acceleration;
    double decayRate;
    double gamma;
    double motivity;
    double exponentClassic;
    double scale;
    double exponentPower;
    double limit;
    double syncSpeed;
    double smooth;

    [JsonProperty("Cap / Jump")]
    Vec2<double> cap;

    [JsonProperty("Cap mode")]
    CapMode capMode;

    [JsonIgnore]
    int length;

    [JsonProperty(Required = Required::Always)]
    [MarshalAs(UnmanagedType::ByValArray, SizeConst = ra::LUT_RAW_DATA_CAPACITY)]
    array<float>^ data;

    [OnDeserialized]
    void OnDeserializedMethod(StreamingContext context)
    {
        // data->Length must match SizeConst when marshalling
        if (data == nullptr) throw gcnew JsonSerializationException("acceleration data must be an array");
        length = data->Length;
        array<float>::Resize(data, ra::LUT_RAW_DATA_CAPACITY);
    }

    bool IsEquivalentTo(AccelArgs other)
    {
        if (mode != other.mode)
        {
            return false;
        }

        if (mode != AccelMode::noaccel && gain != other.gain)
        {
            return false;
        }

        bool isEquivalent = false;

        switch (mode)
        {
            case AccelMode::noaccel:
                isEquivalent = true;
                break;
            case AccelMode::synchronous:
                isEquivalent =
                    syncSpeed == other.syncSpeed &&
                    motivity == other.motivity &&
                    gamma == other.gamma &&
                    smooth == other.smooth;
                break;
            case AccelMode::classic:
                isEquivalent =
                    exponentClassic == other.exponentClassic &&
                    inputOffset == other.inputOffset &&
                    capMode == other.capMode &&
                    (capMode == CapMode::in_out ?
                        (cap.x == other.cap.x && cap.y == other.cap.y) :
                    capMode == CapMode::output ?
                        (acceleration == other.acceleration && cap.y == other.cap.y) :
                        (acceleration == other.acceleration && cap.x == other.cap.x));
                break;
            case AccelMode::power:
                isEquivalent =
                    exponentPower == other.exponentPower &&
                    outputOffset == other.outputOffset &&
                    capMode == other.capMode &&
                    (capMode == CapMode::in_out ?
                        (cap.x == other.cap.x && cap.y == other.cap.y) :
                    capMode == CapMode::output ?
                        (scale == other.scale && cap.y == other.cap.y) :
                        (scale == other.scale && cap.x == other.cap.x));
                break;
            case AccelMode::natural:
                isEquivalent =
                    decayRate == other.decayRate &&
                    inputOffset == other.inputOffset &&
                    limit == other.limit;
                break;
            case AccelMode::jump:
                isEquivalent =
                    cap.x == other.cap.x &&
                    cap.y == other.cap.y &&
                    smooth == other.smooth;
                break;
            case AccelMode::lut:
                isEquivalent = true;

                if (length != other.length)
                {
                    isEquivalent = false;
                    break;
                }

                if (length < 0 || data == nullptr || other.data == nullptr ||
                    length > data->Length || length > other.data->Length)
                {
                    return false;
                }

                // length counts floats, not (speed, multiplier) pairs.
                for (int i = 0; i < length; i++)
                {
                    if (data[i] != other.data[i])
                    {
                        isEquivalent = false;
                        break;
                    }
                }

                break;
            default:
                break;
        }

        return isEquivalent;
    }
};

[StructLayout(LayoutKind::Sequential)]
public value struct SpeedArgs
{
    [JsonProperty("Whole/combined accel (set false for 'by component' mode)")]
    [MarshalAs(UnmanagedType::U1)]
    bool combineMagnitudes;

    double lpNorm;

    [JsonProperty("Time in ms after which an input is weighted at half its original value.")]
	double inputSmoothHalflife;

    [JsonProperty("Time in ms after which scale is weighted at half its original value.")]
	double scaleSmoothHalflife;

    [JsonProperty("Time in ms after which an output is weighted at half its original value.")]
	double outputSmoothHalflife;
};

[JsonObject(ItemRequired = Required::Always)]
[StructLayout(LayoutKind::Sequential, CharSet = CharSet::Unicode)]
public ref struct Profile
{
    [MarshalAs(UnmanagedType::ByValTStr, SizeConst = ra::MAX_NAME_LEN)]
    System::String^ name;

    [JsonProperty("Stretches domain for horizontal vs vertical inputs")]
    Vec2<double> domainXY;
    [JsonProperty("Stretches accel range for horizontal vs vertical inputs")]
    Vec2<double> rangeXY;

    [JsonProperty("Whole or horizontal accel parameters")]
    AccelArgs argsX;
    [JsonProperty("Vertical accel parameters")]
    AccelArgs argsY;
    [JsonProperty("Input speed calculation parameters")]
    SpeedArgs inputSpeedArgs;

    [JsonProperty("Output DPI")]
    double outputDPI;
    [JsonProperty("Y/X output DPI ratio (vertical sens multiplier)")]
    double yxOutputDPIRatio;
    [JsonProperty("L/R output DPI ratio (left sens multiplier)")]
    double lrOutputDPIRatio;
    [JsonProperty("U/D output DPI ratio (up sens multiplier)")]
    double udOutputDPIRatio;

    [JsonProperty("Degrees of rotation")]
    double rotation;

    [JsonProperty("Degrees of angle snapping")]
    double snap;

    [JsonIgnore]
    double minimumSpeed;
    [JsonProperty("Input Speed Cap")]
    double maximumSpeed;

    Profile(ra::profile& args)
    {
        Marshal::PtrToStructure(IntPtr(&args), this);
    }

    Profile() :
        Profile(default_modifier_settings.prof) {}
};

[JsonObject(ItemRequired = Required::Always)]
[StructLayout(LayoutKind::Sequential, CharSet = CharSet::Unicode)]
public value struct DeviceConfig {
    [MarshalAs(UnmanagedType::U1)]
    bool disable;

    [MarshalAs(UnmanagedType::U1)]
    [JsonProperty(Required = Required::Default)]
    bool setExtraInfo;

    [MarshalAs(UnmanagedType::U1)]
    [JsonProperty("Use constant time interval based on polling rate", Required = Required::Default)]
    bool pollTimeLock;

    [MarshalAs(UnmanagedType::U1)]
    [JsonProperty("Acceleration only while Mouse1 is held", Required = Required::Default)]
    bool accelOnlyWhileMouse1;

    [JsonProperty("DPI (normalizes input speed unit: counts/ms -> in/s)")]
    int dpi;

    [JsonProperty("Polling rate Hz (keep at 0 for automatic adjustment)")]
    int pollingRate;

    [ComponentModel::DefaultValue(ra::DEFAULT_TIME_MIN)]
    [JsonProperty(Required = Required::Default)]
    double minimumTime;

    [ComponentModel::DefaultValue(ra::DEFAULT_TIME_MAX)]
    [JsonProperty(Required = Required::Default)]
    double maximumTime;

    [MarshalAs(UnmanagedType::ByValTStr, SizeConst = ra::MAX_NAME_LEN)]
    [JsonProperty("Left button profile", Required = Required::Default)]
    String^ buttonProfileLeft;

    [MarshalAs(UnmanagedType::ByValTStr, SizeConst = ra::MAX_NAME_LEN)]
    [JsonProperty("Right button profile", Required = Required::Default)]
    String^ buttonProfileRight;

    [MarshalAs(UnmanagedType::ByValTStr, SizeConst = ra::MAX_NAME_LEN)]
    [JsonProperty("Middle button profile", Required = Required::Default)]
    String^ buttonProfileMiddle;

    [MarshalAs(UnmanagedType::ByValTStr, SizeConst = ra::MAX_NAME_LEN)]
    [JsonProperty("X1 button profile", Required = Required::Default)]
    String^ buttonProfileX1;

    [MarshalAs(UnmanagedType::ByValTStr, SizeConst = ra::MAX_NAME_LEN)]
    [JsonProperty("X2 button profile", Required = Required::Default)]
    String^ buttonProfileX2;

    [JsonIgnore]
    property array<String^>^ ButtonProfiles
    {
        array<String^>^ get()
        {
            return gcnew array<String^> { buttonProfileLeft, buttonProfileRight,
                buttonProfileMiddle, buttonProfileX1, buttonProfileX2 };
        }
    }

    void SetButtonProfile(int index, String^ name)
    {
        switch (index) {
        case 0: buttonProfileLeft = name; break;
        case 1: buttonProfileRight = name; break;
        case 2: buttonProfileMiddle = name; break;
        case 3: buttonProfileX1 = name; break;
        case 4: buttonProfileX2 = name; break;
        default: throw gcnew ArgumentOutOfRangeException("index");
        }
    }

    bool ShouldSerializebuttonProfileLeft() { return !String::IsNullOrEmpty(buttonProfileLeft); }
    bool ShouldSerializebuttonProfileRight() { return !String::IsNullOrEmpty(buttonProfileRight); }
    bool ShouldSerializebuttonProfileMiddle() { return !String::IsNullOrEmpty(buttonProfileMiddle); }
    bool ShouldSerializebuttonProfileX1() { return !String::IsNullOrEmpty(buttonProfileX1); }
    bool ShouldSerializebuttonProfileX2() { return !String::IsNullOrEmpty(buttonProfileX2); }

    bool ShouldSerializesetExtraInfo()
    {
        return setExtraInfo == true;
    }

    bool ShouldSerializeminimumTime()
    {
        return minimumTime != ra::DEFAULT_TIME_MIN;
    }

    bool ShouldSerializemaximumTime()
    {
        return maximumTime != ra::DEFAULT_TIME_MAX;
    }

    bool ShouldSerializeaccelOnlyWhileMouse1()
    {
        return accelOnlyWhileMouse1 == true;
    }

    void Init(const ra::device_config& cfg)
    {
        disable = cfg.disable;
        setExtraInfo = cfg.set_extra_info;
        pollTimeLock = cfg.poll_time_lock;
        accelOnlyWhileMouse1 = cfg.accel_only_while_mouse1;
        dpi = cfg.dpi;
        pollingRate = cfg.polling_rate;
        minimumTime = cfg.clamp.min;
        maximumTime = cfg.clamp.max;
        for (int i = 0; i < 5; ++i) SetButtonProfile(i, gcnew String(cfg.button_profiles[i]));
    }
};

[JsonObject(ItemRequired = Required::Always)]
[StructLayout(LayoutKind::Sequential, CharSet = CharSet::Unicode)]
public ref struct DeviceSettings
{
    [MarshalAs(UnmanagedType::ByValTStr, SizeConst = ra::MAX_NAME_LEN)]
    String^ name;

    [MarshalAs(UnmanagedType::ByValTStr, SizeConst = ra::MAX_NAME_LEN)]
    String^ profile;

    [MarshalAs(UnmanagedType::ByValTStr, SizeConst = ra::MAX_DEV_ID_LEN)]
    String^ id;

    DeviceConfig config;

    DeviceSettings(ra::device_settings& args)
    {
        Marshal::PtrToStructure(IntPtr(&args), this);
    }

    DeviceSettings() :
        DeviceSettings(default_device_settings) {}
};


public ref class ProfileErrors
{
    List<String^>^ tmp;
    bool single;

    delegate void MsgHandler(const char*);

    void Add(const char* msg)
    {
        tmp->Add(gcnew String(msg));
    }

public:
    ref struct SingleProfileErrors
    {
        Profile^ prof;
        array<String^>^ messages;
        int lastX;
        int lastY;
    };

    List<SingleProfileErrors^>^ list;

    ProfileErrors(List<Profile^>^ profiles)
    {
        single = profiles->Count == 1;
        list = gcnew List<SingleProfileErrors^>();
        tmp = gcnew List<String^>();
        MsgHandler^ del = gcnew MsgHandler(this, &ProfileErrors::Add);
        GCHandle gch = GCHandle::Alloc(del);
        auto fp = static_cast<void (*)(const char*)>(
            Marshal::GetFunctionPointerForDelegate(del).ToPointer());
        ra::profile native;
        auto* native_ptr = &native;
        try {
            for each (auto prof in profiles) {
                Marshal::StructureToPtr(prof, IntPtr(native_ptr), false);
                auto [last_x, last_y, _] = ra::valid(*native_ptr, fp);

                if (tmp->Count != 0) {
                    auto singleErrors = gcnew SingleProfileErrors();
                    singleErrors->messages = tmp->ToArray();
                    singleErrors->lastX = last_x;
                    singleErrors->lastY = last_y;
                    singleErrors->prof = prof;
                    list->Add(singleErrors);
                    tmp->Clear();
                }
            }
        }
        finally {
            tmp = nullptr;
            gch.Free();
        }
    }

    bool Empty()
    {
        return list->Count == 0;
    }

    virtual String^ ToString() override
    {
        Text::StringBuilder^ sb = gcnew Text::StringBuilder();

        for each (auto elem in list) {
            if (!single) {
                sb->AppendFormat("profile: {0}\n", elem->prof->name);
            }

            auto msgs = elem->messages;
            if (elem->prof->inputSpeedArgs.combineMagnitudes) {
                for (int i = 0; i < elem->lastX; i++) {
                    sb->AppendFormat("\t{0}\n", msgs[i]);
                }
            }
            else {
                for (int i = 0; i < elem->lastX; i++) {
                    sb->AppendFormat("\tx: {0}\n", msgs[i]);
                }
                for (int i = elem->lastX; i < elem->lastY; i++) {
                    sb->AppendFormat("\ty: {0}\n", msgs[i]);
                }
            }

            for (int i = elem->lastY; i < msgs->Length; i++) {
                sb->AppendFormat("\t{0}\n", msgs[i]);
            }
        }

        return sb->ToString();
    }
};

public ref class DeviceErrors
{
    List<String^>^ tmp;
    bool single;

    delegate void MsgHandler(const char*);

    void Add(const char* msg)
    {
        tmp->Add(gcnew String(msg));
    }

public:
    ref struct SingleDeviceErrors
    {
        DeviceSettings^ settings;
        array<String^>^ messages;
    };

    List<SingleDeviceErrors^>^ list;

    DeviceErrors(List<DeviceSettings^>^ devSettings)
    {
        single = devSettings->Count == 1;
        list = gcnew List<SingleDeviceErrors^>();
        tmp = gcnew List<String^>();
        MsgHandler^ del = gcnew MsgHandler(this, &DeviceErrors::Add);
        GCHandle gch = GCHandle::Alloc(del);
        auto fp = static_cast<void (*)(const char*)>(
            Marshal::GetFunctionPointerForDelegate(del).ToPointer());
        ra::device_settings native;
        auto* native_ptr = &native;
        try {
            for each (auto dev in devSettings) {
                Marshal::StructureToPtr(dev, IntPtr(native_ptr), false);
                ra::valid(*native_ptr, fp);

                if (tmp->Count != 0) {
                    auto singleErrors = gcnew SingleDeviceErrors();
                    singleErrors->messages = tmp->ToArray();
                    singleErrors->settings = dev;
                    list->Add(singleErrors);
                    tmp->Clear();
                }
            }
        }
        finally {
            tmp = nullptr;
            gch.Free();
        }
    }

    bool Empty()
    {
        return list->Count == 0;
    }

    virtual String^ ToString() override
    {
        Text::StringBuilder^ sb = gcnew Text::StringBuilder();

        for each (auto elem in list) {
            if (!single) {
                sb->AppendFormat("device: {0}\n", elem->settings->id);
                if (!String::IsNullOrWhiteSpace(elem->settings->name)) {
                    sb->AppendFormat("  name: {0}\n", elem->settings->name);
                }
            }

            for each (auto msg in elem->messages) {
                sb->AppendFormat("\tx: {0}\n", msg);
            }
        }

        return sb->ToString();
    }
};

struct accel_instance_t {
    ra::modifier mod;
    ra::modifier_settings settings;

    accel_instance_t() = default;

    accel_instance_t(ra::modifier_settings& args) : settings(args)
    {
        // READ returns source profiles, never trusted kernel-derived unions.
        ra::init_data(settings);
        mod = { settings };
    }

    void init(Profile^ args)
    {
        Marshal::StructureToPtr(args, IntPtr(&settings.prof), false);
        ra::init_data(settings);
        mod = { settings };
    }
};

struct speed_calc_instance_t
{
    ra::speed_processor speed_calculator = {};
};

public ref class ManagedAccel
{
    accel_instance_t* instance = new accel_instance_t();
    speed_calc_instance_t* speed_instance = new speed_calc_instance_t();

public:

    ManagedAccel() {}

    ManagedAccel(ra::modifier_settings& settings) :
        instance(new accel_instance_t(settings))
    {
        speed_instance->speed_calculator.init(settings.prof.speed_processor_args);
    }

    ManagedAccel(Profile^ settings)
    {
        Settings = settings;
    }

    virtual ~ManagedAccel()
    {
        this->!ManagedAccel();
    }

    !ManagedAccel()
    {
        delete instance;
        instance = nullptr;
        delete speed_instance;
        speed_instance = nullptr;
    }

    /// <summary>
    /// The modifier inside of ManagedAccel is now stateful, which can result in strange graphs when used for graphing purposes.
    /// This method creates a copy of its instance with any stateful elements turned off.
    /// </summary>
    /// <returns></returns>
    ManagedAccel^ CreateStatelessCopy()
    {
        Profile^ profile = Settings;

        profile->inputSpeedArgs.inputSmoothHalflife = 0;
        profile->inputSpeedArgs.scaleSmoothHalflife = 0;
        profile->inputSpeedArgs.outputSmoothHalflife = 0;

        return gcnew ManagedAccel(profile);
    }

    Tuple<double, double>^ Accelerate(int x, int y, double dpi_factor, double time)
    {
        ThrowIfDisposed();
        vec2d in_out_vec = {
            (double)x,
            (double)y
        };

        instance->mod.modify(in_out_vec, speed_instance->speed_calculator, instance->settings, dpi_factor, time);

        return gcnew Tuple<double, double>(in_out_vec.x, in_out_vec.y);
    }

    property Profile^ Settings
    {
        Profile^ get()
        {
            ThrowIfDisposed();
            return gcnew Profile(instance->settings.prof);
        }

        void set(Profile^ val)
        {
            ThrowIfDisposed();
            if (val == nullptr) throw gcnew ArgumentNullException("val");
            instance->init(val);
            ra::speed_args speedArgs;
            Marshal::StructureToPtr(val->inputSpeedArgs, IntPtr(&speedArgs), false);
            speed_instance->speed_calculator.init(speedArgs);
        }
    }

    ra::modifier_settings NativeSettings()
    {
        ThrowIfDisposed();
        return instance->settings;
    }

private:
    void ThrowIfDisposed()
    {
        if (instance == nullptr) throw gcnew ObjectDisposedException("ManagedAccel");
    }
};

public ref class SpeedCalculatorArgs
{
public:
    SpeedCalculatorArgs() {}
    SpeedCalculatorArgs(
        double lp_norm,
        double input_speed_smooth_halflife,
        double scale_smooth_halflife,
        double output_speed_smooth_halflife)
    {
        speed_args->lp_norm = lp_norm;
        speed_args->input_speed_smooth_halflife = input_speed_smooth_halflife;
        speed_args->scale_smooth_halflife = scale_smooth_halflife;
        speed_args->output_speed_smooth_halflife = output_speed_smooth_halflife;
    }

    ~SpeedCalculatorArgs() { this->!SpeedCalculatorArgs(); }
    !SpeedCalculatorArgs()
    {
        delete speed_args;
        speed_args = nullptr;
    }

    ra::speed_args* speed_args = new ra::speed_args();
};

public ref class SpeedCalculator
{
    speed_calc_instance_t* instance = new speed_calc_instance_t();

public:
    SpeedCalculator() {}
    ~SpeedCalculator() { this->!SpeedCalculator(); }
    !SpeedCalculator()
    {
        delete instance;
        instance = nullptr;
    }

    void Init(SpeedCalculatorArgs^ args)
    {
        ThrowIfDisposed();
        if (args == nullptr) throw gcnew ArgumentNullException("args");
        if (args->speed_args == nullptr) throw gcnew ObjectDisposedException("SpeedCalculatorArgs");
        instance->speed_calculator.init(*args->speed_args);
    }

    double CalculateSpeed(double x, double y, double time)
    {
        ThrowIfDisposed();
        vec2d in = { x ,y };
        return instance->speed_calculator.calc_speed_whole(in, time);
    }

    double SmoothScale(double scale, double time)
    {
        ThrowIfDisposed();
        return instance->speed_calculator.smoother_x.scale_smoother.smooth(scale, time);
    }

    double SmoothOutput(double outputSpeed, double time)
    {
        ThrowIfDisposed();
        return instance->speed_calculator.smoother_x.output_speed_smoother.smooth(outputSpeed, time);
    }

private:
    void ThrowIfDisposed()
    {
        if (instance == nullptr) throw gcnew ObjectDisposedException("SpeedCalculator");
    }
};


[JsonObject(ItemRequired = Required::Always)]
public ref class DriverConfig {
public:
    literal double WriteDelayMs = ra::WRITE_DELAY;
    literal String^ Key = "Driver settings";

    String^ version = RA_VER_STRING;

    DeviceConfig defaultDeviceConfig;

    List<Profile^>^ profiles;

    [NonSerialized]
    List<ManagedAccel^>^ accels;

    List<DeviceSettings^>^ devices;

    void Activate()
    {
        auto errors = Errors();
        if (errors != nullptr) throw gcnew ArgumentException(errors);
        if (accels == nullptr || accels->Count != profiles->Count)
            throw gcnew InvalidOperationException("Profile count does not match ManagedAccel");

        const auto modifier_data_bytes = size_t(profiles->Count) * sizeof(ra::modifier_settings);
        const auto device_data_bytes = size_t(devices->Count) * sizeof(ra::device_settings);
        const auto buffer_size = sizeof(ra::io_base) + modifier_data_bytes + device_data_bytes;
        if (buffer_size > DWORD(-1)) throw gcnew ArgumentException("Configuration is too large");
        try {
            auto buffer = std::make_unique<std::byte[]>(buffer_size);
            auto* byte_ptr = buffer.get();
            auto* base_data = reinterpret_cast<ra::io_base*>(byte_ptr);
            Marshal::StructureToPtr(defaultDeviceConfig, IntPtr(&base_data->default_dev_cfg), false);
            base_data->modifier_data_size = profiles->Count;
            base_data->device_data_size = devices->Count;
            byte_ptr += sizeof(ra::io_base);

            auto* modifier_data = reinterpret_cast<ra::modifier_settings*>(byte_ptr);
            for (int i = 0; i < profiles->Count; ++i) {
                // Public profile fields may have changed since deserialization.
                accels[i]->Settings = profiles[i];
                modifier_data[i] = accels[i]->NativeSettings();
            }
            byte_ptr += modifier_data_bytes;
            auto* device_data = reinterpret_cast<ra::device_settings*>(byte_ptr);
            for (int i = 0; i < devices->Count; ++i)
                Marshal::StructureToPtr(devices[i], IntPtr(&device_data[i]), false);
            ra::write(buffer.get(), buffer_size);
        }
        catch (const std::exception& e) {
            throw gcnew InteropException(e);
        }
    }

    void SetProfileAt(int index, Profile^ val)
    {
        profiles[index] = val;
        accels[index]->Settings = val;
    }

    // returns null or a joined list of error messages
    String^ Errors()
    {
        Text::StringBuilder^ sb = gcnew Text::StringBuilder();
        if (profiles == nullptr || devices == nullptr) return "profiles and devices must be arrays";
        if (profiles->Count > 1024) return "configuration supports at most 1024 profiles";
        if (devices->Count > 4096) return "configuration supports at most 4096 device overrides";
        const auto bytes = sizeof(ra::io_base) + size_t(profiles->Count) * sizeof(ra::modifier_settings) +
            size_t(devices->Count) * sizeof(ra::device_settings);
        if (bytes > 16 * 1024 * 1024) return "configuration exceeds the 16 MiB driver limit";
        auto names = gcnew HashSet<String^>(StringComparer::Ordinal);
        for each (auto prof in profiles) {
            if (prof == nullptr) return "profiles must not contain null entries";
            if (String::IsNullOrEmpty(prof->name) || prof->name->Length >= ra::MAX_NAME_LEN ||
                prof->name->IndexOf(L'\0') >= 0) return "profile names must contain 1 to 255 characters and no nulls";
            if (!names->Add(prof->name)) return "profile names must be unique";
            if (prof->argsX.data == nullptr || prof->argsY.data == nullptr ||
                prof->argsX.data->Length != ra::LUT_RAW_DATA_CAPACITY ||
                prof->argsY.data->Length != ra::LUT_RAW_DATA_CAPACITY)
                return "acceleration data must use the fixed marshal capacity";
        }
        auto ids = gcnew HashSet<String^>(StringComparer::OrdinalIgnoreCase);
        for each (auto dev in devices) {
            if (dev == nullptr) return "devices must not contain null entries";
            if (String::IsNullOrEmpty(dev->id) || dev->id->Length >= ra::MAX_DEV_ID_LEN ||
                dev->id->IndexOf(L'\0') >= 0) return "device IDs must contain 1 to 199 characters and no nulls";
            if (!ids->Add(dev->id)) return "device IDs must be unique";
            if ((dev->name != nullptr && dev->name->Length >= ra::MAX_NAME_LEN) ||
                (dev->profile != nullptr && dev->profile->Length >= ra::MAX_NAME_LEN))
                return "device and profile names must be shorter than 256 characters";
        }
        auto configs = gcnew List<DeviceConfig>();
        configs->Add(defaultDeviceConfig);
        for each (auto dev in devices) configs->Add(dev->config);
        for each (auto config in configs) {
            for each (auto name in config.ButtonProfiles) {
                if (!String::IsNullOrEmpty(name) && !names->Contains(name))
                    sb->AppendFormat("button profile '{0}' does not exist\n", name);
            }
        }

        ProfileErrors^ profErrors = gcnew ProfileErrors(profiles);
        if (!profErrors->Empty()) {
            sb->Append(profErrors->ToString());
        }

        DeviceSettings^ defaultDev = gcnew DeviceSettings();
        defaultDev->config = defaultDeviceConfig;
        defaultDev->id = "Default";
        auto devicesToValidate = gcnew List<DeviceSettings^>(devices);
        devicesToValidate->Add(defaultDev);

        DeviceErrors^ devErrors = gcnew DeviceErrors(devicesToValidate);
        if (!devErrors->Empty()) {
            sb->Append(devErrors->ToString());
        }

        if (sb->Length == 0) {
            return nullptr;
        }
        else {
            return sb->ToString();
        }
    }

    JObject^ ToJObject()
    {
        // Serialize first, then trim the JSON copy. Never resize live arrays:
        // graphing and configuration writes may still be using them.
        JObject^ jObject = JObject::FromObject(this);
        auto jsonProfiles = safe_cast<JArray^>(jObject["profiles"]);
        auto keys = gcnew array<String^> { "Whole or horizontal accel parameters", "Vertical accel parameters" };
        for (int i = 0; i < profiles->Count; ++i) {
            auto axes = gcnew array<AccelArgs> { profiles[i]->argsX, profiles[i]->argsY };
            for (int axis = 0; axis < 2; ++axis) {
                auto args = axes[axis];
                auto values = gcnew JArray();
                if (args.mode == AccelMode::lut) {
                    if (args.data == nullptr || args.length < 0 || args.length > args.data->Length)
                        throw gcnew JsonSerializationException("invalid lookup data length");
                    for (int j = 0; j < args.length; ++j) values->Add(args.data[j]);
                }
                auto argsJson = safe_cast<JObject^>(jsonProfiles[i][keys[axis]]);
                argsJson["data"] = values;
            }
        }
        String^ capModes = String::Join(" | ", Enum::GetNames(CapMode::typeid));
        String^ accelModes = String::Join(" | ", Enum::GetNames(AccelMode::typeid));
        jObject->AddFirst(gcnew JProperty("### Cap modes ###", capModes));
        jObject->AddFirst(gcnew JProperty("### Accel modes ###", accelModes));

        return jObject;
    }

    String^ ToJSON()
    {
        return ToJObject()->ToString();
    }

    // returns (config, null) or (null, error message)
    static Tuple<DriverConfig^, String^>^ Convert(String^ json)
    {
        auto jss = gcnew JsonSerializerSettings();
        jss->DefaultValueHandling = DefaultValueHandling::Populate;
        auto cfg = JsonConvert::DeserializeObject<DriverConfig^>(json, jss);
        if (cfg == nullptr) throw gcnew JsonException("invalid JSON");
        cfg->version = RA_VER_STRING;

        if (cfg->profiles != nullptr && cfg->profiles->Count == 0) cfg->profiles->Add(gcnew Profile());
        auto message = cfg->Errors();
        if (message != nullptr) {
            return gcnew Tuple<DriverConfig^, String^>(nullptr, message);
        }
        else {
            cfg->accels = gcnew List<ManagedAccel^>();

            if (cfg->profiles->Count == 0) {
                cfg->profiles->Add(gcnew Profile());
            }

            for each (auto prof in cfg->profiles) {
                cfg->accels->Add(gcnew ManagedAccel(prof));
            }
            return gcnew Tuple<DriverConfig^, String^>(cfg, nullptr);
        }
    }

    static DriverConfig^ GetActive()
    {
        std::unique_ptr<std::byte[]> bytes;
        try {
            bytes = ra::read();
        }
        catch (const std::exception& e) {
            throw gcnew InteropException(e);
        }

        auto cfg = gcnew DriverConfig();
        cfg->profiles = gcnew List<Profile^>();
        cfg->accels = gcnew List<ManagedAccel^>();
        cfg->devices = gcnew List<DeviceSettings^>();

        auto* byte_ptr = bytes.get();
        ra::io_base* base_data = reinterpret_cast<ra::io_base*>(byte_ptr);
        cfg->defaultDeviceConfig.Init(base_data->default_dev_cfg);

        byte_ptr += sizeof(ra::io_base);

        ra::modifier_settings* modifier_data = reinterpret_cast<ra::modifier_settings*>(byte_ptr);
        for (auto i = 0u; i < base_data->modifier_data_size; i++) {
            auto& mod_settings = modifier_data[i];
            cfg->profiles->Add(gcnew Profile(mod_settings.prof));
            cfg->accels->Add(gcnew ManagedAccel(mod_settings));
        }

        byte_ptr += base_data->modifier_data_size * sizeof(ra::modifier_settings);

        ra::device_settings* device_data = reinterpret_cast<ra::device_settings*>(byte_ptr);
        for (auto i = 0u; i < base_data->device_data_size; i++) {
            auto& dev_settings = device_data[i];
            cfg->devices->Add(gcnew DeviceSettings(dev_settings));
        }

        return cfg;
    }

    static DriverConfig^ FromProfile(Profile^ prof)
    {
        auto cfg = gcnew DriverConfig();
        cfg->profiles = gcnew List<Profile^>();
        cfg->accels = gcnew List<ManagedAccel^>();
        cfg->devices = gcnew List<DeviceSettings^>();

        cfg->profiles->Add(prof);
        cfg->accels->Add(gcnew ManagedAccel(prof));
        cfg->defaultDeviceConfig.Init(default_device_settings.config);
        return cfg;
    }

    static DriverConfig^ GetDefault()
    {
        return FromProfile(gcnew Profile());
    }

    static void Deactivate()
    {
        try {
            ra::reset();
        }
        catch (const std::exception& e) {
            throw gcnew InteropException(e);
        }
    }

private:
    DriverConfig() {}
};

