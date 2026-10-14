# User-mode robustness tests

Run `tests/user-mode/run_tests.sh` on a host with GCC and C++17 support. The script
builds the production I/O, installer, uninstaller, and registry-filter code with
minimal Win32 mocks, then runs AddressSanitizer and UndefinedBehaviorSanitizer.
On hosts where LeakSanitizer cannot run under the sandbox/ptrace environment, use
`ASAN_OPTIONS=detect_leaks=0 tests/user-mode/run_tests.sh`.

The I/O tests cover configuration growth, shrinkage and reset between reads;
truncated headers/payloads; bounded retries; overflow; exact write lengths;
version responses; error capture before handle cleanup; and filter parsing,
case-insensitive deduplication, order preservation, and absence/error handling.

The installer tests inject failures in staging copy, file activation, service
creation/update, registry write, and delayed cleanup. They check that the prior
binary/service are restored before commit, fresh-install failures leave no
binary/service behind, corrupt binaries are rejected, successful replacement
schedules the old unique backup, and uninstall retains a recoverable binary on
cleanup failure. Tests operate only on temporary mock filesystem fixtures.

These are logic tests, not Windows integration or ABI tests. The mock PE
structures intentionally contain only fields used by the code. They do not test
Authenticode, Windows driver loading, actual locked-file behavior, SetupAPI/SCM
permissions, or restart-time deletion. Before shipping, build x64 and ARM64 with
MSVC/Windows SDK and validate install, upgrade, rollback, and uninstall in
throwaway Windows VMs, including a running driver and third-party mouse filters.
