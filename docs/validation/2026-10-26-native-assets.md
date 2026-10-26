# Native geometry/material asset adoption — 26 October 2026

Scope: **A0/AP0 IN_PROGRESS**. The [new draft path](../protocols/native_assets.md)
connects actual privately admitted geometry/material payloads and optional selected
completion with revisioned definitions/instances. It checks original receipt and
epoch/pose, every material group, complete face/channel provenance, recomputed
pivoted bounds and compensated original world placement. Owned metadata history
provides atomic publication, cancellation/replay and selected-commit reload.
Generated-static v1, captured GLB and previous product readers are unchanged.
No real production, rendering, relighting, export or AP gate is qualified.

Repository-root cwd: `/home/Pooh555/programming/World-Explorer`. Clean parent
`7bc8590bd4db4f06390e4050e29baae6e5ea3e36`. Final implementation identity
`b83ae7a0ef6d24819f571ab2d2d9a695f8a6b9f31c1127b46ad325c162b51485`
is SHA-256 of sorted compact path→file-SHA JSON for CMake plus the five added C++
files. Ignored evidence root `R=artifacts/validation/a0-native-assets-20261026`
contains `source-final.json`, exact command arrays/cwd/exits/timings in per-check
JSON, raw logs and `environment.json`. Source identity was rechecked at handoff.

## Commands and observations

Local GNU 16.2.1, CMake 4.4.4, C++26 development and explicit C++23 compatibility;
format/tidy use workspace LLVM **23.1.1**. No language standard or warning is
weakened. `run_check.py` in R records the following actual commands and exits:

```sh
cmake --build --preset dev -j 4
cmake --build --preset compat-cxx23 --parallel 4
ctest --preset dev
ctest --preset compat-cxx23
```

| Final check | Result / log in R |
|---|---|
| Full development build and CPU/worker suite | Exit 0; **833/833**, 261.27 s; `build-dev.log`, `ctest-dev.log` |
| Explicit C++23 build and CPU/worker suite | Exit 0; **808/808**, 259.48 s; `build-compat.log`, `ctest-compat.log` |
| Focused development and compatibility | Exit 0 each; **96 cases / 53,919 assertions** each; `focused-dev.log`, `focused-compat.log` |
| Native sanitizer build and focused run | Exit 0; **96 cases / 53,919 assertions**, no findings; `build-asan.log`, `focused-asan.log` |
| Exact repo format/tidy/boundary gate | Exit 0, five added C++ files; `style-final.log` |
| Application/test libc++ syntax | Exit 0 each; `libcxx23-application.log`, `libcxx23-final.log` |

Documentation checks pass (1,600 local targets in 303 files); whitespace checks
exit 0. Exact results are retained in `docs.log` and `diff.log`.

The focused filter is
`[native-assets],[geometry-product],[material-product],[completion-product],[static-export],[asset-edit],[asset-schema]`,
run with each profile's `we_core_tests`. Ten new cases include an authored
two-face/two-group native mesh, two independently admitted six-channel materials,
shared definition/two instances, captured-unlit preservation, declared manual
ancestry, missing-group/foreign/stale rejection and distinct semantic identities.
Geometry/source/frame bindings use actual native snapshots. A 180° pivot rotation
requires compensating original placement; unadjusted placement fails. Separate
authored object and shell completion fixtures preserve eight observed faces and
two generated faces; wrong subject, omitted completion pin and a valid unselected
candidate fail. These are synthetic diagnostics, not reconstructed assets.

Publication checks exercise a cancelled first blob, empty committed snapshot,
matching recovery/replay, changed-operation and stale-base rejection, subsequent
scene revision, historical semantic revision under the newly selected current
physical commit, source-tree protection and preserved foreign output. Metadata
is the existing two-record definition/instance transaction; native product bytes
remain in the original receipt. No process-crash or arbitrary power-loss claim
is added by this slice.

```sh
cmake --build --preset asan -j 4
build/asan/we_core_tests '[native-assets],[geometry-product],[material-product],[completion-product],[static-export],[asset-edit],[asset-schema]'
PATH="$PWD/artifacts/tools/llvm-official-23.1.1/bin:$PATH" \
  python3 tools/check_cpp_style.py \
  include/world_explorer/application/native_assets.hpp \
  src/application/native_assets.cpp tests/fixtures/native_assets_fixture.hpp \
  tests/fixtures/native_completion_fixture.hpp tests/unit/native_assets_tests.cpp
python3 tools/check_docs.py
git diff --check
```

Sanitizer runs use native access and default ASan/UBSan/LeakSanitizer settings;
the three sanitizer option environment variables are unset. Scoped libc++23.1.1
syntax checks use Clang23.1.4 on both new application/test translation units:
exact argument arrays and results are retained. They are not a full libc++ build
or a native Windows/macOS/GCC13 pass; pending platform/build confirmations remain.

## Failures, review and preservation

`build-first.log` records Catch INFO's unparenthesized conditional expansion;
parentheses repair the diagnostic expression. `style-first.log` identifies unchecked
optional access through a generic assertion helper, a redundant move and a cloned
test branch. Checked selection helpers and consolidated assertions fix these;
no check is suppressed. Later review removes a caller-source-vector copy before
the metadata count guard, replacing it with an exact bounded permutation comparison.
Earlier successful full/focused/style/build results are retained as
`*-before-review.*`; final checks use the identity above.

Saved-data verification checks **378 original hashes**, including retained video,
scene/map/cache and completed recovery products, with **zero mismatches**.
The previous synthetic exported GLB remains
`0a00277c98a88668053cc6d9cdb716ad78379bda8046545a7606f676e4d76357`;
its final manifest remains
`6af871832b57acf9806b76da1d73fc3d937fcc7942044857fcdd01918734c396`.
No video, model/environment, producer, GPU, validator or Godot run occurs here.
Dependency-runtime receipts remain stale after relinking.

Heterogeneous channel-summary representation, broader structure/LOD/collider and
producer integration, texture conversion, native PBR viewing and real A1/Godot
validation remain open. The [handoff](../progress.md#current-a0-work) owns the next
converter step and unchanged provisioning/capture prerequisites.
