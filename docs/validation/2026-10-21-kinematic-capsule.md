# Synthetic kinematic capsule — 2026-10-21 (Asia/Hong_Kong)

M3 remains **IN_PROGRESS**. The private Jolt 5.3.0 character/world now provides
fixed 120 Hz flat synthetic capsule movement, an eight-tick catch-up cap, swept
wall response/sliding and separate conservative ledger blocking. Explicit
synthetic dimensions do not declare scale or enable measured walking. Source,
epoch, revision and pose changes refuse advancement; invalid input and
cancellation preserve position and fractional clock. The 45° slope and 0.30
step/stick settings are configured, but flat support rejects non-flat results:
actual stairs/slopes, application/viewer controller ownership and measured
collision remain pending. See the [controller contract](../protocols/kinematic_capsule.md).

Repository root is the default cwd; parent is
`d72162eb7b363b27b7bec0f366880ab0c2c68eec`. This report describes the controller
implementation slice on that parent. The eight-file code receipt
`R/source-files.sha256` hashes to
`e49d3ea80ef45fa6926b67716e8302d4b209b75164a39b5f6eba5092931dd09e`.
Raw commands/results, initial failures,
synthetic outputs and captures are ignored under
`artifacts/validation/m3-capsule-20261021/` (`R`). No real reconstruction, fusion,
bake or refit was rerun; all 5,184 retained real cells remain unknown.

`cmake --build --preset dev -j2` and
`ctest --preset dev --output-on-failure -j2` exit 0 on GNU 16.2.1/C++26:
**460/460** checks. `build/dev/we_core_tests
'[kinematic-capsule],[collision-wall],[collision-support],[collision-cook],[jolt-shape]'
--reporter compact` exits 0: **25 cases / 1,888 assertions**. The tests cover
exact fractional timing, dropped catch-up debt, all four one-sided wall
orientations at maximum input speed, sliding, occupied corners without selected
geometry, unknown/unsupported blocking, cancelled creation/advance, stale
scene/epoch/pose, bad dimensions/time/velocity and prior shape preservation.
The seven-file pinned clang-format/clang-tidy 23.1.1 gate passes, together
with documentation links, include boundaries and whitespace checks. No Python
production code changed.

`cmake --build --preset compat-cxx23 -j2` (repeated after the fixture changes)
and `ctest --preset compat-cxx23 --output-on-failure -j2` exit 0 on GNU
16.2.1/C++23: **435/435** checks. Native `cmake --build --preset asan --target
we_core_tests we_collision_fixture -j1`, then the same focused test command
using `ASAN_OPTIONS=detect_leaks=1 build/asan/we_core_tests`, exit 0:
**25 cases / 1,888 assertions**, without reported ASan/UBSan/LSan errors.
The first sandbox sanitizer build failed during Catch discovery because
LeakSanitizer could not inspect threads; its log is retained. The native
sanitizer fixture command below also exits 0 and reproduces every output byte.

`build/dev/we_collision_fixture R/fixture-verified --capsule` exits 0. It reloads
the pinned native source bundle/support bytes, admits/cooks/builds the shape,
then simulates three 100-frame paths with explicit radius 0.25, height 1.7 and
10 ms frames. All end at tick 120: wall sliding reaches
`(2.745001316, 0, 1.699999571)`; the unknown-column route stops at
`(1.5, 0, 3.725000858)`; the unsupported seam route stops at
`(3.708335638, 0, 3.5)`. These are synthetic-coordinate diagnostics, not metres
measured in sample_1. `python3 R/audit.py` independently checks all 300 accepted frame envelopes
(622 cell visits), analytic containment in the two original floor triangles,
exact ticks and blocked endpoints. All eight original V2 source/support/request
files remain byte-identical. The default V1 fixture also retains the prior
source/support hashes recorded in the wall report. The ASan fixture output matches every current
fixture byte. `R/receipt.json` records source/support/request pins unchanged
from the [wall slice](2026-10-21-collision-wall.md), trace SHA
`50fc8a90aa620a2e2f3da0b7e2366d8117d54697825c9309b4fbd79ba1d204d6`,
path-manifest SHA
`ce4a73c12f5bc9ec5bc4bbf008c19aa9aa78b1ad28afef8e660360922bb0d51e`
and screenshot SHA
`d0b20e90a2798caef47db97f0388547c5fdcc2cc6772625b6fac7027c47264a9`. Source and collision selection are separate from
the added diagnostic path-marker mesh.

Native viewing uses `SDL_VIDEO_DRIVER=x11 build/dev/we_viewer --scene
R/fixture-verified/paths.wexplore --source-view --frames 90 --settings
R/viewer-settings.json --screenshot R/paths-verified-visible.ppm` with native
GPU/display access. It exits 0 on the RTX 4060 Laptop GPU: **1,204 triangles / 1,208 vertices**,
90 frames, zero Vulkan validation errors. Visual inspection confirms the wall,
three stopped/sliding marker paths, synthetic label and unavailable walk control.
The initial splat-only path view hid meshes under the appearance representation;
the diagnostic now emits mesh markers and a recorded inspection camera. The
first mesh markers lacked valid-geometry/source-pose metadata; corrected output
uses the ordinary validated native mesh path.

Initial focused execution exposed Jolt's minimum four contact-hash buckets and
a doubled padding offset (Jolt already applies padding internally). Exact
upstream 5.3.0 headers and a native gdb stack identified these; both were fixed
without weakening the ledger or warnings. The clock test then passes eight
successive ticks. Arbitrary-scale sample_1 still lacks independent distance/up;
ADR 020's local exit and the separate Q5/N2/A1 scale requirements stay open.
Next is application-owned, explicitly synthetic live controller diagnostics,
then non-flat support/fixtures and the dated local M3 exit review before A0.
