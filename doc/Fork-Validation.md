# Raw Accel device/button-profile development branch

## Configuration

1. Use the main window's profile selector to create or duplicate named profiles.
   Edit each profile's normal acceleration fields, then apply it.
2. Open the device settings window. Defaults apply to devices without an override.
   To configure one mouse, choose its device instance and enable its override.
   Disable acceleration by default and enable only the intended mouse when other
   mice/touchpads should remain unaffected.
3. Assign a base profile and optional held-button profiles. All five ordinary
   mouse buttons are supported: left, right, middle, X1, X2.
4. If several bound buttons are held, priority is X2 > X1 > middle > right > left.
   Unbound held buttons do not block another binding. An explicit held binding
   takes precedence over the legacy Mouse1-only gate. With no binding held,
   the base profile follows that gate. Empty bindings preserve legacy behavior.

A profile transition resets fractional carry and smoothing history. Press/release
packets are forwarded, including packets without movement. Absolute-coordinate
packets and attribute notifications are forwarded unchanged. Settings changes
still have the existing one-second activation delay; selecting among already
configured button profiles does not add that delay.

Device IDs now include their instance suffix, allowing two identical model mice
to be configured separately. Old model-level IDs remain fallback matches.
Instance IDs can change when a non-serial-number device moves to another port;
check its selected override after reconnecting or moving ports. Sleep/resume
clears held-button and timing state; a button already held through resume may
need release/re-press because the mouse must report a new transition.

Keyboard triggers are not implemented. The existing driver receives mouse-stack
packets, not keyboard packets. Adding cross-device keyboard triggers requires a
separate keyboard input/control design and compatibility review. No keyboard
hook or user-system configuration change is part of this work.

## Audit coverage and corrections

Reviewed first-party common math/validation, KMDF filter/control path, managed
interop, input enumeration, settings/graph/UI, writer, installer/uninstaller,
project configuration, and existing tests. Prebuilt signed binaries and external
libraries were not reverse-engineered. This is an evidence-driven audit, not a
claim that every defect is eliminated.

- Kernel settings are validated, including raw bool representations, finite
  numbers, bounded strings, enum ranges, lookup-table shape/order, duplicate IDs
  and names, and profile references. Derived curve data is rebuilt in kernel
  context rather than trusted from user-mode buffers.
- Configuration is prepared outside the packet spin lock. Nonpaged immutable
  profiles are published under per-device synchronization. No allocations,
  profile-name searches, or profile coefficient construction occur per packet.
- The callback sends bounded scratch batches, leaves port-owned packets intact,
  and commits state only for packets accepted by the class callback. It handles
  partial/zero consumption, reentry, and configuration replacement during delivery.
- Invalid/out-of-range results are rejected before converting to Windows LONG.
  Original motion is preserved and numerical state reset on invalid output.
- Control work is explicitly PASSIVE_LEVEL. Query allocation failure, failed
  connect rollback, device collection failures, power resets, control-device
  lifetime, and pool cleanup are handled.
- Numerical edge fixes and an exact-time smoothing coefficient cache have
  regression tests. See [math coverage and benchmark](../tests/native/MATH_TESTS.md).
- User-mode work includes error-aware asynchronous Apply, immutable write
  snapshots, profile editing/bindings, bounded enumeration, interop lifetime and
  equivalence fixes, robust IO reads/writes, and installer error/rollback handling.

The control interface retains upstream's local-user access policy. Settings are
system-wide, so another local process with access can change them. This change
bounds config payloads to 16 MiB, 1,024 profiles, and 4,096 device entries.

## Build and automated validation

The wire ABI changes to 1.8.0; older 1.7.x apps/drivers are incompatible. JSON
compatibility is separate: omitted new properties default to empty bindings.

- Linux/cloud: `bash tests/native/run_math_tests.sh`,
  `bash tests/native/run_input_tests.sh`, `bash tests/user-mode/run_tests.sh`.
- If the execution sandbox uses ptrace, LeakSanitizer cannot run. Use
  `ASAN_OPTIONS=detect_leaks=0 bash tests/user-mode/run_tests.sh` for ASan/UBSan
  coverage and report leak checking as unavailable.
- Windows: Visual Studio 2022 C++/CLI and .NET 4.7.2 tools, NuGet, and
  `powershell -File ci/build-windows.ps1`. The script restores pinned official
  Microsoft WDK/SDK 10.0.26100.6584 packages and builds without signing/installing.
- GitHub Actions runs portable sanitizer tests and the Windows build/test script.
  Check the exact PR commit's run; a workflow file alone is not a successful run.

## Required Windows validation before deployment

Compilation and portable tests do not prove kernel integration, driver latency,
or anti-cheat compatibility. On an isolated test Windows system with a recovery
input device, a qualified driver developer should validate:

- WDK x64 Debug/Release builds, compiler analysis, Driver Verifier/WDF Verifier,
  HVCI-compatible signing and package policy without disabling production security
- malformed IOCTLs and concurrent Apply/read while both mice generate input
- mouse disconnect/reconnect, two identical devices, port migration, sleep/resume,
  failed connect/device queries, and cleanup under allocation-failure injection
- partial/zero class-queue consumption, mixed absolute/relative/button packets,
  all button combinations, normal and swapped Windows primary-button settings
- GUI profile creation/duplicate/delete, reference protection, Apply failure,
  saving failure, repeated Apply, close during Apply, and startup auto-apply
- mouse motion/button/wheel behavior and measured CPU/DPC cost at 125/500/1000/
  2000/4000/8000 Hz; compare ETW/WPA traces and end-to-end latency to the baseline
- game-specific behavior and vendor policies for the custom driver

No Windows kernel load, hardware benchmark, signed release, or game acceptance
is implied by the source changes. The local smoothing microbenchmark is a CPU
calculation measurement only, not evidence of a particular input-latency gain.

## Reference contracts

- [MouseClassServiceCallback](https://learn.microsoft.com/en-us/previous-versions/ff542394(v=vs.85))
- [MOUSE_INPUT_DATA](https://learn.microsoft.com/en-us/windows/win32/api/ntddmou/ns-ntddmou-mouse_input_data)
- [Framework locks](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/using-framework-locks)
- [Control-device cleanup](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/kmdf-controldevicedeleted)
- [IoGetDevicePropertyData](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-iogetdevicepropertydata)
- [x64 floating-point state](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/using-floating-point-or-mmx-in-a-wdm-driver)
- [WDK NuGet builds](https://learn.microsoft.com/en-us/windows-hardware/drivers/install-the-wdk-using-nuget)
