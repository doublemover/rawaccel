using Newtonsoft.Json;
using System;
using System.IO;
using System.Windows.Forms;
using System.Threading.Tasks;
using System.Text;
using System.Drawing;
using grapher.Models.Devices;
using System.Collections.Generic;
using System.Dynamic;
using System.Linq;
using grapher.Models.Theming;
using grapher.Common;

namespace grapher.Models.Serialized
{
    public class SettingsManager
    {
        #region Constructors

        public SettingsManager(
            Field dpiField,
            Field pollRateField,
            ToolStripMenuItem autoWrite,
            ToolStripMenuItem showLastMouseMove,
            ToolStripMenuItem showVelocityAndGain,
            ToolStripMenuItem themeMenu,
            Form form
            )
        {
            DpiField = dpiField;
            PollRateField = pollRateField;
            AutoWriteMenuItem = autoWrite;
            ShowLastMouseMoveMenuItem = showLastMouseMove;
            ShowVelocityAndGainMoveMenuItem = showVelocityAndGain;
            ThemeMenu = themeMenu;
            Form = form;

            SystemDevices = new List<MultiHandleDevice>();
            ActiveNormTaggedHandles = new List<(IntPtr, bool)>();

            GuiSettings = GUISettings.MaybeLoad();

            SelectedTheme = GetSelectedTheme();
            AddEventsToThemeMenu();

            if (GuiSettings is null)
            {
                GuiSettings = MakeGUISettingsFromFields();
                GuiSettings.Save();
            }
            else
            {
                UpdateFieldsFromGUISettings();
            }

            UserConfigField = InitActiveAndGetUserConfig();
            SelectedProfileName = UserConfigField.profiles[0].name;
            RebuildGraphingAccel();
        }

        #endregion Constructors

        #region Fields

        private EventHandler DeviceChangeField;

        private DriverConfig ActiveConfigField;
        private DriverConfig UserConfigField;

        private ManagedAccel ActiveAccelForGraphingField;

        #endregion Fields

        #region Properties

        public GUISettings GuiSettings { get; private set; }

        public event EventHandler DeviceChange
        {
            add => DeviceChangeField += value;
            remove => DeviceChangeField -= value;
        }

        public DriverConfig ActiveConfig 
        {
            get => ActiveConfigField;

            private set
            {
                if (ActiveConfigField != value)
                {
                    ActiveConfigField = value;
                    ActiveProfileNamesSet = new HashSet<string>(value.profiles.Select(p => p.name));
                }

                RebuildGraphingAccel();
            }
        }

        public Profile ActiveProfile 
        {
            get => ActiveConfigField.profiles.Find(p => p.name == SelectedProfileName)
                ?? UserConfigField?.profiles.Find(p => p.name == SelectedProfileName)
                ?? ActiveConfigField.profiles[0];
        }

        public ManagedAccel ActiveAccel
        {
            get
            {
                int index = ActiveConfig.profiles.FindIndex(p => p.name == SelectedProfileName);
                if (index >= 0) return ActiveConfig.accels[index];
                index = UserConfigField?.profiles.FindIndex(p => p.name == SelectedProfileName) ?? -1;
                return index >= 0 ? UserConfig.accels[index] : ActiveConfig.accels[0];
            }
        }

        /// <summary>
        /// Version of active accel modified to be appropriate for graphing (stateless)
        /// </summary>
        public ManagedAccel ActiveAccelForGraphing
        {
            get => ActiveAccelForGraphingField;
        }

        public DriverConfig UserConfig 
        { 
            get => UserConfigField;
            private set => UserConfigField = value;
        }

        public Profile UserProfile 
        {
            get => UserConfigField.profiles.Find(p => p.name == SelectedProfileName) ?? UserConfigField.profiles[0];
            private set => UserConfigField.SetProfileAt(UserConfigField.profiles.IndexOf(UserProfile), value);
        }

        public string SelectedProfileName { get; private set; }

        public bool IsApplying { get; private set; }

        private readonly Dictionary<IntPtr, (DeviceConfig Config, string Profile, string[] Bindings)> InputConfigs =
            new Dictionary<IntPtr, (DeviceConfig, string, string[])>();

        public HashSet<string> ActiveProfileNamesSet { get; private set; }

        public Field DpiField { get; private set; }

        public Field PollRateField { get; private set; }

        public IList<MultiHandleDevice> SystemDevices { get; private set; }

        public List<(IntPtr, bool)> ActiveNormTaggedHandles { get; }

        private ToolStripMenuItem AutoWriteMenuItem { get; set; }

        private ToolStripMenuItem ShowLastMouseMoveMenuItem { get; set; }

        private ToolStripMenuItem ShowVelocityAndGainMoveMenuItem { get; set; }
        private ToolStripMenuItem ThemeMenu { get; set; }
        private string SelectedTheme { get; set; }
        private Form Form { get; set; }

        #endregion Properties

        #region Methods

        private static DriverConfig CloneConfig(DriverConfig source)
        {
            var converted = DriverConfig.Convert(source.ToJSON());
            if (converted.Item2 != null) throw new ApplicationException(converted.Item2);
            return converted.Item1;
        }

        private void RebuildGraphingAccel()
        {
            ActiveAccelForGraphingField?.Dispose();
            ActiveAccelForGraphingField = ActiveAccel.CreateStatelessCopy();
        }

        public void SelectProfile(string name)
        {
            if (!UserConfig.profiles.Any(p => p.name == name)) throw new ArgumentException("Unknown profile", nameof(name));
            SelectedProfileName = name;
            RebuildGraphingAccel();
            SetActiveHandles();
        }

        public string SaveEditedProfile(Profile profile)
        {
            profile.name = UserProfile.name;
            var errors = DriverConfig.FromProfile(profile).Errors();
            if (errors == null) UserProfile = profile;
            return errors;
        }

        public void AddProfile(string name, bool duplicate)
        {
            if (UserConfig.profiles.Count >= 1024) throw new ApplicationException("At most 1024 profiles are supported.");
            if (string.IsNullOrWhiteSpace(name) || name.Length >= 256 || name.IndexOf('\0') >= 0 ||
                UserConfig.profiles.Any(p => p.name == name))
                throw new ApplicationException("Choose a unique profile name of 1 to 255 characters.");
            var profile = duplicate ? CloneConfig(DriverConfig.FromProfile(UserProfile)).profiles[0] : new Profile();
            profile.name = name;
            UserConfig.profiles.Add(profile);
            UserConfig.accels.Add(new ManagedAccel(profile));
            SelectProfile(name);
        }

        public string DeleteSelectedProfile()
        {
            string name = UserProfile.name;
            if (UserConfig.profiles[0].name == name) return "The first (default) profile cannot be deleted.";
            if (UserConfig.defaultDeviceConfig.ButtonProfiles.Contains(name) ||
                UserConfig.devices.Any(d => d.profile == name || d.config.ButtonProfiles.Contains(name)))
                return "This profile is assigned to a device or mouse button. Change those assignments first.";
            int index = UserConfig.profiles.IndexOf(UserProfile);
            UserConfig.accels[index].Dispose();
            UserConfig.accels.RemoveAt(index);
            UserConfig.profiles.RemoveAt(index);
            SelectProfile(UserConfig.profiles[0].name);
            return null;
        }

        public async Task<string> ResetDriverAsync()
        {
            if (IsApplying) return "A driver update is already in progress.";
            IsApplying = true;
            try
            {
                await Task.Run(() => DriverConfig.Deactivate());
                ActiveConfig = DriverConfig.GetDefault();
                SelectedProfileName = ActiveConfig.profiles[0].name;
                RebuildGraphingAccel();
                SetActiveHandles();
                return null;
            }
            catch (Exception e) { return e.Message; }
            finally { IsApplying = false; }
        }

        public void UpdateFieldsFromGUISettings()
        {
            DpiField.SetToEntered(GuiSettings.DPI);
            PollRateField.SetToEntered(GuiSettings.PollRate);
            ShowLastMouseMoveMenuItem.Checked = GuiSettings.ShowLastMouseMove;
            ShowVelocityAndGainMoveMenuItem.Checked = GuiSettings.ShowVelocityAndGain;
            AutoWriteMenuItem.Checked = GuiSettings.AutoWriteToDriverOnStartup;
        }

        public async Task<(bool Applied, string Error)> ApplyProfileAsync(Profile profile)
        {
            if (IsApplying) return (false, "A driver update is already in progress.");
            IsApplying = true;
            try
            {
                profile.name = UserProfile.name;
                var snapshot = CloneConfig(UserConfig);
                snapshot.SetProfileAt(snapshot.profiles.FindIndex(p => p.name == profile.name), profile);
                string errors = snapshot.Errors();
                if (errors != null) return (false, errors);
                // Capture the immutable snapshot rather than a mutable ActiveConfig field.
                await Task.Run(() => snapshot.Activate());
                return (true, CommitAppliedConfig(snapshot));
            }
            catch (Exception e) { return (false, e.Message); }
            finally { IsApplying = false; }
        }

        private string CommitAppliedConfig(DriverConfig snapshot)
        {
            UserConfig = CloneConfig(snapshot);
            ActiveConfig = snapshot;
            SetActiveHandles();
            try
            {
                GuiSettings = MakeGUISettingsFromFields();
                GuiSettings.Save();
                SaveTextAtomically(Constants.DefaultSettingsFileName, snapshot.ToJSON());
                return null;
            }
            catch (Exception e)
            {
                return "The driver was updated, but settings could not be saved: " + e.Message;
            }
        }

        internal static void SaveTextAtomically(string path, string text)
        {
            string fullPath = Path.GetFullPath(path);
            string temporary = fullPath + "." + Guid.NewGuid().ToString("N") + ".tmp";
            try
            {
                File.WriteAllText(temporary, text);
                if (File.Exists(fullPath)) File.Replace(temporary, fullPath, null);
                else File.Move(temporary, fullPath);
            }
            finally
            {
                if (File.Exists(temporary)) File.Delete(temporary);
            }
        }

        public void SetHiddenOptions(Profile settings)
        {
            settings.snap = UserProfile.snap;
            settings.maximumSpeed = UserProfile.maximumSpeed;
            settings.minimumSpeed = UserProfile.minimumSpeed;
            settings.lrOutputDPIRatio = UserProfile.lrOutputDPIRatio;
            settings.udOutputDPIRatio = UserProfile.udOutputDPIRatio;
            settings.inputSpeedArgs.inputSmoothHalflife = UserProfile.inputSpeedArgs.inputSmoothHalflife;
            settings.inputSpeedArgs.scaleSmoothHalflife = UserProfile.inputSpeedArgs.scaleSmoothHalflife;
            settings.inputSpeedArgs.outputSmoothHalflife = UserProfile.inputSpeedArgs.outputSmoothHalflife;
        }

        public GUISettings MakeGUISettingsFromFields()
        {
            return new GUISettings
            {
                DPI = (int)DpiField.Data,
                PollRate = (int)PollRateField.Data,
                ShowLastMouseMove = ShowLastMouseMoveMenuItem.Checked,
                ShowVelocityAndGain = ShowVelocityAndGainMoveMenuItem.Checked,
                AutoWriteToDriverOnStartup = AutoWriteMenuItem.Checked,
                CurrentColorScheme = SelectedTheme
            };
        }

        public static string LegacyDeviceId(string instanceId)
        {
            int separator = instanceId.LastIndexOf('\\');
            return separator >= 0 ? instanceId.Substring(0, separator) : instanceId;
        }

        public static DeviceSettings FindDeviceSettings(DriverConfig config, string instanceId)
        {
            return config.devices.Find(d => string.Equals(d.id, instanceId, StringComparison.OrdinalIgnoreCase))
                ?? config.devices.Find(d => string.Equals(d.id, LegacyDeviceId(instanceId), StringComparison.OrdinalIgnoreCase));
        }

        public void SetActiveHandles()
        {
            ActiveNormTaggedHandles.Clear();
            InputConfigs.Clear();
            foreach (var device in SystemDevices)
            {
                var settings = FindDeviceSettings(ActiveConfig, device.id);
                var config = settings?.config ?? ActiveConfig.defaultDeviceConfig;
                string profile = settings?.profile;
                if (string.IsNullOrEmpty(profile) || !ActiveProfileNamesSet.Contains(profile))
                    profile = ActiveConfig.profiles[0].name;
                foreach (var handle in device.handles)
                {
                    InputConfigs[handle] = (config, profile, config.ButtonProfiles);
                    if (!config.disable && profile == ActiveProfile.name)
                        ActiveNormTaggedHandles.Add((handle, config.dpi > 0));
                }
            }
        }

        public bool MatchesSelectedInputProfile(IntPtr handle, int heldButtons, out bool normalized)
        {
            normalized = false;
            if (!InputConfigs.TryGetValue(handle, out var input) || input.Config.disable) return false;
            normalized = input.Config.dpi > 0;
            var bindings = input.Bindings;
            for (int i = bindings.Length - 1; i >= 0; --i)
            {
                if ((heldButtons & (1 << i)) != 0 && !string.IsNullOrEmpty(bindings[i]))
                    return bindings[i] == ActiveProfile.name && ActiveProfileNamesSet.Contains(bindings[i]);
            }
            return (!input.Config.accelOnlyWhileMouse1 || (heldButtons & 1) != 0) &&
                input.Profile == ActiveProfile.name;
        }

        public void Submit(DeviceConfig newDefaultConfig, DeviceDialogItem[] items)
        {
            UserConfig.defaultDeviceConfig = newDefaultConfig;
            foreach (var item in items)
            {
                if (item.overrideDefaultConfig)
                {
                    if (item.oldSettings is null || !string.Equals(item.oldSettings.id, item.device.id, StringComparison.OrdinalIgnoreCase))
                    {
                        UserConfig.devices.Add(
                            new DeviceSettings
                            {
                                name = item.device.name,
                                profile = item.newProfile,
                                id = item.device.id,
                                config = item.newConfig
                            });
                    }
                    else
                    {
                        item.oldSettings.config = item.newConfig;
                        item.oldSettings.profile = item.newProfile;
                    }
                }
                else if (!(item.oldSettings is null) && string.Equals(item.oldSettings.id, item.device.id, StringComparison.OrdinalIgnoreCase))
                {
                    UserConfig.devices.Remove(item.oldSettings);
                }
            }
        }

        public void OnProfileSelectionChange()
        {
            SetActiveHandles();
        }

        public void OnDeviceChangeMessage()
        {
            SystemDevices = MultiHandleDevice.GetList();
            SetActiveHandles();

            DeviceChangeField?.Invoke(this, EventArgs.Empty);
        }

        private DriverConfig InitActiveAndGetUserConfig()
        {
            var path = Constants.DefaultSettingsFileName;
            if (File.Exists(path))
            {
                try
                {
                    var (cfg, err) = DriverConfig.Convert(File.ReadAllText(path));

                    if (err == null)
                    {
                        if (GuiSettings.AutoWriteToDriverOnStartup)
                        {
                            try
                            {
                                cfg.Activate();
                                ActiveConfig = CloneConfig(cfg);
                            }
                            catch (Exception e)
                            {
                                ActiveConfig = DriverConfig.GetActive();
                                MessageBox.Show("Startup settings could not be applied: " + e.Message, "Raw Accel");
                            }
                        }
                        else
                        {
                            ActiveConfig = DriverConfig.GetActive();
                        }

                        return cfg;
                    }
                    MessageBox.Show("Saved settings are invalid and were left unchanged:\n" + err, "Raw Accel settings");
                }
                catch (JsonException e)
                {
                    MessageBox.Show("Saved settings could not be read and were left unchanged:\n" + e.Message, "Raw Accel settings");
                }
            }

            ActiveConfig = DriverConfig.GetActive();
            if (!File.Exists(path)) SaveTextAtomically(path, ActiveConfig.ToJSON());
            return CloneConfig(ActiveConfig);
        }

        private string GetSelectedTheme()
        {
            if (GuiSettings == null || string.IsNullOrEmpty(GuiSettings.CurrentColorScheme))
            {
                return "Light Theme";
            }

            return GuiSettings.CurrentColorScheme;
        }

        private void AddEventsToThemeMenu()
        {
            foreach (ToolStripMenuItem item in ThemeMenu.DropDownItems)
            {
                item.Click += (s, e) =>
                {
                    if (GuiSettings.CurrentColorScheme == item.Name)
                    {
                        return;
                    }

                    SelectedTheme = item.Text;
                    var colorScheme = ColorSchemeManager.FromName(item.Text);

                    Theme.CurrentScheme = colorScheme;
                    Theme.Apply(Form);
                    GuiSettings.CurrentColorScheme = colorScheme.Name;
                    GuiSettings.Save();

                    MessageBox.Show(@"If you switched between a light and dark theme, some colors might be off. 
If this is the case, please restart the app to apply all colors correctly."); 
                };
            }
        }

        #endregion Methods
    }
}
