# Explicit synthetic wall faces — 2026-10-21 (Asia/Hong_Kong)

M3 remains **IN_PROGRESS**. `SYNTHETIC_COLLISION_SUPPORT_V2` selects explicit
source mesh triangles covering an occupied cell's vertical face. Admission pins
source/pose/chunk identity, requires two outward-wound triangles with the full
face's shared diagonal, and rejects free/unknown cells, duplicate selections or
geometry outside the exact grid face. The V2 canonical cook preserves floor and
wall groups; the existing private Jolt adapter builds all four fixture triangles.
No controller, collision response or measured walking is enabled. See the
[support](../protocols/collision_support.md) and
[cook](../protocols/collision_cook.md) contracts.

Repository root is the default cwd; parent commit is
`68b0089da7a5f30a8b5a3d9a00dd785beb35112e`. Raw logs, generated
fixtures, probe source/binaries and captures are ignored under
`artifacts/validation/m3-wall-20261021/` (`R`). No real MVS, fusion, bake or refit
was rerun, and no capture or generated product enters Git.

`cmake --build --preset dev -j4` and
`ctest --preset dev --output-on-failure -j4` exit 0 on GNU 16.2.1/C++26:
**455/455** checks. The corresponding `compat-cxx23` build and CTest commands
exit 0 on GNU 16.2.1/C++23: **430/430**. Focused development and native
`ASAN_OPTIONS=detect_leaks=1 build/asan/we_core_tests
'[collision-wall],[collision-support],[collision-cook],[jolt-shape]'
--reporter compact` each pass **20 cases / 951 assertions**, without reported
ASan/UBSan/LSan errors. The sanitizer target is built with
`cmake --build --preset asan --target we_core_tests -j4`; native access avoids
the known sandbox LeakSanitizer/ptrace discovery limit. Tests include all four
vertical directions, malformed/duplicate/incomplete faces, source correction,
cancellation and prior-owner preservation. The seven-file pinned C++
format/tidy gate, documentation links, include boundaries and whitespace pass.
Early format/naming/optional-access failures were corrected without weakening
warnings. There is no new Python code.

`build/dev/we_collision_fixture R/fixture-v2 --walls` and
`build/dev/we_process inspect-collision R/fixture-v2/request.json
3f493a8c04961c99b7f24094affa621e9a3a6509929daca8620ae7a57b2e5372
R/inspection-v2` exit 0. Request, source manifest and support SHA-256 are
respectively the pin above,
`e3034715aa8e8cbecf8fa874e189038b8ab17ff5d791e78f33df0769e8704a13` and
`a7eb564e685393ea60c3d30cb32c48610754a9e963173725fda4f1d66fb1df11`.
The shared inspection reports clear / occupied / unknown / unsupported-floor
for its four probes and keeps `walking_enabled=false`,
`measured_collision=false`. Its report SHA is
`97651346146539b0549ad3788d679a4c51acd2b1a6ecba22514eac46af4bbe43`.

Two native-access runs use `SDL_VIDEO_DRIVER=x11 build/dev/we_viewer --scene
SCENE --frames 90 --settings R/viewer-settings.json
--screenshot OUTPUT`. `SCENE` is first `R/inspection-v2/diagnostic.wexplore`,
then `R/fixture-v2/source.wexplore`; outputs are `R/diagnostic-visible.ppm` and
`R/source-mesh-visible.ppm`. Both exit 0 on the NVIDIA RTX 4060 Laptop GPU with
zero Vulkan validation errors: 320 diagnostic splats, then 4 mesh triangles /
8 vertices. Visual inspection confirms the green/red/blue/amber state samples,
the source floor and vertical wall, synthetic labels and unavailable walking.
Screenshot SHAs are respectively
`18802304ef7517224c85e834924b58d5a428ff2a239fc9cda2632283270e0b60` and
`d956f8dd11e432e93cf8d2ef8af80be8919d883ababcfbb642bf6dc26ac94619`;
settings SHA is
`b865980007141b5a5bd624968120841f4e1a9856599d84660275d57cdfebfc2c`.

The default V1 fixture reproduces its prior source/support bytes exactly:
`54ae971ceff12003c340ca7710c8fcfefb3cb0c36e6c9a02ac9124ca299da8dd` /
`621779efb21c5cb0c2b073ae280d38fe61d200e518bdf08f2940fcedb83e0540`.
A retained probe compiles the parent cook source against current V1 contracts
for the all-free unit fixture; `cmp` confirms its 417-byte stream equals the
current cook, SHA
`cdee4ccb1e8d70a8d9b5c90fba7a8376289a36c30bdabf3f5982861969160941`,
also pinned in the regression test. All 12 prior fixture/diagnostic products
still match their retained receipt. These synthetic results do not certify
Q5/N2. All 5,184 retained real space cells remain unknown; `sample_1` still has
no independent distance/up reference. ADR 020's local software exit and A1's
independent-scale pilot remain separate. The next M3 implementation is the
fixed 120 Hz kinematic capsule and its conservative unknown-space integration.
