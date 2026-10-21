# Selected synthetic step admission — 2026-10-21 (Asia/Hong_Kong)

M3 remains **IN_PROGRESS**. This slice admits one synthetic positive-X step as
source-bound geometry: two floor levels, a selected complete riser, paired
landing tiles and a V4 canonical triangle stream. It does not admit a capsule
transition. V4 clearance returns `UNSUPPORTED_FLOOR` even at spawn, and measured
walking remains disabled. See the [implemented contract](../protocols/collision_steps.md).

Repository root is the default cwd. Parent revision:
`9763b8223c172d14aaa4fc42935c7466aeb357a9`. The implementation is a dirty
working-tree slice relative to that parent; exact source identities
are recorded in ignored `artifacts/validation/m3-collision-step-20261021/`
(`R`). That directory also holds the synthetic fixture, diagnostic, logs and
native screenshots. No real source, depth, CUDA solve, fusion or asset job ran.
The seven-file `R/source-files.sha256` receipt hashes to
`33e5eb83c5aee740ac594f6c098a9275be87efdb0766ee90c3443ebe75bc0dca`.
Development and compatibility builds use CMake/Ninja with GNU C++ 16.2.1.
The available `clang-format` and `clang-tidy` binaries under the local
`llvm-23.1.1` directory report **LLVM 23.1.3**, rather than the required
23.1.1; record the actual tool version instead of claiming a pinned pass.

The fixture command `build/dev/we_collision_fixture R/fixture --step` exited 0.
Its source manifest SHA-256 is
`b467351d0972227c5e3ec8fd38f7f5db99dd6f8cde80ad1514714fbd40f02ca2`;
support JSON SHA-256 is
`eb36d7adc1f3a2bc08d21667b430346a684b9409a6e7822377cb295985f08e54`.
The selected floor and riser use a binary32 height immediately below 0.30
(`nextafter(0.3f, 0)`), preserving exact source-vertex comparisons. A matching
height immediately above 0.30 is rejected. The admitted cook holds four floor
and two riser triangles. Removing upper floor references, changing the boundary,
omitting a riser triangle, changing its chunk hash or declaring excess height
rejects admission. Source revision correction invalidates the canonical owner.

`build/dev/we_process inspect-collision R/fixture/request.json
9a0bb302b0994cfe597a341731aab5993d70e544754070ef5bfe3563cc9b9b47
R/inspection` exited 0. Its manifest reports 48 selected support tiles,
240 free/12 occupied/4 unknown explicitly labeled cells, and four
`UNSUPPORTED_FLOOR` probes with zero checked cells. This is a geometry display,
not a clearance result. `R/inspection/manifest.json` owns exact output hashes.

Native local viewing needed device access outside the sandbox, which otherwise
returned `No available video device`. The first device-access attempt with
`--source-view` returned exit 2 because the synthetic diagnostic has no source
bookmark. With the default camera, each of these commands exited 0:

```sh
timeout 45s build/dev/we_viewer --scene R/inspection/diagnostic.wexplore --frames 20 --screenshot R/view.ppm
timeout 45s build/dev/we_viewer --scene R/fixture/source.wexplore --frames 20 --screenshot R/source-view.ppm
```

Both report NVIDIA GeForce RTX 4060 Laptop GPU and zero validation errors. The
diagnostic draws 320 synthetic markers; the source draws six mesh triangles and
12 vertices. Visual inspection of `R/source-view.png` shows separate low/high
source surfaces, a `SYNTHETIC SCENE` label and `Walk unavailable`. This is a
local display check only, not a step path or N2 result.

GNU C++26 development build and `ctest --preset dev --output-on-failure -j 4`
exit 0, **476/476** checks; see `R/dev-build.log` and `R/dev-ctest.log`.
The final source receipt rebuild and focused replay also exit 0
(`R/dev-build-final.log`, `R/focused-tests-final.log`).
The focused command
`build/dev/we_core_tests '[collision-step],[collision-ramp],[collision-support],[collision-cook],[collision-wall],[jolt-shape],[kinematic-capsule],[capsule-session]' --reporter compact`
exits 0 with **41 cases / 4,382 assertions** (`R/focused-tests.log`).
`cmake --build --preset compat-cxx23 -j 4` and
`ctest --preset compat-cxx23 --output-on-failure -j 4` exit 0 with **451/451**
checks (`R/compat-build.log`, `R/compat-ctest.log`).
`cmake --build --preset asan --target we_core_tests -j 4` first linked the
binary but failed Catch2 discovery inside the sandbox because LeakSanitizer
cannot operate under ptrace. Re-running that same build with local device/process
access exited 0. The focused command above with
`ASAN_OPTIONS=detect_leaks=1 build/asan/we_core_tests` exits 0 with **41 cases /
4,382 assertions** and no reported sanitizer errors (`R/asan-tests.log`).
The first new focused attempt failed on a Jolt ray fraction rounding difference
of 0.00000095 and on absent upper-landing admission. The ray assertion now uses
a 1e-5 comparison bound, and V4 admission requires paired selected landing
support. The retained tool output is in this session; final passing logs above
are saved. No behavior threshold, step height or existing gate was weakened.

Fresh `we_collision_fixture` default, `--walls` and `--ramp` outputs under
`R/legacy-v1`, `R/legacy-v2` and `R/legacy-v3` compare byte-for-byte (`diff -qr`
for source bundles, `cmp` for support and request JSON) against the retained
V1/V2/V3 outputs in `artifacts/validation/m3-collision-ramp-20261021/`.

The repository style gate with the seven changed C++ files as arguments exits
0 (`R/cpp-style-scoped-final.log`): formatting, clang-tidy and whitespace checks pass
for this slice. An attempted unscoped run was interrupted after seven of 161
translation units because it was spending substantial time reanalyzing
unmodified files; `R/cpp-style.log` retains its partial output and exit 130.
This is a scoped style result, with the available LLVM 23.1.3 version noted
above. `python3 tools/check_docs.py` and `git diff --check` exit 0.

The source does not prove vertical lift, full envelope clearance through a
transition, ascent/descent, mixed slopes or corners. V4 shared capsule sessions
therefore remain blocked by design. V1–V3 behavior and the real arbitrary-scale
scene retain their prior limits. Q5/N2, M3 local exit and A0 remain open.
