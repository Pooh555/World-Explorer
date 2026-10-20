# Canonical synthetic collision cook — 2026-10-21 (Asia/Hong_Kong)

M3 remains **IN_PROGRESS**. The new `collision` target owns an immutable,
portable, source-bound [triangle cook](../protocols/collision_cook.md) of the
floor triangles explicitly selected by synthetic support. The cook has no Jolt
dependency, response/controller, real free-space inference or walking
permission. This is a local software-contract slice, not Q5/N2, A1 or platform
qualification. Repository root is the default cwd. Parent commit:
`b059abfee1abdc2114be2f0b6d0cec28d1976163`; the report describes this
reviewable implementation diff. Ignored evidence directory (`R`) is
`artifacts/validation/m3-collision-cook-20261021/`.

## Contract and fixture results

The source/support admission path now retains exact floor references; a later
cook rechecks chunk generation, pose revision, content hash and triangle index,
then transforms world vertices and serializes canonical little-endian bytes.
Scene/epoch/revision/content changes, cancellation, nonfinite or degenerate
geometry fail before returning an owner. The complete support document hash is
part of the digest, so changed occupied/unknown cells invalidate even when
floor geometry is identical. A conservative diagnostic probe remains attached
to the owner; it is not a physics simulation. The boundary gate confines any
future Jolt headers to `collision`.

`build/dev/we_core_tests '[collision-cook]' --reporter compact` exits 0:
**3 cases / 54 assertions** cover deterministic bytes, wide 64-bit IDs,
triangle winding/order, support changes, blocked occupied/unknown envelopes,
source correction and cancellation. Focused native `ASAN_OPTIONS=detect_leaks=1
build/asan/we_core_tests '[collision-cook],[collision-support]' --reporter
compact` exits 0: **13 cases / 852 assertions**, without reported
ASan/UBSan/LSan errors. The initial sandboxed ASan post-link discovery could
not run the executable; rerunning that build and the focused test with native
access passed. This is an execution-environment limit, not a suppressed test.

`python3 R/fixture_replay.py` exits 0, using argument-array subprocess calls.
It runs `build/dev/we_collision_fixture` and the shared `we_process
inspect-collision` command, retaining stdout/stderr in `fixture.log` and
`inspection.log`. Request SHA is
`8262f43682297ad1765db386be4e5eb8ec24479caaa1742596f3efeca059902c`;
source manifest SHA is
`54ae971ceff12003c340ca7710c8fcfefb3cb0c36e6c9a02ac9124ca299da8dd`;
support SHA is
`621779efb21c5cb0c2b073ae280d38fe61d200e518bdf08f2940fcedb83e0540`.
The diagnostic manifest SHA remains
`c05583b8ce6973fa1d745e4c5cd0ff062447b21bb0a8e2a7aad6ca38a5fc0ea3`;
`products.json` pins every new file. These identities exactly match the prior
synthetic product regression; no real input was read or changed.

The 90-frame local native command was
`SDL_VIDEO_DRIVER=x11 build/dev/we_viewer --scene
R/inspection/diagnostic.wexplore --frames 90 --settings R/viewer-settings.json
--screenshot R/diagnostic-visible.ppm`. It exits 0 on an NVIDIA RTX 4060
Laptop GPU with **320 synthetic splats and zero Vulkan validation errors**.
The retained screenshot SHA is
`d2611cfd3d5cbe0ed569cdd5595491522ced77665dfc7f8f28ab8fca335cdcab`.
Visual inspection shows labeled green free, red occupied, blue unknown and
amber unsupported-floor samples, with walking unavailable. The sandboxed
attempt exited 2 because X11 was unavailable; the native retry passed.

## Profiles, preservation and remaining work

`cmake --preset dev` and `cmake --build --preset dev -j 4` exit 0 on GNU
16.2.1/C++26. `ctest --preset dev --output-on-failure -j 4` passes **448/448**.
`cmake --preset compat-cxx23 -DWE_BUILD_VIEWER=OFF`, its `-j 4` build and
`ctest --preset compat-cxx23 --output-on-failure -j 4` pass **423/423** on
GNU 16.2.1/C++23. Pinned clang-format/tidy 23.1.1 through
`python3 tools/check_cpp_style.py` passes for all five changed C++ files;
full tracked-Python Black, documentation, boundary and whitespace checks pass.
Raw suite and sanitizer outputs are retained as `ctest-dev.log`,
`ctest-cxx23.log` and `asan-focused.log` in R; `viewer.log` records the native
device result. The Python check ran
`git ls-files -z -- '*.py' | xargs -0 -n1 env
PYTHONPATH=/tmp/world_explorer_python_style_tools python3 -m black --check
--quiet --workers 1`; `python3 tools/check_docs.py` and `git diff --check`
exit 0.
Early style/tidy/boundary failures were corrected in code/policy; warnings and
acceptance thresholds were not weakened.

Read-only SHA-256 checks confirm the protected fresh native, baked real and
real depth-space manifests still equal respectively
`6f84d9ce12f79a899e75d685698c0e58bd94b406eb58814da87ff9fa0c6084de`,
`0b873804a3d5aa2a02c95245a43c550ed4d2ba2240d3af1e4325e23c198c6ee2`
and `28b470c06d83a203f9c85cb2b5c33cf0da0c1df44abd880b21dd65545a4d2573`.
No MVS, fusion, baking or refit was rerun. Explicit barrier *mesh* geometry,
Jolt static shapes, 120 Hz kinematic capsule, measured support and real walking
remain open. All 5,184 retained real cells remain unknown. The user has no
independent distance/up reference for internet-sourced `sample_1`; local M3
software exit can proceed under ADR 020, but Q5/N2 and A1 independent-scale
evidence remain separate requirements.
