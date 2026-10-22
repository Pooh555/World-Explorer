# Synthetic step controller integration — 2026-10-23 (Asia/Hong_Kong)

M3 remains **IN_PROGRESS**; step controller/session integration is
**IMPLEMENTED_UNVERIFIED** pending native input validation. CPU/shared CLI and
sanitizer checks pass. Native input is **BLOCKED** by the locked desktop, not
GPU absence. V4 controller construction is implemented for opted-in supported
spawns, but its local acceptance gate remains open. No M3 exit, A0, platform,
measured Q5/N2 or real walking gate advances.

The [implemented contract](../protocols/collision_steps.md#controller-integration)
selects an opposite landing when a tick reaches the riser margin, then retains
a complete-path cursor alongside pose and clock in speculative call state.
Signed X input advances/retraces one certified leg per tick; lateral input is
held during a crossing. Total 3D travel includes lift/drop. Zero input holds
phase; errors/cancellation preserve phase, pose, ticks and fractional clock.
Jolt position/contacts synchronize to each certified point without applying
stick-down to airborne phases. Ordinary landings still use Jolt proposals and
the original probe. No rejected raw Jolt chord is reclassified. Legacy V4,
short unsupported spawns, blocked corners and mixed inclined/step source geometry
remain rejected. General surface transitions remain unsupported.

Default cwd: `/home/Pooh555/programming/World-Explorer`. Parent:
`c6e9e527aa9471437731616518a529dc8bfa099b`. Ignored evidence root
`R=artifacts/validation/m3-step-controller-20261023`. Nine-file source receipt
`R/source-files.sha256` hashes to
`e2d523ea8880da0e857455fe0ce1c88b05191013be8acde5bc40e253ce17be9a`;
eight input pins in `R/input-files.sha256` hash to
`db8f4746c9661c6842e556b9a8579a034bdb26455739898eb77712640ece209f`.
No real capture/reconstruction/asset processing ran; all 5,184 real cells stay
unknown. Existing user files, source assets and old fixture bytes are retained.

These commands exit 0 with GNU 16.2.1/CMake/Ninja:

```sh
cmake --build --preset dev -j 4
ctest --preset dev --output-on-failure -j 4
cmake --build --preset compat-cxx23 -j 4
ctest --preset compat-cxx23 --output-on-failure -j 4
cmake --build --preset asan --target we_core_tests we_collision_fixture we_process -j 4
build/dev/we_core_tests '[collision-step],[collision-ramp],[collision-support],[collision-cook],[collision-wall],[jolt-shape],[kinematic-capsule],[capsule-session]' --reporter compact
ASAN_OPTIONS=detect_leaks=1 build/asan/we_core_tests '[collision-step],[collision-ramp],[collision-support],[collision-cook],[collision-wall],[jolt-shape],[kinematic-capsule],[capsule-session]' --reporter compact
```

C++26 development passes **486/486** CTest; explicit C++23 **461/461**.
Focused development and native ASan/UBSan/LeakSanitizer pass **51 cases /
30,451 assertions**, with no reported sanitizer errors. Logs are `R/build-*.log`,
`ctest-*.log`, `focused-tests.log` and `asan-tests.log`. Tests include 0.3/3/10
ascent/descent with independent per-tick quadratic clearance, phase across calls,
fractional boundaries, capped catch-up/drop, eight single ticks versus one
batch across a corner, pause/reversal, stale/cancel preservation, unknown/
occupied corners, short spawns, mixed-floor rejection and shared airborne
focus/dialog pauses. Frame endpoint chords are not sweep certificates.

`build/dev/we_collision_fixture R/fixture --step-session` exits 0. Ordinary
`build/dev/we_process capsule-session REQUEST SHA256 1 0 10000000 FRAMES`
replays up/down/slow/corner for **45/45/450/100** frames. End X/Y:
**4.435000896/.299999982**, **3.564999342/0**, **4.450004578/.299999982**,
**3.725001335/0**. All have 54/54/540/120 ticks and zero drops; the first three
stay clear, the corner holds `OCCUPIED`. Short-spawn admission exits 2 as expected.
`R/cli.log` and per-route outputs retain results. Sanitizer CLI stdout/stderr
match all five development results exactly, including rejection; all **15**
fixture files match sanitizer generation (`R/asan-cli.log`). Eight legacy modes
retain **7/8/11/9/9/18/17/17** files byte-for-byte (`R/legacy-checks.log`),
including the original 781-clear/17-unsupported proposal trace.

The prepared native command is:

```sh
python3 R/native_steps.py
```

Harness SHA-256:
`39a86c8866d56fbed2810e2f1802d63a934b26c34289141b0e6a960dcaca0be0`.
It targets only its own windows and restores focus/cursor. Both attempts loaded
6 triangles/12 vertices with zero Vulkan errors on the RTX 4060 Laptop GPU,
then timed out waiting for focus; ticks remained zero. Moving the pointer into
the viewer did not resolve it. `hyprlock` was confirmed running; final preflight
exits 1 immediately with an unlock instruction. `R/attempt1-*`, `attempt2-*`
and `native-preflight.log` preserve this evidence. No native step input or
screenshot is claimed. Unlock the desktop and rerun before accepting V4 sessions.

The initial red tests retain V4 rejection (`tests-red.log`). A resume assertion
requested 8,333,333 ns with zero fractional remainder and correctly scheduled
zero ticks; requesting 8,333,334 ns fixes the test without changing the clock.
One run raced executable relinking and exited 126; stable-source replay passes.
Scoped nine-file format/tidy, include boundaries, docs and whitespace checks
pass (`cpp-style-initial.log` plus final documentation receipts). Available
format/tidy are **LLVM 23.1.3**; required exact 23.1.1 remains unverified.
