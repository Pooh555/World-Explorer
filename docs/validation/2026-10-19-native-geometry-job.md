# Native geometry-job adoption — 2026-10-19 (Asia/Hong_Kong)

M3 remains **IN_PROGRESS**. The shared native application/CLI now adopts one
externally pinned completed Python geometry job, owns isolated conversion,
checks current poses, publishes a new native bundle and strictly replays it.
Real populated cancelled native attempts survive restart unchanged. This is
retained-cache partial geometry, without fresh depth, solver/GUI job integration,
metric scale, collision, Q5/N2 or A-track qualification. The
[contract](../protocols/fusion.md#native-terminal-adoption-and-scene-publication)
owns schemas, limits and commit semantics.

Repository root `/home/Pooh555/programming/World-Explorer` is the cwd. Parent
`125dc687a67bf4efee4af617ec2340ce0e2041a6`; ignored evidence root
`R=artifacts/validation/m3-native-geometry-20261019`. `identity.json` SHA-256
`660ab745e4f608403542fe05641a5a4220e8a2850a817ec68c507f0f65852f1e`
pins all five implementation/build files and four tested executables. Linux
7.2.9-arch1-1 x86-64, GCC 16.2.1, development C++26 and explicit CPU C++23;
format/tidy 23.1.1 with their isolated `LD_LIBRARY_PATH`. Native graphics:
RTX 4060 Laptop, 8,188 MiB, driver 615.78.08. No external worker/environment modification, CUDA extension rebuild, solve,
refit, download or upload ran.

## Commands and checks

Command arrays, cwd, exits, durations and logs live in `real-commands.json`,
`adverse-commands.json`, `final-{dev,asan}-commands.json`,
`synthetic-active-cancel.json` and `native-commands.json`. Their versioned local
drivers are `{real,adverse,final_checks,native}.py`; fresh-output drivers refuse
existing paths. The following actual build/test entry points have corresponding
`build-*.log`, `ctest-*.log` and `final-*.log` under R:

```sh
cmake --preset dev
cmake --build --preset dev -j 4
ctest --preset dev
ctest --preset dev --rerun-failed
cmake --build --preset compat-cxx23 -j 4
ctest --preset compat-cxx23
ctest --preset compat-cxx23 -R 'geometry adoption'
cmake --build --preset asan -j 4
build/dev/we_core_tests 'geometry adoption*'
build/asan/we_core_tests 'geometry adoption*,*fusion*'
python3 tools/check_docs.py
git diff --check
```

Development CTest initially passed **266/373**; 107 cases could not start when
this session mistakenly relinked executables during that run. After linking
finished, **all 107 affected cases passed** in 61.82 s. C++23 passed **350/350**
in 106.47 s. Subsequent schema/attempt bounds and oversized-request additions
pass the final **3 cases / 68 assertions** on development and C++23. Native
ASan/UBSan/LeakSanitizer passes **19 geometry/fusion cases / 428 assertions**,
plus real parallax conversion/admission/publication/replay/validation. These final
focused runs are separate from the earlier full suites; explicit `<new>`/`<span>`
include cleanup then rebuilt development and repeated the same 3/68 checks
(`build-dev-includes.log`, `final-includes-tests.log`). No final full-suite count
is inferred. Sanitizers ran with `ASAN_OPTIONS=detect_leaks=1` outside the sandbox;
initial sandbox discovery failed because LSan cannot work under ptrace.

The ordinary scoped repo gate (`python3 tools/check_cpp_style.py` with the four
changed C++ paths in `cpp-paths.txt`) still fails on **42 existing-policy
pragma-once portability errors**. Preserve committed header policy `8829bfc`;
do not suppress warnings. Initial new-code static-initialization/automatic-move
findings were corrected. Guarded-header VFS diagnostics use unchanged checks;
their success does not qualify actual working-tree header policy. Final doc,
boundary and whitespace results are recorded separately in `quality-commands.json`.

## Full real integration and rejection

Requests adopt the exact terminal jobs from the
[prior geometry-job slice](2026-10-19-geometry-job.md). The command is:

```sh
build/dev/we_process adopt-geometry "$R/temporal-request.json" \
  26b97d1b3d7439f14cb823e2b46a7e9bb1b95a79059c66b0c0b57c7ee824f061 \
  "$R/temporal-native"
build/dev/we_viewer --scene "$R/temporal-native/stages/attempt-0/scene.wexplore"
```

`real-commands.json` owns the corresponding parallax request pin/argv.
`exact-integration.json` verifies every converted voxel, mesh and metadata byte
equals the independently admitted prior `.wefusion` output, preserving all
support/weight/sigma/coverage/unknown denominators and source/pose pins.

| Complete retained partial mesh | Vertices | Triangles | Native frames | Vulkan errors |
|---|---:|---:|---:|---:|
| Temporal | 32,074 | 47,559 | 90 | 0 |
| Parallax | 61,598 | 82,248 | 90 | 0 |

Both bundles validate, strictly reopen without conversion and load through
ordinary viewer scene updates. PPM/PNG screenshots were inspected: normal-color
fragments and large unknown gaps, with unmeasured scale and walking unavailable.
Sandbox X11 was inaccessible (`sandbox-native-failure.json`); actual native runs
succeed. These are short functional checks, without performance qualification.

`cancel_probe.cpp` requests cancellation via the real application log callback
after conversion finishes: exit **130**, real **8,365,831-byte** temporal asset
retained, no terminal receipt. Resume publishes attempt 1; attempt 0's SHA
`9119213daeef47e76b1efab83c242bab29ddb333e4965eb29db7a4d0ef15f06d`
is unchanged. Replay reuses attempt 1. A separately labeled synthetic converter
writes partial bytes and waits; cancellation kills/reaps that active child,
retains its partial file and publishes no receipt. This does not claim populated
*fusion-solver* recovery, which remains distinct from native conversion recovery.

Wrong current-pose pins, terminal traversal, corrupted source NPZ and converted
assets, internally valid foreign bundles, writer contention and output symlinks
all reject without altering caller/prior output. Rehashed negative, fractional
and ≥2³² attempt indices reject before narrowing on both real fixtures.
`preservation.json` verifies **24,457 protected files**, all four original bakes
and 33 prior audit/bundle files unchanged. The pre-existing **3,982 external
header differences** remain; worker rebuilds require isolated reconciliation.
GUI solver-to-viewer ownership, fresh depth, metric alignment/collision/walking,
native platform/release evidence and full gates remain open.
