using Microsoft.VisualStudio.TestTools.UnitTesting;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using System;
using System.Runtime.InteropServices;

namespace wrapper_tests
{
    [TestClass]
    public class UserModeRegressionTests
    {
        [TestMethod]
        public void NoAccelEquivalenceIgnoresUnusedParameters()
        {
            var x = new Profile().argsX;
            var y = new Profile().argsY;
            x.mode = y.mode = AccelMode.noaccel;
            x.gamma = 3;
            x.gain = false;
            Assert.IsTrue(x.IsEquivalentTo(y));
            Assert.IsTrue(y.IsEquivalentTo(x));
        }

        [TestMethod]
        public void PowerEquivalenceUsesScaleInsteadOfAcceleration()
        {
            foreach (var capMode in new[] { CapMode.input, CapMode.output })
            {
                var x = new Profile().argsX;
                var y = new Profile().argsY;
                x.mode = y.mode = AccelMode.power;
                x.capMode = y.capMode = capMode;
                x.scale = y.scale = 2;
                x.acceleration = 0.1;
                y.acceleration = 0.7;
                Assert.IsTrue(x.IsEquivalentTo(y));
                y.scale = 3;
                Assert.IsFalse(x.IsEquivalentTo(y));
            }
        }

        [TestMethod]
        public void LutEquivalenceChecksAllFloatsWithoutReadingPastCapacity()
        {
            var x = new Profile().argsX;
            var y = new Profile().argsY;
            x.mode = y.mode = AccelMode.lut;
            x.length = y.length = AccelArgs.MaxLutPoints * 2;
            for (int i = 0; i < x.length; ++i) x.data[i] = y.data[i] = i;
            Assert.IsTrue(x.IsEquivalentTo(y));
            y.data[y.length - 1] += 1;
            Assert.IsFalse(x.IsEquivalentTo(y));
            x.length = y.length = int.MaxValue;
            Assert.IsFalse(x.IsEquivalentTo(y));
        }

        [TestMethod]
        public void SerializationDoesNotReplaceOrTrimLiveArrays()
        {
            var cfg = DriverConfig.GetDefault();
            var profile = cfg.profiles[0];
            profile.argsX.mode = AccelMode.lut;
            profile.argsX.length = 4;
            profile.argsX.data[0] = 1;
            profile.argsX.data[1] = 1;
            profile.argsX.data[2] = 2;
            profile.argsX.data[3] = 2;
            profile.argsX.data[20] = 123;
            var original = profile.argsX.data;
            var other = profile.argsY.data;
            var serialized = JObject.Parse(cfg.ToJSON());
            Assert.AreEqual(4, ((JArray)serialized["profiles"][0]["Whole or horizontal accel parameters"]["data"]).Count);
            Assert.AreEqual(0, ((JArray)serialized["profiles"][0]["Vertical accel parameters"]["data"]).Count);
            Assert.AreSame(original, profile.argsX.data);
            Assert.AreSame(other, profile.argsY.data);
            Assert.AreEqual(123f, profile.argsX.data[20]);
        }

        [TestMethod]
        public void NullLutDataProducesJsonError()
        {
            var json = JObject.Parse(DriverConfig.GetDefault().ToJSON());
            json["profiles"][0]["Whole or horizontal accel parameters"]["data"] = JValue.CreateNull();
            Assert.ThrowsException<JsonSerializationException>(() => DriverConfig.Convert(json.ToString()));
        }

        [TestMethod]
        public void NullEntriesAreRejectedBeforeMarshalling()
        {
            var cfg = DriverConfig.GetDefault();
            cfg.profiles.Add(null);
            StringAssert.Contains(cfg.Errors(), "null entries");
            cfg.profiles.RemoveAt(1);
            cfg.devices.Add(null);
            StringAssert.Contains(cfg.Errors(), "null entries");
        }

        [TestMethod]
        public void ProfileNamesCannotCollideAfterNativeTruncation()
        {
            var cfg = DriverConfig.GetDefault();
            cfg.profiles[0].name = new string('a', 256);
            Assert.IsNotNull(cfg.Errors());
            cfg.profiles[0].name = "same";
            cfg.profiles.Add(new Profile { name = "same" });
            StringAssert.Contains(cfg.Errors(), "unique");
        }

        [TestMethod]
        public void DeviceValidationDoesNotMutateDeviceList()
        {
            var cfg = DriverConfig.GetDefault();
            var device = new DeviceSettings { id = "test-device", name = "Test", profile = "" };
            cfg.devices.Add(device);
            var config = cfg.defaultDeviceConfig;
            config.dpi = -1;
            cfg.defaultDeviceConfig = config;
            Assert.IsNotNull(cfg.Errors());
            Assert.AreEqual(1, cfg.devices.Count);
            Assert.AreSame(device, cfg.devices[0]);
        }

        [TestMethod]
        public void FiveButtonBindingsRoundTripAndPreserveLegacyGate()
        {
            var cfg = DriverConfig.GetDefault();
            var config = cfg.defaultDeviceConfig;
            config.accelOnlyWhileMouse1 = true;
            for (int i = 0; i < 5; ++i)
            {
                string name = "button-" + i;
                cfg.profiles.Add(new Profile { name = name });
                config.SetButtonProfile(i, name);
            }
            cfg.defaultDeviceConfig = config;
            cfg.devices.Add(new DeviceSettings { id = "HID\\MODEL\\INSTANCE", name = "Mouse", profile = "", config = config });
            var result = DriverConfig.Convert(cfg.ToJSON());
            Assert.IsNull(result.Item2);
            Assert.IsTrue(result.Item1.defaultDeviceConfig.accelOnlyWhileMouse1);
            CollectionAssert.AreEqual(config.ButtonProfiles, result.Item1.defaultDeviceConfig.ButtonProfiles);
            CollectionAssert.AreEqual(config.ButtonProfiles, result.Item1.devices[0].config.ButtonProfiles);
        }

        [TestMethod]
        public void OldJsonDefaultsToNoButtonOverrides()
        {
            var result = DriverConfig.Convert(DriverConfig.GetDefault().ToJSON());
            Assert.IsNull(result.Item2);
            foreach (string name in result.Item1.defaultDeviceConfig.ButtonProfiles)
                Assert.IsTrue(string.IsNullOrEmpty(name));
        }

        [TestMethod]
        public void MissingBoundProfilesAreRejected()
        {
            var cfg = DriverConfig.GetDefault();
            var config = cfg.defaultDeviceConfig;
            config.buttonProfileRight = "missing";
            cfg.defaultDeviceConfig = config;
            StringAssert.Contains(cfg.Errors(), "does not exist");
        }

        [TestMethod]
        public void ButtonProfileMarshalLayoutMatchesNativeArray()
        {
            string[] fields = { "buttonProfileLeft", "buttonProfileRight", "buttonProfileMiddle", "buttonProfileX1", "buttonProfileX2" };
            var config = DriverConfig.GetDefault().defaultDeviceConfig;
            for (int i = 0; i < fields.Length; ++i)
            {
                Assert.AreEqual(32 + i * 512, Marshal.OffsetOf(typeof(DeviceConfig), fields[i]).ToInt32());
                config.SetButtonProfile(i, "P" + i);
            }
            Assert.AreEqual(2592, Marshal.SizeOf(typeof(DeviceConfig)));
            IntPtr ptr = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(DeviceConfig)));
            try
            {
                Marshal.StructureToPtr(config, ptr, false);
                for (int i = 0; i < fields.Length; ++i)
                    Assert.AreEqual("P" + i, Marshal.PtrToStringUni(IntPtr.Add(ptr, 32 + i * 512)));
            }
            finally { Marshal.FreeHGlobal(ptr); }
        }

        [TestMethod]
        public void DriverLimitsAreReportedBeforeNativeMarshalling()
        {
            var cfg = DriverConfig.GetDefault();
            var profile = cfg.profiles[0];
            while (cfg.profiles.Count <= 1024) cfg.profiles.Add(profile);
            StringAssert.Contains(cfg.Errors(), "1024 profiles");
            cfg.profiles.RemoveRange(1, cfg.profiles.Count - 1);
            while (cfg.devices.Count <= 4096) cfg.devices.Add(null);
            StringAssert.Contains(cfg.Errors(), "4096 device");
        }

        [TestMethod]
        public void ManagedAccelDisposalIsIdempotentAndRejectsReuse()
        {
            var accel = new ManagedAccel();
            accel.Dispose();
            accel.Dispose();
            Assert.ThrowsException<ObjectDisposedException>(() => accel.Accelerate(1, 1, 1, 1));
        }

        [TestMethod]
        public void SpeedCalculatorDisposalIsIdempotentAndRejectsReuse()
        {
            var calculator = new SpeedCalculator();
            calculator.Dispose();
            calculator.Dispose();
            Assert.ThrowsException<ObjectDisposedException>(() => calculator.CalculateSpeed(1, 1, 1));
            var args = new SpeedCalculatorArgs();
            args.Dispose();
            args.Dispose();
            using (var live = new SpeedCalculator())
                Assert.ThrowsException<ObjectDisposedException>(() => live.Init(args));
        }
    }
}
