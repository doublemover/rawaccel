# Managed and GUI validation

The solution's x64 Release build and MSTest run are automated by
`ci/build-windows.ps1`. C++/CLI requires the Windows Visual C++ toolchain; the
Linux portable tests do not validate managed/native ABI layout or WinForms.

`UserModeRegressionTests` exercises button-profile JSON compatibility, inline
Unicode marshal offsets, missing/duplicate/overlong names, driver size/count
limits, malformed input, LUT comparison bounds, nonmutating serialization,
power/noaccel equivalence, and disposal. Existing tests cover configuration,
acceleration, and smoothing.

Before releasing, use a disposable Windows test system with a correctly signed
1.8.0 driver and perform these checks:

1. Start with no settings file, and also with a legacy 1.7.1 settings file. Confirm
   the old Left-only option behaves unchanged when no button profiles are set.
2. In the Profile menu create and duplicate profiles, give them visibly different
   acceleration/sensitivity values, switch between them, and Apply. Restart and
   verify all values/names persist. The first/default profile cannot be deleted;
   any profile referenced by a device or button cannot be deleted until unbound.
3. In Devices select a physical mouse, enable separate settings, and assign its
   base and Left/Right/Middle/X1/X2 held profiles. Verify priority
   X2 > X1 > Middle > Right > Left, and that an unbound held button does not mask a
   bound lower-priority button. Explicit bindings override the legacy Left gate.
4. Attach two identical mice and verify distinct full instance IDs and independent
   overrides. Load an old model-level ID and verify both inherit it until a new
   per-instance override is added. Disabling the instance override restores the
   legacy model/default inheritance. USB port changes may create a different ID.
5. Unplug/replug while the Devices dialog has unsaved edits. Existing edits must
   survive; a notice asks to reopen for the refreshed device list.
6. Inject a failed driver write. The GUI must remain running, retain the last
   active settings, report the error, and re-enable Apply/Reset. Verify failed
   persistence explicitly says the driver was updated but the file was not saved.
7. Attempt closing during Apply. Closing is blocked while the write is in progress.
   Test Reset after selecting a non-default profile.
8. Launch the GUI/startup writer from a different working directory, including a
   path with spaces. Settings remain beside the application, and the writer's
   startup shortcut has a quoted absolute settings path and working directory.
9. Verify malformed settings files are reported and left unchanged on startup.
10. At high polling rates, confirm live graph points match the selected effective
    profile while holding buttons. Graph inverse lookups happen at render cadence;
    per-device movement timing and button edges still process each input packet.

Keyboard-triggered profiles are not implemented. Mouse trigger state is taken
from that mouse's hardware packets; this feature does not synthesize input or
change anti-cheat policy.
