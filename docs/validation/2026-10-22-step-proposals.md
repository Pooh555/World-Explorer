# Bounded synthetic Jolt step proposals — 2026-10-22 (Asia/Hong_Kong)

M3 remains **IN_PROGRESS**. The [step diagnostic](../protocols/collision_steps.md)
now records bounded raw 120 Hz Jolt proposals and their straight/transition
probe results through the controller's shared private update routine. Public
V4 controller creation remains blocked. Rejected proposals exist only inside
this speculative diagnostic; no application pose or clock is committed.
Measured walking, Q5/N2, the M3 exit review and A0 remain open.

Default cwd: `/home/Pooh555/programming/World-Explorer`. Parent:
`db8d1d0af4f35cb84c3a0698d7fa3b99d610d5b5`. Logs, synthetic fixtures and
screenshots are in ignored `artifacts/validation/m3-step-motion-20261022/`
(`R`). Its six-file `source-files.sha256` receipt hashes to
`b6a2fc555873273ee38ab8c2ee48967e6accca3ca0add3c4e06434092b0dfe5f`;
`sha256sum -c R/source-files.sha256` exits 0. Builds use GNU C++ 16.2.1,
CMake/Ninja, C++26 development and explicit C++23 compatibility. No real
capture/reconstruction/asset work ran.

A local exploratory Jolt probe exposed a horizontal budget violation in its
default 0.02 minimum forward stair distance: maximum tick travel was 0.0400004
at requested 0.025 (speed 3), and 0.0200932 at requested 0.0025 (speed 0.3).
The common updater now sets that minimum to zero. The exploratory source is
`R/jolt_probe_default.cpp`, SHA-256
`f6185f02525bf9c7e8676372cdff6801808f1c3e90e8a0f1a6a5c3c887bae442`.
Actual commands, exit 0:

```sh
c++ -D_DEBUG -std=c++26 -msse2 -mfpmath=sse -pthread -isystem build/dev/_deps/jolt53-src R/jolt_probe_default.cpp build/dev/_deps/jolt53-build/libJolt.a -o R/jolt_probe_default
R/jolt_probe_default 3 > R/default-ascent.csv
R/jolt_probe_default 0.3 > R/default-slow.csv
```

The final saved-file command exits 0:

```sh
build/dev/we_collision_fixture R/fixture-final --step-proposals
```

Its 780 proposal rows retain these outcomes; distances include binary32
rounding, checked within a 1e-5 bound:

| Input X speed | Ticks | Maximum horizontal tick distance | Final X/Y | Straight unsupported rows |
|---|---:|---:|---|---:|
| 3 | 90 | 0.025000191 | 5.603069 / 0.300000012 | 13 |
| 0.3 | 600 | 0.002500153 | 3.745002 / approximately 0 | 0 |
| -3 | 90 | 0.025000095 | 3.149992 / approximately 0 | 18 |

All short per-tick transition probes remain unsupported. Ordinary probes
retain corner and conservative floor-tile rejections. Jolt's supported flag
does not override either result. Slow ascent stalls at the riser after removing
the minimum-distance jump. An initial test incorrectly expected slow ascent to
succeed; retained raw replay demonstrates the stall, which is now explicitly
tested without adding a production speed cutoff or relaxing clearance.
The first fixture publication failed because diagnostic markers had no pose
revision; setting their synthetic pose revision fixes it. `R/fixture` retains
that attempt's inputs; the successful run uses a fresh directory.

`fixture-final/step-proposals.json` SHA-256:
`a4c65789643e1b747b0a942cbc88089990d4880fb2e39474580e4961d2616058`.
The source manifest remains
`b467351d0972227c5e3ec8fd38f7f5db99dd6f8cde80ad1514714fbd40f02ca2`,
and support remains
`bc230c21a8dcd5047b2ee9ebe3dd54dec872a6671a422cc233ba7392f0d30bf7`.
The trace pins both and the canonical cook, with `proposal_only=true` and
false walking/measured flags. The additional native bundle manifest hashes to
`2732263515d0397ff285d518154ed2db5d77fde6864a7c5aae066c379939a218`.

This native command exits 0 with local device access outside the sandbox:

```sh
timeout 45s build/dev/we_viewer --scene R/fixture-final/proposals.wexplore --source-view --frames 20 --screenshot R/proposals.ppm
```

NVIDIA GeForce RTX 4060 Laptop GPU displays 3,126 mesh triangles, zero splats
and zero Vulkan validation errors (`R/native-view.log`). Visual inspection of
`R/proposals.png` confirms the source riser, proposal markers, synthetic label
and `Walk unavailable`. This is a native saved-scene check, not accepted step
movement or input qualification. Markers never enter the collision cook.

Final build/check commands exit 0; corresponding logs are in `R`:

```sh
cmake --build --preset dev -j 4
ctest --preset dev --output-on-failure -j 4
cmake --build --preset compat-cxx23 -j 4
ctest --preset compat-cxx23 --output-on-failure -j 4
cmake --build --preset asan --target we_core_tests we_collision_fixture -j 4
build/dev/we_core_tests '[collision-step],[collision-ramp],[collision-support],[collision-cook],[collision-wall],[jolt-shape],[kinematic-capsule],[capsule-session]' --reporter compact
ASAN_OPTIONS=detect_leaks=1 build/asan/we_core_tests '[collision-step],[collision-ramp],[collision-support],[collision-cook],[collision-wall],[jolt-shape],[kinematic-capsule],[capsule-session]' --reporter compact
ASAN_OPTIONS=detect_leaks=1 build/asan/we_collision_fixture R/fixture-asan --step-proposals
```

Development CTest passes **478/478**, C++23 **453/453**. Focused development
and ASan/UBSan/LSan runs pass **43 cases / 5,229 assertions**. Sanitizer build
and execution use local process access outside the known ptrace limitation.
No sanitizer errors are reported. `diff -qr R/fixture-final R/fixture-asan`
exits 0: the complete saved fixture, trace and native diagnostic are byte-identical.
Tests cover tick travel at 0.3/3/10 input speeds, retained corner rejection,
descent, source preservation/correction, cancellation, invalid ticks/vertical
input, legacy profile rejection and continued public V4 creation rejection.
Fresh default, walls, ramp, step, step-clearance and capsule fixtures retain
all **7/8/11/9/9/18** files byte-for-byte (`R/legacy-checks.log`).

The scoped gate exits 0 (`R/cpp-style.log`):

```sh
PATH="$PWD/artifacts/tools/llvm-23.1.1/usr/bin:$PATH" python3 tools/check_cpp_style.py include/world_explorer/collision/kinematic_capsule.hpp include/world_explorer/collision/triangle_cook.hpp src/collision/kinematic_capsule.cpp src/collision/triangle_cook.cpp tests/unit/collision_step_tests.cpp tests/fixtures/collision_fixture.cpp
```

Available format/tidy binaries report **LLVM 23.1.3**; exact required 23.1.1
remains unverified. Separate `python3 tools/check_docs.py` and `git diff --check`
exit 0. Next is a source-bound capsule/corner and clipped-cell sweep proof,
followed by safe slow-step continuation and shared CLI/native movement replay.
No current diagnostic closes that remaining work or a real reconstruction gate.
