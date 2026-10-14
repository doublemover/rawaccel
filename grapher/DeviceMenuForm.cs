using grapher.Models.Devices;
using grapher.Models.Serialized;
using System;
using System.Drawing;
using System.Linq;
using System.Windows.Forms;

namespace grapher
{
    public class DeviceMenuForm : Form
    {
        private readonly SettingsManager Manager;
        private readonly ListBox DeviceSelect = new ListBox();
        private readonly CheckBox OverrideCheck = new CheckBox();
        private readonly CheckBox DisableCheck = new CheckBox();
        private readonly CheckBox Mouse1AccelCheck = new CheckBox();
        private readonly NumericUpDown DpiInput = new NumericUpDown();
        private readonly NumericUpDown RateInput = new NumericUpDown();
        private readonly ComboBox BaseProfile = new ComboBox();
        private readonly ComboBox[] ButtonProfiles = new ComboBox[5];
        private readonly TextBox IdText = new TextBox();
        private readonly Label InheritanceText = new Label();
        private readonly Control[] ConfigControls;
        private int selectedIndex = -1;
        private bool loading;
        private bool devicesChanged;

        public DeviceConfig defaultConfig;
        public DeviceDialogItem[] Items { get; private set; }

        private sealed class ProfileChoice
        {
            public string Name;
            public string Display;
            public override string ToString() => Display;
        }

        public DeviceMenuForm(SettingsManager manager)
        {
            Manager = manager;
            defaultConfig = manager.UserConfig.defaultDeviceConfig;
            Items = manager.SystemDevices.Select(device =>
            {
                var exact = manager.UserConfig.devices.Find(d =>
                    string.Equals(d.id, device.id, StringComparison.OrdinalIgnoreCase));
                var inherited = manager.UserConfig.devices.Find(d =>
                    string.Equals(d.id, SettingsManager.LegacyDeviceId(device.id), StringComparison.OrdinalIgnoreCase));
                return new DeviceDialogItem
                {
                    device = device,
                    oldSettings = exact,
                    inheritedSettings = inherited,
                    overrideDefaultConfig = exact != null,
                    newConfig = (exact ?? inherited)?.config ?? defaultConfig,
                    newProfile = (exact ?? inherited)?.profile ?? string.Empty
                };
            }).ToArray();

            Text = "Devices and button profiles";
            FormBorderStyle = FormBorderStyle.Sizable;
            StartPosition = FormStartPosition.CenterParent;
            MinimizeBox = false;
            MinimumSize = new Size(760, 600);
            ClientSize = new Size(860, 630);
            AutoScaleMode = AutoScaleMode.Font;

            var layout = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 3, Padding = new Padding(10) };
            layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 37));
            layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 63));
            layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            DeviceSelect.Dock = DockStyle.Fill;
            DeviceSelect.HorizontalScrollbar = true;
            DeviceSelect.IntegralHeight = false;
            DeviceSelect.Items.Add("Default settings (unconfigured mice)");
            DeviceSelect.Items.AddRange(Items);
            layout.Controls.Add(DeviceSelect, 0, 0);

            var fields = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, AutoScroll = true, Padding = new Padding(8, 0, 0, 0) };
            fields.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            fields.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            int row = 0;
            void AddWide(Control control)
            {
                fields.RowStyles.Add(new RowStyle(SizeType.AutoSize));
                control.Dock = DockStyle.Top;
                fields.Controls.Add(control, 0, row++);
                fields.SetColumnSpan(control, 2);
            }
            void AddField(string label, Control control)
            {
                fields.RowStyles.Add(new RowStyle(SizeType.AutoSize));
                fields.Controls.Add(new Label { Text = label, AutoSize = true, Anchor = AnchorStyles.Left, Margin = new Padding(3, 8, 12, 8) }, 0, row);
                control.Dock = DockStyle.Fill;
                fields.Controls.Add(control, 1, row++);
            }

            IdText.ReadOnly = true;
            IdText.Multiline = true;
            IdText.Height = 54;
            AddWide(IdText);
            OverrideCheck.Text = "Use separate settings for this mouse";
            OverrideCheck.AutoSize = true;
            AddWide(OverrideCheck);
            InheritanceText.AutoSize = true;
            InheritanceText.MaximumSize = new Size(480, 0);
            AddWide(InheritanceText);
            DisableCheck.Text = "Disable all Raw Accel processing";
            DisableCheck.AutoSize = true;
            AddWide(DisableCheck);
            Mouse1AccelCheck.Text = "Base profile only while Left is held (legacy gate)";
            Mouse1AccelCheck.AutoSize = true;
            AddWide(Mouse1AccelCheck);

            DpiInput.Maximum = int.MaxValue;
            RateInput.Maximum = int.MaxValue;
            AddField("DPI (0 = no normalization)", DpiInput);
            AddField("Polling Hz (0 = automatic)", RateInput);
            PopulateProfiles(BaseProfile, "First/default profile");
            AddField("Base profile", BaseProfile);
            string[] labels = { "Left / Mouse1", "Right / Mouse2", "Middle / Mouse3", "X1 / Mouse4", "X2 / Mouse5" };
            for (int i = 0; i < ButtonProfiles.Length; ++i)
            {
                ButtonProfiles[i] = new ComboBox();
                PopulateProfiles(ButtonProfiles[i], "No override (use base / other held button)");
                AddField(labels[i] + " held", ButtonProfiles[i]);
            }
            layout.Controls.Add(fields, 1, 0);
            ConfigControls = new Control[] { DisableCheck, Mouse1AccelCheck, DpiInput, RateInput, BaseProfile }
                .Concat(ButtonProfiles).ToArray();

            var help = new Label
            {
                Text = "Create or duplicate profiles in the main Profiles menu, then edit their acceleration values and assign them here. " +
                    "Held-button priority: X2 > X1 > Middle > Right > Left. Unassigned buttons do not block other bindings. " +
                    "A bound button overrides the legacy Left gate. Keyboard triggers are not supported. " +
                    "Instance IDs distinguish identical mice; some devices get a different ID after changing USB ports.",
                AutoSize = true, Dock = DockStyle.Fill, Padding = new Padding(0, 8, 0, 8),
                MaximumSize = new Size(1000, 0)
            };
            layout.Controls.Add(help, 0, 1);
            layout.SetColumnSpan(help, 2);
            var buttons = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill, FlowDirection = FlowDirection.RightToLeft };
            var cancel = new Button { Text = "Cancel", DialogResult = DialogResult.Cancel, AutoSize = true };
            var apply = new Button { Text = "Apply", DialogResult = DialogResult.OK, AutoSize = true };
            buttons.Controls.AddRange(new Control[] { cancel, apply });
            layout.Controls.Add(buttons, 0, 2);
            layout.SetColumnSpan(buttons, 2);
            Controls.Add(layout);
            CancelButton = cancel;
            AcceptButton = apply;
            apply.Click += (s, e) => SaveSelection();
            DeviceSelect.SelectedIndexChanged += (s, e) =>
            {
                SaveSelection();
                selectedIndex = DeviceSelect.SelectedIndex;
                LoadSelection();
            };
            OverrideCheck.CheckedChanged += (s, e) =>
            {
                if (loading || selectedIndex <= 0) return;
                var item = Items[selectedIndex - 1];
                // Both disabling and first enabling start from the effective
                // inherited values, including defaults edited in this dialog.
                item.newConfig = item.inheritedSettings?.config ?? defaultConfig;
                item.newProfile = item.inheritedSettings?.profile ?? string.Empty;
                item.overrideDefaultConfig = OverrideCheck.Checked;
                LoadSelection();
            };
            Manager.DeviceChange += OnDeviceChange;
            Disposed += (s, e) => Manager.DeviceChange -= OnDeviceChange;
            DeviceSelect.SelectedIndex = 0;
        }

        private void PopulateProfiles(ComboBox box, string emptyLabel)
        {
            box.DropDownStyle = ComboBoxStyle.DropDownList;
            box.Items.Add(new ProfileChoice { Name = string.Empty, Display = emptyLabel });
            foreach (var profile in Manager.UserConfig.profiles)
                box.Items.Add(new ProfileChoice { Name = profile.name, Display = profile.name });
            box.SelectedIndex = 0;
        }

        private static string SelectedProfile(ComboBox box) => ((ProfileChoice)box.SelectedItem)?.Name ?? string.Empty;

        private static void SelectProfile(ComboBox box, string name)
        {
            box.SelectedIndex = 0;
            for (int i = 1; i < box.Items.Count; ++i)
            {
                if (((ProfileChoice)box.Items[i]).Name == name) { box.SelectedIndex = i; break; }
            }
        }

        private void SaveSelection()
        {
            if (loading || selectedIndex < 0) return;
            bool defaults = selectedIndex == 0;
            var item = defaults ? null : Items[selectedIndex - 1];
            if (!defaults && !item.overrideDefaultConfig) return;
            var config = defaults ? defaultConfig : item.newConfig;
            config.disable = DisableCheck.Checked;
            config.accelOnlyWhileMouse1 = Mouse1AccelCheck.Checked;
            config.dpi = (int)DpiInput.Value;
            config.pollingRate = (int)RateInput.Value;
            for (int i = 0; i < ButtonProfiles.Length; ++i) config.SetButtonProfile(i, SelectedProfile(ButtonProfiles[i]));
            if (defaults) defaultConfig = config;
            else { item.newConfig = config; item.newProfile = SelectedProfile(BaseProfile); }
        }

        private void LoadSelection()
        {
            if (selectedIndex < 0) return;
            loading = true;
            try
            {
                bool defaults = selectedIndex == 0;
                var item = defaults ? null : Items[selectedIndex - 1];
                var config = defaults ? defaultConfig : item.overrideDefaultConfig ? item.newConfig : item.inheritedSettings?.config ?? defaultConfig;
                bool enabled = defaults || item.overrideDefaultConfig;
                IdText.Text = defaults ? "Default settings apply when a mouse has no device or legacy model override." : item.device.id;
                OverrideCheck.Visible = !defaults;
                OverrideCheck.Checked = !defaults && item.overrideDefaultConfig;
                InheritanceText.Text = devicesChanged ? "Device connections changed. Existing edits are preserved; reopen to refresh the list." :
                    !defaults && item.inheritedSettings != null ? "Inherits legacy model settings until a separate override is enabled." : string.Empty;
                DisableCheck.Checked = config.disable;
                Mouse1AccelCheck.Checked = config.accelOnlyWhileMouse1;
                DpiInput.Value = config.dpi;
                RateInput.Value = config.pollingRate;
                SelectProfile(BaseProfile, defaults ? string.Empty : item.newProfile);
                var profiles = config.ButtonProfiles;
                for (int i = 0; i < profiles.Length; ++i) SelectProfile(ButtonProfiles[i], profiles[i]);
                foreach (var control in ConfigControls) control.Enabled = enabled;
                BaseProfile.Enabled = !defaults && enabled;
            }
            finally { loading = false; }
        }

        private void OnDeviceChange(object sender, EventArgs e)
        {
            devicesChanged = true;
            InheritanceText.Text = "Device connections changed. Existing edits are preserved; reopen to refresh the list.";
        }
    }
}
