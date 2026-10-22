# Synthetic capsule/corner sweep proof — 2026-10-22 (Asia/Hong_Kong)

M3 remains **IN_PROGRESS**. The additional [V4 motion probe](../protocols/collision_steps.md)
certifies a straight capsule sweep against selected step geometry using
outward-rounded interval bounds and conservative clipped-cell checks.
Endpoint contact/support is checked separately. The existing ordinary and
landing-path probes, legacy documents and public V4 controller rejection remain.
This is geometric software evidence: continuous grounding, step continuation,
active sessions, Q5/N2, the M3 exit review and A0 remain open.

Default cwd: `/home/Pooh555/programming/World-Explorer`. Parent:
`00c4dd1714acf95a169d52fae90d963524672b14`. Ignored evidence root
`R=artifacts/validation/m3-step-motion-proof-20261022` owns logs, fixtures and
screenshots. Its six-file `source-files-final.sha256` receipt hashes to
`314a72ab865c0ce1c806a8c739d72f166da34f3b0cea671c50831704d7154360`.
Builds use CMake/Ninja and GNU C++ 16.2.1: C++26 development and explicit C++23
compatibility. No real capture, reconstruction or asset job ran.

Tests use analytic safe chords at three capsule radii, a penetrating chord
whose endpoints are individually clear, exact tangency/proof refusal,
unknown/occupied and touched-boundary cells, 0.125-cell upper-floor clipping,
legacy rejection, dimensions, cancellation and stale sources. For accepted
actual Jolt proposals, a separate piecewise-quadratic minimizer checks the
minimum capsule/step distance over the whole segment. It does not use the
production interval subdivision.

Saved-file commands exit 0:

```sh
build/dev/we_collision_fixture R/fixture-final --step-motion
python3 R/verify_motion.py R/fixture-final
```

The independent verifier uses 80-digit Decimal arithmetic to minimize each
piecewise quadratic, with a 1e-65 arithmetic comparison bound. Its SHA-256 is
`9ddcc16a8e79343debb967fac5a70cd1a7e7ce35b2cc1fd4cc721d9873bc77fb`;
`R/oracle.json` records all outcomes. All certified segments clear both the
riser solid and lower floor. Raw proposal coordinates are retained; query
copies use the existing 0.002 floor-contact normalization only.

| X velocity | Rows | Clear | Unsupported | Riser-cutting chords | Newly clear versus the old box probe |
|---|---:|---:|---:|---:|---:|
| 3 | 90 | 90 | 0 | 0 | 13 |
| 0.3 | 600 | 600 | 0 | 0 | 0 |
| -3 | 90 | 77 | 13 | 0 | 5 |
| 10 | 18 | 14 | 4 | 2 | 0 |

Thus **781/798** segments are certified; **17** remain unsupported. Both
high-speed chords that intersect the riser are rejected. Other refusals
include endpoint support and whole-tile coverage. The 600 slow rows still
stall at the riser; clear stationary segments do not establish successful
ascent. An initial assertion incorrectly expected every tested speed to gain
certified corner segments (`R/step-tests.log`). The independent oracle exposed
the high-speed cuts; the final tests retain rejection and verify every accepted
raw segment. No geometry/contact threshold was relaxed.

`fixture-final/step-motion.json` SHA-256:
`69e93f41f3cd16c3c833d42afad0aa18c9bff0eac254308a6a5f5620f6025c3a`.
The source manifest remains
`b467351d0972227c5e3ec8fd38f7f5db99dd6f8cde80ad1514714fbd40f02ca2`,
and support remains
`bc230c21a8dcd5047b2ee9ebe3dd54dec872a6671a422cc233ba7392f0d30bf7`.
Trace metadata pins source/support/canonical identities, marks proposals only,
and retains false walking/measured flags. The native diagnostic manifest hashes to
`0df2b6d822b1c037c0fe9f2d08cec494eab180d4da90f66fe4e706c6291b2e69`.

This local native command exits 0 with device access outside the sandbox:

```sh
timeout 45s build/dev/we_viewer --scene R/fixture-final/motion.wexplore --source-view --frames 20 --screenshot R/motion.ppm
```

The NVIDIA GeForce RTX 4060 Laptop GPU displays 3,198 mesh triangles, 3,204
vertices, zero splats and zero Vulkan validation errors (`R/native-view.log`).
Visual inspection of `R/motion.png` confirms the selected riser, clear/rejected
proposal markers, synthetic label and `Walk unavailable`. This is saved-scene
viewing; no active step input is qualified. Markers remain outside the cook.

Final commands exit 0; corresponding build/test logs are in `R`:

```sh
cmake --build --preset dev -j 4
ctest --preset dev --output-on-failure -j 4
cmake --build --preset compat-cxx23 -j 4
ctest --preset compat-cxx23 --output-on-failure -j 4
cmake --build --preset asan --target we_core_tests we_collision_fixture -j 4
build/dev/we_core_tests '[collision-step],[collision-ramp],[collision-support],[collision-cook],[collision-wall],[jolt-shape],[kinematic-capsule],[capsule-session]' --reporter compact
ASAN_OPTIONS=detect_leaks=1 build/asan/we_core_tests '[collision-step],[collision-ramp],[collision-support],[collision-cook],[collision-wall],[jolt-shape],[kinematic-capsule],[capsule-session]' --reporter compact
ASAN_OPTIONS=detect_leaks=1 build/asan/we_collision_fixture R/fixture-asan-typed --step-motion
```

Development CTest passes **480/480**, C++23 **455/455**. Development and
ASan/UBSan/LSan focused runs pass **45 cases / 7,346 assertions**. Sanitizers
run outside the known ptrace restriction. The final named-input source uses
the `*-typed.log` build/test receipts. Fresh default/walls/ramp/step/
step-clearance/capsule/step-proposals modes retain all **7/8/11/9/9/18/17**
files byte-for-byte against their retained fixtures (`R/legacy-checks.log`).

The first style gate rejected four easily swapped helper parameters
(`R/cpp-style.log`). Named step geometry, position ranges and test-circle
inputs remove those argument-order hazards without disabling warnings. The
final scoped gate exits 0 (`R/cpp-style-final.log`):

```sh
PATH="$PWD/artifacts/tools/llvm-23.1.1/usr/bin:$PATH" python3 tools/check_cpp_style.py include/world_explorer/scene_store/collision_support.hpp include/world_explorer/collision/triangle_cook.hpp src/scene_store/collision_support.cpp src/collision/triangle_cook.cpp tests/unit/collision_step_tests.cpp tests/fixtures/collision_fixture.cpp
python3 tools/check_docs.py
git diff --check
```

Separate documentation/whitespace checks exit 0. Regeneration after the typed
input change (`R/fixture-typed`) retains every byte of `R/fixture-final`.
The final sanitizer fixture is also byte-identical:
`diff -qr R/fixture-typed R/fixture-asan-typed` exits 0, with no reported
sanitizer errors.

Available format/tidy binaries report **LLVM 23.1.3**; required exact 23.1.1
remains unverified. Next is bounded V4 transition state with safe slow ascent,
supported descent and held/alternative paths for the unsupported segments,
then the shared CLI/native session replay. No real walking gate advances.
