# Owned synthetic step transition cursor — 2026-10-23 (Asia/Hong_Kong)

M3 remains **IN_PROGRESS**. The [V4 path contract](../protocols/collision_steps.md#owned-transition-cursor)
now returns six waypoints only after all five complete capsule envelopes pass.
`SyntheticStepTransition` retains the immutable source-bound cook, path, leg and
offset. Explicit single ticks bound total 3D travel to signed path speed/120,
with speed magnitude at most 10. Each tick stays on one leg; unused corner
budget is discarded. Zero input holds phase; opposite input retraces the same
path. No rejected raw Jolt chord is accepted by this mechanism.

This is geometric diagnostic state, not an active Jolt controller. The cursor
has no elapsed-time clock or shared CLI/native input mapping. Airborne internal
poses are authorized by the complete path certificate, not ordinary standing
support. Continuous grounding, Jolt/clock integration, active V4 sessions,
measured Q5/N2, the M3 exit review and A0 remain open. Public V4 controller
creation still rejects. All 5,184 retained real observation-space cells stay
unknown; no real capture, reconstruction or asset job ran.

Default cwd: `/home/Pooh555/programming/World-Explorer`. Parent:
`101e78cacceea72c7bc079b1b10d8101e1af5f91`. Ignored evidence root
`R=artifacts/validation/m3-step-cursor-20261023` owns build/check logs and
regenerated fixtures. The seven changed C++ files are identified by
`R/source-files.sha256`, whose SHA-256 is
`6dd83dcfd22e167a388f1fc5fc1bd6923dbe3cf4a6ba727c652610e764c4763c`. CMake/Ninja use GNU C++
16.2.1, C++26 development and explicit C++23 compatibility.

The three new cases exercise ascent/descent at 0.3, 3 and 10 units/s, bounded
travel, no corner chords, exact final landing, held endpoints, pause during
lift, reversal, checkpoint/replay, stale revision/epoch and cancellation
preservation, dimension/speed rejection, legacy evidence, unknown/occupied
space and short crossing. A separate piecewise-quadratic minimum checks every
traversed capsule/step segment, including airborne points which the ordinary
motion endpoint rule rejects. Existing source support and clearance rules are
unchanged. Full controller clock/drop and application input checks remain due.

Commands (literal `R` below denotes the root above):

```sh
cmake --build --preset dev -j 4
ctest --preset dev --output-on-failure -j 4
cmake --build --preset compat-cxx23 -j 4
ctest --preset compat-cxx23 --output-on-failure -j 4
cmake --build --preset asan --target we_core_tests we_collision_fixture -j 4
build/dev/we_core_tests '[collision-step],[collision-ramp],[collision-support],[collision-cook],[collision-wall],[jolt-shape],[kinematic-capsule],[capsule-session]' --reporter compact
ASAN_OPTIONS=detect_leaks=1 build/asan/we_core_tests '[collision-step],[collision-ramp],[collision-support],[collision-cook],[collision-wall],[jolt-shape],[kinematic-capsule],[capsule-session]' --reporter compact
```

All final commands exit 0. Development CTest passes **483/483**, explicit
C++23 **458/458**; focused native and ASan/UBSan/LeakSanitizer runs pass
**48 cases / 22,409 assertions**, with no reported sanitizer errors. Final
builds and full tests use `*-certified.log` (development build:
`build-dev-certified.log`). Sanitizer build/discovery and tests run outside the
sandbox's ptrace restriction. The final seven-file style gate also exits 0
(`cpp-style-certified.log`); docs validate **1,146 local targets / 228 files**,
include boundaries and `git diff --check` pass. The explicit optional guard
only changes test access; production sources retain the first successful
focused run's behavior.

The eight fixture commands use `build/dev/we_collision_fixture R/fixture-NAME`
with default / `--walls` / `--ramp` / `--step` / `--step-clearance` /
`--capsule` / `--step-proposals` / `--step-motion`. Recursive relative-path and
byte comparisons against the preceding report's `legacy-*` and `fixture-typed`
outputs retain all **7/8/11/9/9/18/17/17** files (`R/legacy-checks.log`). The motion
trace SHA-256 remains
`69e93f41f3cd16c3c833d42afad0aa18c9bff0eac254308a6a5f5620f6025c3a`,
including its 781 clear / 17 unsupported proposals. No fixture/schema bytes change.

```sh
timeout 45s build/dev/we_viewer --scene R/fixture-motion/motion.wexplore --source-view --frames 20 --screenshot R/motion.ppm
```

This exits 0 with native device access: NVIDIA GeForce RTX 4060 Laptop GPU,
3,198 mesh triangles, 3,204 vertices, zero splats and **zero Vulkan validation
errors** (`R/native-view.log`). This is a regression view of the preserved
proposal diagnostic, not a native run of the new cursor or active step input.
FFmpeg conversion and inspection of `R/motion.png` confirm the source riser,
proposal markers, synthetic scene label and `Walk unavailable`.

```sh
PATH="$PWD/artifacts/tools/llvm-23.1.1/usr/bin:$PATH" python3 tools/check_cpp_style.py include/world_explorer/scene_store/collision_support.hpp include/world_explorer/collision/triangle_cook.hpp include/world_explorer/collision/kinematic_capsule.hpp src/scene_store/collision_support.cpp src/collision/triangle_cook.cpp src/collision/kinematic_capsule.cpp tests/unit/collision_step_tests.cpp
python3 tools/check_docs.py
git diff --check
```

The initial red build (`R/build-before.log`) lacks the new cursor API and also
exposes a test helper's nonexistent `length` spelling; the final test uses dot
product/square root. Static analysis rejected optional access behind a Catch
assertion; an explicit optional guard fixes it without weakening warnings.
`R/cpp-style-initial.log`, `cpp-style-final.log` and `tidy-optional.log` retain
those failed iterations. Sandbox sanitizer discovery hit the known LeakSanitizer
ptrace restriction (`R/build-asan.log`); native discovery and execution are used
for final checks. No sanitizer option is disabled. Available format/tidy report
**23.1.3**, so the required exact 23.1.1 run remains unverified.
