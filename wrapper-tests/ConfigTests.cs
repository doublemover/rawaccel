using Microsoft.VisualStudio.TestTools.UnitTesting;
using System;
using System.Runtime.InteropServices;

namespace wrapper_tests
{
    [TestClass]
    public class ConfigTests
    {
        [TestMethod]
        public void DeviceConfig_Mouse1Only_RoundTripsThroughJson()
        {
            var cfg = DriverConfig.GetDefault();

            var defaultConfig = cfg.defaultDeviceConfig;
            defaultConfig.accelOnlyWhileMouse1 = true;
            cfg.defaultDeviceConfig = defaultConfig;

            var device = new DeviceSettings();
            device.id = "test-device";
            device.name = "Test Device";
            device.profile = string.Empty;

            var deviceConfig = device.config;
            deviceConfig.accelOnlyWhileMouse1 = true;
            device.config = deviceConfig;
            cfg.devices.Add(device);

            string json = cfg.ToJSON();
            StringAssert.Contains(json, "Acceleration only while Mouse1 is held");

            var converted = DriverConfig.Convert(json);
            Assert.IsNull(converted.Item2);
            Assert.IsTrue(converted.Item1.defaultDeviceConfig.accelOnlyWhileMouse1);
            Assert.IsTrue(converted.Item1.devices[0].config.accelOnlyWhileMouse1);
        }

        [TestMethod]
        public void DeviceConfig_Mouse1Only_IsInNativeMarshalLayout()
        {
            int mouse1Offset = Marshal.OffsetOf(typeof(DeviceConfig), nameof(DeviceConfig.accelOnlyWhileMouse1)).ToInt32();
            int dpiOffset = Marshal.OffsetOf(typeof(DeviceConfig), nameof(DeviceConfig.dpi)).ToInt32();

            Assert.AreEqual(3, mouse1Offset);
            Assert.AreEqual(4, dpiOffset);

            var config = new DeviceConfig();
            config.accelOnlyWhileMouse1 = true;
            byte[] configBytes = StructureBytes(config, typeof(DeviceConfig));

            Assert.AreEqual(1, configBytes[mouse1Offset]);

            var settings = new DeviceSettings();
            settings.id = "test-device";
            settings.name = "Test Device";
            settings.profile = string.Empty;

            var settingsConfig = settings.config;
            settingsConfig.accelOnlyWhileMouse1 = true;
            settings.config = settingsConfig;

            byte[] settingsBytes = StructureBytes(settings, typeof(DeviceSettings));
            int settingsConfigOffset = Marshal.OffsetOf(typeof(DeviceSettings), nameof(DeviceSettings.config)).ToInt32();

            Assert.AreEqual(1, settingsBytes[settingsConfigOffset + mouse1Offset]);
        }

        [TestMethod]
        public void DriverConfigErrors_DeviceConfigError_ReturnsDeviceErrorText()
        {
            var cfg = DriverConfig.GetDefault();
            var defaultConfig = cfg.defaultDeviceConfig;
            defaultConfig.dpi = -1;
            cfg.defaultDeviceConfig = defaultConfig;

            string errors = cfg.Errors();

            Assert.IsNotNull(errors);
            StringAssert.Contains(errors, "dpi can not be negative");
        }

        private static byte[] StructureBytes(object value, Type type)
        {
            int size = Marshal.SizeOf(type);
            IntPtr ptr = Marshal.AllocHGlobal(size);
            bool marshaled = false;

            try
            {
                Marshal.StructureToPtr(value, ptr, false);
                marshaled = true;
                byte[] bytes = new byte[size];
                Marshal.Copy(ptr, bytes, 0, size);
                return bytes;
            }
            finally
            {
                if (marshaled)
                {
                    Marshal.DestroyStructure(ptr, type);
                }
                Marshal.FreeHGlobal(ptr);
            }
        }
    }
}
