# Source-bound synthetic ramps — 2026-10-21 (Asia/Hong_Kong)

M3 remains **IN_PROGRESS**. V3 admits one explicit inclined synthetic plane,
plane-relative cell prisms and source-proven floor/wall triangles. The private
Jolt capsule follows that plane, including exact 45° ascent, with conservative
full-envelope blocking. Steeper support refuses spawn. Steps and transitions
between planes remain outside this profile; measured walking stays disabled.
See the [implemented contract](../protocols/collision_ramps.md).

Repository root is the default cwd. Parent is
`440345fdf7623f4190062619abe5d5543f5b3161`. Ignored logs, fixtures, scripts,
traces and readbacks are in `artifacts/validation/m3-collision-ramp-20261021/`
(`R`). The ten-code-file receipt `R/source-files.sha256` hashes to
`1c5490b6476b2c97381bab48cf391dffc6d24756b01c40c5d9a94d7790d6a0e7`.
No real solve, fusion, bake or refit ran. All **5,184** retained real cells remain
unknown; no real scale, Q5/N2, asset or platform gate is promoted.

Actual build/check commands and results:

```sh
cmake --build --preset dev -j2
ctest --preset dev --output-on-failure -j2
cmake --build --preset compat-cxx23 -j1
ctest --preset compat-cxx23 --output-on-failure -j2
cmake --build --preset asan --target we_core_tests we_process -j2
```

All exit 0. GNU C++26 development passes **474/474** in 89.93 s; explicit
C++23 passes **449/449** in 84.85 s (`dev-ctest-final.log`, `compat-ctest.log`).
Development and native ASan/UBSan/LSan each pass **39 cases / 4,355 assertions**:

```sh
build/dev/we_core_tests '[collision-ramp],[collision-support],[collision-cook],[collision-wall],[jolt-shape],[kinematic-capsule],[capsule-session]' --reporter compact
ASAN_OPTIONS=detect_leaks=1 build/asan/we_core_tests '[collision-ramp],[collision-support],[collision-cook],[collision-wall],[jolt-shape],[kinematic-capsule],[capsule-session]' --reporter compact
```

Logs are `focused-tests-grounded.log` and `asan-tests.log`; no sanitizer errors
are reported. Coverage includes up/down/diagonal slopes, strict 45° boundaries,
idle stability, all four prism wall faces, cap clearance missed by center-only
checks, malformed/stale source pins, cancellation and wall/corner blocking.

`build/dev/we_collision_fixture R/fixture --ramp`, with separate `fixture45
--ramp-45` and `steep --ramp-steep` destinations, exits 0. Shared replay uses
`build/dev/we_process capsule-session REQUEST SHA256 1 0 10000000 FRAMES`:
100 frames for wall/unknown/unsupported, 50 for the separately pinned exact-45°
ascent. `python3 R/run_replays.py` and `python3 R/run_replays.py asan` exit 0;
all four sanitizer reports match development bytes exactly. Wall sliding ends
at `(2.745001316,1.402600325,1.699999571)`; unknown and unsupported routes stop
conservatively. Ascent ends at `(1.999999523,2.105624982,1.5)` after 60 ticks.
The steep fixture's CLI and viewer both exit 2 at unsupported spawn.

`python3 R/audit.py` exits 0: an independent plane-distance/prism oracle checks
all **350 frame envelopes / 784 cell visits**, floor coverage and explicit labels.
Default V1 and capsule V2 fixture files, plus all five prior V2 inspection files,
remain byte-identical. `receipt.json` binds inputs/readbacks; wall request SHA is
`17c368c449430190ea4e15890de1ac5bbb969de4fce8833b32aa19073564ab51`,
ascent SHA is `9aea0fc02da931e74935a31702c33f5dc8086d3e8255b347b189599e5e982346`.

Native `python3 R/run_native.py` exits 0 with **16 assertions** using targeted
compositor input. Exact viewer arguments are in `native-verified-commands.json`;
results are in `native-verified-result.json`. Wall/unknown/correction/ascent
viewers run **286/57/392/60 frames**, each with four mesh triangles/eight vertices
and zero Vulkan errors on the NVIDIA GeForce RTX 4060 Laptop GPU. Input verifies
wall sliding, unknown blocking, focus/dialog pause, terminal Stop/correction and
45° ascent. Focus/cursor are restored and only owned windows closed. Readbacks
visually confirm inclined surfaces, synthetic labeling and unavailable measured
walking. This is local product validation, without human latency qualification.

Initial steep-fixture admission exposed a one-ULP distance-declaration mismatch:
the fixture now uses the loader's three-argument `hypot`; exact admission is
unchanged. Initial wall replay exposed unsupported upward tangent motion after
horizontal blocking. A Jolt downward support sweep fixes contact without widening
the existing 0.002 tolerance. V3 grounded motion also removes idle gravity creep.
Failing logs/replays remain beside final `cli-final-*` reports. No threshold or
warning policy was weakened.

The pinned clang-format/clang-tidy 23.1.1 repository gate, documentation and
whitespace checks pass (`finish-checks.log`, `cpp-style-complete.log`). The
style invocation was:

```sh
PATH="$PWD/artifacts/tools/llvm-23.1.1/usr/bin:$PATH" \
LD_LIBRARY_PATH="$PWD/artifacts/tools/llvm-23.1.1/usr/lib" \
python3 tools/check_cpp_style.py \
 include/world_explorer/scene_store/collision_support.hpp \
 include/world_explorer/collision/triangle_cook.hpp \
 include/world_explorer/collision/kinematic_capsule.hpp \
 src/scene_store/collision_support.cpp src/collision/triangle_cook.cpp \
 src/collision/kinematic_capsule.cpp src/application/collision_inspection.cpp \
 tests/fixtures/collision_support_fixture.hpp tests/fixtures/collision_fixture.cpp \
 tests/unit/collision_ramp_tests.cpp
python3 tools/check_docs.py
git diff --check
```

Next is source-bound step/landing/clearance admission
and actual 0.30 boundary validation through this session, before the M3 local
exit review and A0. ADR 020 leaves independent real measurement and Q5/N2 open.
