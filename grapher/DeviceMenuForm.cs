using grapher.Models.Devices;
using grapher.Models.Serialized;
using System;
using System.Drawing;
using System.Windows.Forms;

namespace grapher
{
    public class DeviceMenuForm : Form
    {
        #region Constructors
        public DeviceMenuForm(SettingsManager sm)
        {
            Manager = sm;
            defaultConfig = Manager.UserConfig.defaultDeviceConfig;

            var columns = 3;
            var rows = 9;
            var tablePanel = new TableLayoutPanel
            {
                Dock = DockStyle.Fill,
                ColumnCount = columns,
                RowCount = rows
            };

            SuspendLayout();
            tablePanel.SuspendLayout();

            Label MakeConfigLabel(string text)
            {
                return new Label
                {
                    Text = text,
                    Anchor = AnchorStyles.Left,
                    AutoSize = true,
                };
            }

            DpiLabel = MakeConfigLabel("DPI:");
            RateLabel = MakeConfigLabel("Polling rate:");
            DisableLabel = MakeConfigLabel("Disable:");
            Mouse1AccelLabel = MakeConfigLabel("Mouse1-gated accel:");
            OverrideLabel = MakeConfigLabel("Override defaults:");

            var maxLabel = Mouse1AccelLabel;
            tablePanel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 2));
            tablePanel.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, maxLabel.PreferredSize.Width + maxLabel.Margin.Left + maxLabel.Margin.Right));
            tablePanel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 1));

            var middleRowCount = rows - 2;
            tablePanel.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            for (int i = 0; i < middleRowCount; i++)
            {
                tablePanel.RowStyles.Add(new RowStyle(SizeType.Percent, 1));
            }
            tablePanel.RowStyles.Add(new RowStyle(SizeType.AutoSize));

            var topPanel = new FlowLayoutPanel
            {
                AutoSize = true,
            };

            DefaultDisableCheck = new CheckBox
            {
                Text = "Disable by default",
                AutoSize = true,
                Checked = defaultConfig.disable,
            };

            DefaultMouse1AccelCheck = new CheckBox
            {
                Text = "Accel only while Mouse1 is held by default",
                AutoSize = true,
                Checked = defaultConfig.accelOnlyWhileMouse1,
            };

            topPanel.Controls.Add(DefaultDisableCheck);
            topPanel.Controls.Add(DefaultMouse1AccelCheck);

            tablePanel.Controls.Add(topPanel, 0, 0);
            tablePanel.SetColumnSpan(topPanel, columns);

            var bottomPanel = new FlowLayoutPanel
            {
                AutoSize = true,
                Anchor = AnchorStyles.Right | AnchorStyles.Bottom,
            };

            var applyButton = new Button
            {
                Text = "Apply",
                DialogResult = DialogResult.OK,
            };

            bottomPanel.Controls.AddRange(new Control[] {
                applyButton,
                new Button
                {
                    Text = "Cancel",
                    DialogResult = DialogResult.Cancel,
                },
            });

            tablePanel.Controls.Add(bottomPanel, 0, rows - 1);
            tablePanel.SetColumnSpan(bottomPanel, columns);

            IdPanel = new Panel
            {
                Dock = DockStyle.Fill,
            };

            IdText = new TextBox
            {
                ReadOnly = true,
                BorderStyle = BorderStyle.None,
                BackColor = this.BackColor,
                TabStop = false,
                TextAlign = HorizontalAlignment.Center,
                Dock = DockStyle.Fill,
            };

            IdPanel.Controls.Add(IdText);
            IdPanel.Controls.Add(new Label
            {
                // divider
                Height = 2,
                BorderStyle = BorderStyle.Fixed3D,
                AutoSize = false,
                Text = string.Empty,
                Dock = DockStyle.Bottom,
            });

            tablePanel.Controls.Add(IdPanel, 1, 1);
            tablePanel.SetColumnSpan(IdPanel, 2);

            NumericUpDown MakeNumericInput(int val = 0, int min = 0, int max = 999999)
            {
                return new NumericUpDown
                {
                    Value = val,
                    Minimum = min,
                    Maximum = max,
                    Dock = DockStyle.Fill,
                    Anchor = AnchorStyles.Left,
                    AutoSize = true,
                };
            }

            CheckBox MakeCheck()
            {
                return new CheckBox
                {
                    Text = string.Empty,
                    AutoSize = true,
                    Anchor = AnchorStyles.Left,
                };
            }

            DpiInput = MakeNumericInput();
            RateInput = MakeNumericInput();
            DisableCheck = MakeCheck();
            Mouse1AccelCheck = MakeCheck();
            OverrideCheck = MakeCheck();

            tablePanel.Controls.Add(OverrideLabel, 1, 2);
            tablePanel.Controls.Add(OverrideCheck, 2, 2);
            tablePanel.Controls.Add(DisableLabel, 1, 3);
            tablePanel.Controls.Add(DisableCheck, 2, 3);
            tablePanel.Controls.Add(Mouse1AccelLabel, 1, 4);
            tablePanel.Controls.Add(Mouse1AccelCheck, 2, 4);
            tablePanel.Controls.Add(DpiLabel, 1, 5);
            tablePanel.Controls.Add(DpiInput, 2, 5);
            tablePanel.Controls.Add(RateLabel, 1, 6);
            tablePanel.Controls.Add(RateInput, 2, 6);

            DeviceSelect = new ListBox
            {
                Dock = DockStyle.Fill,
                IntegralHeight = false,
                HorizontalScrollbar = true
            };

            tablePanel.Controls.Add(DeviceSelect, 0, 1);
            tablePanel.SetRowSpan(DeviceSelect, middleRowCount);

            ResetDataAndSelection();
            SetEnabled(false);
            SetVisible(false);

            applyButton.Click += ApplyButton_Click;
            OverrideCheck.Click += OverrideCheck_Click;
            OverrideCheck.CheckedChanged += OverrideCheck_Checked;
            DefaultDisableCheck.CheckedChanged += DefaultDisableCheck_Checked;
            DefaultMouse1AccelCheck.CheckedChanged += DefaultMouse1AccelCheck_Checked;
            IdText.DoubleClick += SelectAllText;
            DeviceSelect.SelectedIndexChanged += DeviceSelect_SelectedIndexChanged;
            Manager.DeviceChange += OnDeviceChange;
            Disposed += OnDispose;

            var toolTip = new ToolTip();
            toolTip.SetToolTip(IdText, "Device ID");

            var rateTip = "Keep at 0 for automatic adjustment";
            toolTip.SetToolTip(RateInput, rateTip);
            toolTip.SetToolTip(RateLabel, rateTip);

            var dpiTip = "Normalizes sensitivity and input speed to 1000 DPI";
            toolTip.SetToolTip(DpiInput, dpiTip);
            toolTip.SetToolTip(DpiLabel, dpiTip);


            Name = "DeviceMenuForm";
            Text = "Devices";
            MaximizeBox = false;
            MinimizeBox = false;
            FormBorderStyle = FormBorderStyle.FixedDialog;
            StartPosition = FormStartPosition.CenterParent;
            AutoScaleDimensions = new SizeF(6F, 13F);
            AutoScaleMode = AutoScaleMode.Font;
            ClientSize = new Size(500, 300);

            Controls.Add(tablePanel);

            tablePanel.ResumeLayout(false);
            ResumeLayout(false);
        }

        #endregion Constructors

        #region Fields

        public DeviceConfig defaultConfig;

        #endregion Fields

        #region Properties

        public DeviceDialogItem[] Items { get; private set; }

        private DeviceDialogItem Selected
        {
            get => (DeviceDialogItem)DeviceSelect.SelectedItem;
        }

        private bool AnySelected
        {
            get => DeviceSelect.SelectedIndex != -1;
        }

        private int LastSelectedIndex { get; set; }

        private SettingsManager Manager { get; }

        private ListBox DeviceSelect { get; }

        private CheckBox DefaultDisableCheck { get; }
        private CheckBox DefaultMouse1AccelCheck { get; }

        private TextBox IdText { get; }
        private Panel IdPanel { get; }

        private Label OverrideLabel { get; }
        private CheckBox OverrideCheck { get; }

        private Label DisableLabel { get; }
        private CheckBox DisableCheck { get; }

        private Label Mouse1AccelLabel { get; }
        private CheckBox Mouse1AccelCheck { get; }

        private Label DpiLabel { get; }
        private NumericUpDown DpiInput { get; }

        private Label RateLabel { get; }
        private NumericUpDown RateInput { get; }

        #endregion Properties

        #region Methods

        private void ResetDataAndSelection()
        {
            var count = Manager.SystemDevices.Count;
            Items = new DeviceDialogItem[count];

            for (int i = 0; i < count; i++)
            {
                var sysDev = Manager.SystemDevices[i];
                var settings = Manager.UserConfig.devices.Find(s => s.id == sysDev.id);
                bool found = !(settings is null);

                Items[i] = new DeviceDialogItem
                {
                    device = sysDev,
                    overrideDefaultConfig = found,
                    oldSettings = settings,
                    newConfig = found ?
                        settings.config :
                        Manager.UserConfig.defaultDeviceConfig,
                    newProfile = found ?
                        settings.profile :
                        Manager.UserConfig.profiles[0].name
                };
            }

            LastSelectedIndex = -1;
            DeviceSelect.ClearSelected();
            DeviceSelect.Items.Clear();
            DeviceSelect.Items.AddRange(Items);
        }

        private void SetVisible(bool visible)
        {
            IdPanel.Visible = visible;
            OverrideLabel.Visible = visible;
            OverrideCheck.Visible = visible;
            DisableLabel.Visible = visible;
            DisableCheck.Visible = visible;
            Mouse1AccelLabel.Visible = visible;
            Mouse1AccelCheck.Visible = visible;
            DpiInput.Visible = visible;
            DpiLabel.Visible = visible;
            RateInput.Visible = visible;
            RateLabel.Visible = visible;
        }

        private void SetEnabled(bool enable)
        {
            DisableLabel.Enabled = enable;
            DisableCheck.Enabled = enable;
            Mouse1AccelLabel.Enabled = enable;
            Mouse1AccelCheck.Enabled = enable;
            DpiInput.Enabled = enable;
            DpiLabel.Enabled = enable;
            RateInput.Enabled = enable;
            RateLabel.Enabled = enable;
        }

        private void SetInputsFromNewSelection()
        {
            IdText.Text = Selected.device.id;
            OverrideCheck.Checked = Selected.overrideDefaultConfig;

            SetOverrideDependentInputs();
        }

        private void SetOverrideDependentInputs()
        {
            var item = Selected;
            bool oride = item.overrideDefaultConfig;
            DisableCheck.Checked = oride ? item.newConfig.disable : defaultConfig.disable;
            Mouse1AccelCheck.Checked = oride ? item.newConfig.accelOnlyWhileMouse1 : defaultConfig.accelOnlyWhileMouse1;
            DpiInput.Value = oride ? item.newConfig.dpi : defaultConfig.dpi;
            RateInput.Value = oride ? item.newConfig.pollingRate : defaultConfig.pollingRate;
        }

        private void UpdateLastSelected()
        {
            var item = Items[LastSelectedIndex];
            bool oride = OverrideCheck.Checked;
            item.overrideDefaultConfig = oride;
            item.newConfig.disable = oride ? DisableCheck.Checked : defaultConfig.disable;
            item.newConfig.accelOnlyWhileMouse1 = oride ? Mouse1AccelCheck.Checked : defaultConfig.accelOnlyWhileMouse1;
            item.newConfig.dpi = oride ? (int)DpiInput.Value : defaultConfig.dpi;
            item.newConfig.pollingRate = oride ? (int)RateInput.Value : defaultConfig.pollingRate;
        }

        private void ApplyButton_Click(object sender, EventArgs e)
        {
            if (AnySelected) UpdateLastSelected();
        }

        private void OverrideCheck_Checked(object sender, EventArgs e)
        {
            SetEnabled(OverrideCheck.Checked);
        }

        private void OverrideCheck_Click(object sender, EventArgs e)
        {
            UpdateLastSelected();
            SetOverrideDependentInputs();
        }

        private void DefaultDisableCheck_Checked(object sender, EventArgs e)
        {
            defaultConfig.disable = DefaultDisableCheck.Checked;

            if (AnySelected && !Selected.overrideDefaultConfig)
            {
                DisableCheck.Checked = DefaultDisableCheck.Checked;
            }
        }

        private void DefaultMouse1AccelCheck_Checked(object sender, EventArgs e)
        {
            defaultConfig.accelOnlyWhileMouse1 = DefaultMouse1AccelCheck.Checked;

            if (AnySelected && !Selected.overrideDefaultConfig)
            {
                Mouse1AccelCheck.Checked = DefaultMouse1AccelCheck.Checked;
            }
        }

        private void DeviceSelect_SelectedIndexChanged(object sender, EventArgs e)
        {
            if (AnySelected)
            {
                if (LastSelectedIndex != -1)
                {
                    UpdateLastSelected();
                }

                SetInputsFromNewSelection();
            }

            LastSelectedIndex = DeviceSelect.SelectedIndex;

            SetVisible(AnySelected);
        }

        private void OnDeviceChange(object sender, EventArgs e)
        {
            ResetDataAndSelection();
        }

        private void OnDispose(object sender, EventArgs e)
        {
            Manager.DeviceChange -= OnDeviceChange;
        }

        private static void SelectAllText(object sender, EventArgs e)
        {
            ((TextBoxBase)sender).SelectAll();
        }

        #endregion Methods
    }
}
