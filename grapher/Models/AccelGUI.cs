using grapher.Common;
using grapher.Models.Calculations;
using grapher.Models.Devices;
using grapher.Models.Mouse;
using grapher.Models.Options;
using grapher.Models.Serialized;
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Forms;
using System.Windows.Forms.DataVisualization.Charting;

namespace grapher
{
    public class AccelGUI
    {

        #region Constructors

        public AccelGUI(
            RawAcceleration accelForm,
            AccelCalculator accelCalculator,
            AccelCharts accelCharts,
            SettingsManager settings,
            ApplyOptions applyOptions,
            Button writeButton,
            Button resetButton,
            MouseWatcher mouseWatcher,
            ToolStripMenuItem scaleMenuItem,
            ToolStripMenuItem deviceMenuItem)
        {
            AccelForm = accelForm;
            AccelCalculator = accelCalculator;
            AccelCharts = accelCharts;
            ApplyOptions = applyOptions;
            WriteButton = writeButton;
            ResetButton = resetButton;
            ScaleMenuItem = scaleMenuItem;
            DeviceMenuItem = deviceMenuItem;
            Settings = settings;
            DefaultButtonFont = WriteButton.Font;
            SmallButtonFont = new Font(WriteButton.Font.Name, WriteButton.Font.Size * Constants.SmallButtonSizeFactor);
            MouseWatcher = mouseWatcher;

            DeviceMenuItem.Click += DeviceMenuItemClick;
            ScaleMenuItem.Click += new System.EventHandler(OnScaleMenuItemClick);
            WriteButton.Click += new System.EventHandler(OnWriteButtonClick);
            ResetButton.Click += new System.EventHandler(ResetDriverEventHandler);
            AccelForm.FormClosing += new FormClosingEventHandler(SaveGUISettingsOnClose);

            ButtonTimerInterval = Convert.ToInt32(DriverConfig.WriteDelayMs);
            ButtonTimer = new Timer();
            ButtonTimer.Tick += new System.EventHandler(OnButtonTimerTick);

            ChartRefresh = SetupChartTimer();

            RefreshUser();
            RefreshActive();
            SetupButtons();

            // TODO: The below removes an overlapping form from the anisotropy panel.
            // Figure out why and remove the overlap and below.
            ApplyOptions.Directionality.Show();
            ApplyOptions.Directionality.Hide();
        }

        #endregion Constructors

        #region Properties

        public RawAcceleration AccelForm { get; }

        public AccelCalculator AccelCalculator { get; }

        public AccelCharts AccelCharts { get; }

        public SettingsManager Settings { get; }

        public ApplyOptions ApplyOptions { get; }

        public Button WriteButton { get; }

        public Button ResetButton { get; }

        public Timer ButtonTimer { get; }

        public MouseWatcher MouseWatcher { get; }

        public ToolStripMenuItem ScaleMenuItem { get; }

        public ToolStripMenuItem DeviceMenuItem { get; }

        private Timer ChartRefresh { get; }

        private Font SmallButtonFont { get; }

        private Font DefaultButtonFont { get; }

        private bool SettingsNotDefault { get; set; }

        private bool LastToggleChecked { get; set; }

        private int ButtonTimerInterval { get; }

        #endregion Properties

        #region Methods

        private void SaveGUISettingsOnClose(Object sender, FormClosingEventArgs e)
        {
            if (Settings.IsApplying)
            {
                e.Cancel = true;
                return;
            }
            var guiSettings = Settings.MakeGUISettingsFromFields();
            if (!Settings.GuiSettings.Equals(guiSettings))
            {
                guiSettings.Save();
            }
        }

        public Profile MakeSettingsFromFields()
        {
            var settings = new Profile { name = Settings.UserProfile.name };

            settings.rotation = ApplyOptions.Rotation.Field.Data;
            settings.outputDPI = Helper.CalculatOutputDPI(ApplyOptions.Sensitivity.Field.Data);

            settings.yxOutputDPIRatio = ApplyOptions.YToXRatio.Value;
            settings.inputSpeedArgs.combineMagnitudes = ApplyOptions.IsWhole;
            ApplyOptions.SetArgsFromActiveValues(ref settings.argsX, ref settings.argsY);

            var (domWeights, lpNorm) = ApplyOptions.Directionality.GetDomainArgs();
            settings.domainXY = domWeights;
            settings.inputSpeedArgs.lpNorm = lpNorm;

            settings.rangeXY = ApplyOptions.Directionality.GetRangeXY();

            Settings.SetHiddenOptions(settings);

            return settings;
        }

        public async Task UpdateActiveSettingsFromFieldsAsync()
        {
            if (Settings.IsApplying) return;
            string error_message;

            try
            {
                ButtonDelay(WriteButton);

                var result = await Settings.ApplyProfileAsync(MakeSettingsFromFields());
                error_message = result.Error;
                if (result.Applied)
                {
                    RefreshActive();
                    RebuildProfileMenu();
                }
                if (error_message == null) return;
            }
            catch (Exception e)
            {
                error_message = e.Message;
            }

            using (var form = new MessageDialog(error_message, "Raw Accel settings"))
            {
                form.ShowDialog();
            }
        }

        public void RefreshActive()
        {
            UpdateShownActiveValues(Settings.ActiveProfile);
            UpdateGraph();
        }

        public void RefreshUser()
        {
            UpdateShownActiveValues(Settings.UserProfile);
        }

        public void UpdateGraph()
        {
            AccelCharts.Calculate(Settings.ActiveAccelForGraphing, Settings.ActiveProfile);
            AccelCharts.Bind();
        }

        public void UpdateShownActiveValues(Profile args)
        {
            AccelForm.ResetAutoScroll();
            AccelCharts.ShowActive(args);
            ApplyOptions.SetActiveValues(args);
        }

        private Timer SetupChartTimer()
        {
            Timer chartTimer = new Timer();
            chartTimer.Enabled = true;
            chartTimer.Interval = 10;
            chartTimer.Tick += new System.EventHandler(OnChartTimerTick);
            return chartTimer;
        }

        private void SetupButtons()
        {
            WriteButton.Top = Constants.SensitivityChartAloneHeight - Constants.ButtonVerticalOffset;

            ResetButton.TextAlign = ContentAlignment.MiddleCenter;
            ResetButton.Size = WriteButton.Size;
            ResetButton.Top = WriteButton.Top;

            SetButtonDefaults();
        }

        private void SetButtonDefaults()
        {
            ResetButton.Font = DefaultButtonFont;
            ResetButton.Text = Constants.ResetButtonText;
            ResetButton.Enabled = true;
            ResetButton.Update();

            WriteButton.Font = DefaultButtonFont;
            WriteButton.Text = Constants.WriteButtonDefaultText;
            WriteButton.Enabled = true;
            WriteButton.Update();
        }

        private void OnScaleMenuItemClick(object sender, EventArgs e)
        {
            AccelCalculator.DPI.NewInputToData();
            AccelCalculator.PollRate.NewInputToData();
            UpdateGraph();
        }

        private async void OnWriteButtonClick(object sender, EventArgs e)
        {
            await UpdateActiveSettingsFromFieldsAsync();
        }

        private async void ResetDriverEventHandler(object sender, EventArgs e)
        {
            if (Settings.IsApplying) return;
            ButtonDelay(ResetButton);
            string error = await Settings.ResetDriverAsync();
            if (error != null) MessageBox.Show(error, "Could not reset Raw Accel");
            else
            {
                RefreshActive();
                RebuildProfileMenu();
            }
        }

        private void OnButtonTimerTick(object sender, EventArgs e)
        {
            if (Settings.IsApplying) return;
            ButtonTimer.Stop();
            SetButtonDefaults();
            DeviceMenuItem.Enabled = true;
            if (ProfileMenu != null) ProfileMenu.Enabled = true;
        }

        private void StartButtonTimer()
        {
            DeviceMenuItem.Enabled = false;
            if (ProfileMenu != null) ProfileMenu.Enabled = false;
            ButtonTimer.Interval = ButtonTimerInterval;
            ButtonTimer.Start();
        }

        private void ButtonDelay(ButtonBase btn)
        {
            ResetButton.Enabled = false;
            WriteButton.Enabled = false;

            btn.Font = SmallButtonFont;
            btn.Text = $"{Constants.ButtonDelayText} : {ButtonTimerInterval} ms";

            ResetButton.Update();
            WriteButton.Update();

            StartButtonTimer();
        }

        private void OnChartTimerTick(object sender, EventArgs e)
        {
            MouseWatcher.UpdateLastMove();
            AccelCharts.DrawLastMovement();
        }

        private async void DeviceMenuItemClick(object sender, EventArgs e)
        {
            using (var devMenu = new DeviceMenuForm(Settings))
            {
                if (devMenu.ShowDialog() == DialogResult.OK)
                {
                    Settings.Submit(devMenu.defaultConfig, devMenu.Items);
                    await UpdateActiveSettingsFromFieldsAsync();
                }
            }
        }

        private ToolStripMenuItem ProfileMenu;

        public void AddProfileMenu(MenuStrip menu)
        {
            ProfileMenu = new ToolStripMenuItem("Profiles");
            menu.Items.Add(ProfileMenu);
            RebuildProfileMenu();
        }

        private bool SaveProfileDraft()
        {
            string error = Settings.SaveEditedProfile(MakeSettingsFromFields());
            if (error == null) return true;
            MessageBox.Show(error, "Fix this profile before switching");
            return false;
        }

        private void RebuildProfileMenu()
        {
            if (ProfileMenu == null) return;
            ProfileMenu.DropDownItems.Clear();
            ProfileMenu.Text = "Profile: " + Settings.UserProfile.name;
            foreach (var profile in Settings.UserConfig.profiles)
            {
                string name = profile.name;
                var item = new ToolStripMenuItem(name) { Checked = name == Settings.UserProfile.name };
                item.Click += (s, e) =>
                {
                    if (!SaveProfileDraft()) return;
                    Settings.SelectProfile(name);
                    RefreshUser();
                    UpdateGraph();
                    RebuildProfileMenu();
                };
                ProfileMenu.DropDownItems.Add(item);
            }
            ProfileMenu.DropDownItems.Add(new ToolStripSeparator());
            ProfileMenu.DropDownItems.Add("New profile...", null, (s, e) => CreateProfile(false));
            ProfileMenu.DropDownItems.Add("Duplicate current profile...", null, (s, e) => CreateProfile(true));
            ProfileMenu.DropDownItems.Add("Delete current profile...", null, (s, e) =>
            {
                if (MessageBox.Show("Delete '" + Settings.UserProfile.name + "'? Apply to save this change.",
                    "Delete profile", MessageBoxButtons.OKCancel) != DialogResult.OK) return;
                string error = Settings.DeleteSelectedProfile();
                if (error != null) MessageBox.Show(error, "Cannot delete profile");
                else { RefreshUser(); UpdateGraph(); RebuildProfileMenu(); }
            });
            ProfileMenu.DropDownItems.Add(new ToolStripSeparator());
            ProfileMenu.DropDownItems.Add(new ToolStripMenuItem("Apply saves all profiles and device bindings") { Enabled = false });
        }

        private void CreateProfile(bool duplicate)
        {
            if (!SaveProfileDraft()) return;
            using (var dialog = new Form
            {
                Text = duplicate ? "Duplicate profile" : "New profile",
                FormBorderStyle = FormBorderStyle.FixedDialog,
                StartPosition = FormStartPosition.CenterParent,
                MinimizeBox = false, MaximizeBox = false, ClientSize = new Size(420, 110)
            })
            {
                var label = new Label { Text = "Profile name", AutoSize = true, Left = 12, Top = 12 };
                var name = new TextBox { Left = 12, Top = 34, Width = 396, MaxLength = 255 };
                var ok = new Button { Text = "Create", DialogResult = DialogResult.OK, Left = 246, Top = 72 };
                var cancel = new Button { Text = "Cancel", DialogResult = DialogResult.Cancel, Left = 333, Top = 72 };
                dialog.Controls.AddRange(new Control[] { label, name, ok, cancel });
                dialog.AcceptButton = ok;
                dialog.CancelButton = cancel;
                if (dialog.ShowDialog(AccelForm) != DialogResult.OK) return;
                try
                {
                    Settings.AddProfile(name.Text.Trim(), duplicate);
                    RefreshUser();
                    UpdateGraph();
                    RebuildProfileMenu();
                }
                catch (ApplicationException e) { MessageBox.Show(e.Message, "Cannot create profile"); }
            }
        }

        #endregion Methods
    }

}
