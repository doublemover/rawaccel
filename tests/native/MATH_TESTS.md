# Shared acceleration math tests

Run `tests/native/run_math_tests.sh` with GCC (or set `CXX`). It builds in a temporary directory by default; set `BUILD_DIR` to keep the binaries in a chosen directory.

The three runs use normal native types, two-byte `wchar_t` to check the Windows wire sizes, and two-byte `wchar_t` plus `-ffast-math`. All runs enable undefined-behavior and floating-to-integer conversion sanitizers and stop at the first sanitizer error. Finite-value assertions use the project's bit-based predicate so fast-math cannot remove them.

Coverage includes:

- Nonfinite scalar rejection for both acceleration axes, profile modifiers and device time bounds
- Invalid enum values, unterminated names/IDs/button bindings and negative time/speed settings
- LUT length, capacity, strictly increasing positive inputs, finite/positive outputs, interpolation and extrapolation
- Narrow smooth-jump transitions that previously overflowed; comparison with the old formula in its stable range
- Zero-speed power gain, singular classic IO-cap rejection, very large synchronous lookup inputs
- Zero-vector clamping and preservation of stationary components during output smoothing
- Every acceleration mode with gain on/off and whole/separate processing at multiple polling intervals
- 40,000 exact-result comparisons against uncached EMA reference equations, including reinitialization and fixed/jittered intervals

## Microbenchmark

```
g++ -std=c++17 -O2 tests/native/smoothing_benchmark.cpp -o /tmp/rawaccel-smoothing-benchmark
/tmp/rawaccel-smoothing-benchmark
```

The benchmark isolates the linear EMA smoother and prevents cross-call coefficient hoisting. On the assistant-cloud Linux GCC build, five two-million-step runs measured approximately 47.6 ns/call before caching versus 16.3 ns/call with caching at a fixed 0.125 ms interval. Jittered intervals measured approximately 47.4 versus 47.7 ns/call. These are local microbenchmarks, not Windows kernel, end-to-end input-latency, or hardware measurements.

The cache is keyed by exact interval equality and reset by `init`; it changes no decay equations. It adds runtime smoother state, not serialized profile/configuration fields.

## Compatibility and remaining limits

Configuration validation now rejects nonfinite values, malformed LUTs, unterminated strings, negative smoothing half-lives/minimum speeds and classic IO caps exactly equal to their offset. It validates the dormant Y curve because both curves are constructed. LUT positivity matches the existing GUI parser.

The separate-axis output smoother now keeps a zero component at zero while decaying its state. Previously a stopped negative-axis motion could become positive because zero has no remembered direction.

The requested five per-button profile-name fields deliberately expand the configuration wire layout; both driver and clients must use the matching new protocol version. With two-byte `wchar_t`, the test reports `device_config=2592`, `device_settings=4016`, `io_base=2600` and `modifier_settings=5184`. Portable size checks do not replace a Windows compiler ABI/build check.

Finite but extreme settings can still overflow intermediate floating-point math. The driver must retain its checked conversion/fail-open path. This suite does not execute WDF, test signing/install flows, validate driver IRQL behavior, or measure physical mouse latency.
