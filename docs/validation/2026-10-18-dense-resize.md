# Checked resized dense inputs — 2026-10-18 Asia/Hong_Kong

Scope/status: **M3 IN_PROGRESS**; separate input preparation/admission verified
locally. No new depth, backend qualification, fusion, measured scale, Q5/N2 or
full gate. Parent **bec66b5**; implementation is the commit containing this report.
Cwd: repository root. Ignored evidence **R**:
`artifacts/validation/m3-dense-resize-20261018`.

## Behavior and real checks

The [resized-input contract](../protocols/dense_inputs.md#separate-resized-profile)
pins native and resized images, native manifest, preprocessing implementation/
environment, selected full depth recipe and per-axis half-pixel calibration.
The isolated Pillow preparer owns new PNGs and publishes its manifest last;
cancellation/invalid inputs preserve partial work without a completion marker.
Native C++ admission reconstructs expected metadata independently from retained
poses, validates actual image hashes/headers and rejects forged dependencies,
calibration, dimensions, inventories, links, ambiguous JSON and stale inputs.
The existing native profile and three-view pilot remain unchanged.

All **63 sample_1 frames** prepare from **4104×2208 → 2048×1102**, with unequal
per-axis scales, and pass native admission. The native manifest is byte-identical
to the preceding final preflight. No solve/refit occurs. The full selected depth
recipe is explicitly **UNQUALIFIED_NOT_EXECUTED**, an input probe, not a runnable
resized MVS configuration. `real-result.json` retains:

| Identity | SHA-256 |
|---|---|
| Native manifest | `495084082f7e1f5e272c700aa836d1143f2352320a1e85d8b8221fd7276e48f8` |
| Resized manifest | `2e74658df238d573541d30b930950bc74937b90cbbd8a6a66abc28e39e173e31` |
| Selected resize/depth recipe | `eaf5ea444bf4cd01efc9c944bf42ec9c03950614014d899db656991f389f88db` |
| Resize implementation bundle | `ae25e4459532612ec5f608522d189bf94fe7dbb41f021bdd0ca5e71ec1b7e6ac` |

Independent `check_real.py` decodes all native/resized images, rechecks their
hashes, compares **6,237 sampled pixels** against a separately implemented widened
triangular filter with separable RGB8 rounding, and compares rays against native
pycolmap camera matrices. Maximum/mean/p99 absolute RGB8 error:
**1 / 0.00122922345 / 0**; maximum ray error **2.22045e-16**.
`independent-real.json` retains per-frame results. Installed observer versions:
Pillow **11.3.0**, NumPy **2.2.6**, pycolmap **4.2.1**. No GPU is invoked.

## Commands and outcomes

Toolchains: GCC **16.2.1 C++26 Debug**, explicit GCC **C++23 CPU Debug**, installed
Clang **23.1.3 C++23 Debug**. Prescribed format/tidy **23.1.1** use the existing
ignored `artifacts/tools/llvm-23.1.1` extraction; no package installation occurs.
All successful checks below exit **0**; logs are under R. Timings include
competing builds and are not performance qualification.

| Actual command | Result / logs |
|---|---|
| `cmake --preset dev`; `cmake --build --preset dev -j 4`; `ctest --preset dev --output-on-failure -j 4` | **308/308**, final **31.19 s** (initial **24.21 s**); `configure-dev.log`, `build-dev-final.log`, `ctest-dev.log`; final source rerun in `build-dev-post-style.log`, `ctest-dev-final.log`; final literal/style-only native rebuild/focused **12/207** in `build-dev-handoff.log`, `dense-dev-handoff.log` |
| `cmake --preset compat-cxx23 -DWE_BUILD_VIEWER=OFF`; `cmake --build --preset compat-cxx23 -j 2`; `ctest --preset compat-cxx23 --output-on-failure -j 4` | **285/285**, **33.10 s**, before const naming-only tidy fixes; `configure-cxx23.log`, `build-cxx23.log`, `ctest-cxx23.log` |
| `.venv-dense/bin/python -m unittest discover -s tests/integration -p worker_dense_resize_test.py -v` | **6/6**, no skips; `resize-python-final.log`; pure structure checks also run in CTest, pixel tests require pinned Pillow |
| `python3 artifacts/validation/m3-dense-resize-20261018/prepare_real.py` | Native preparation, actual resize and native admission; exact argv/hashes/exits in `real-commands.json`; resize **93.959 s**, admission **1.369 s** |
| `.venv-dense/bin/python artifacts/validation/m3-dense-resize-20261018/check_real.py` | Independent all-frame checks above; `independent-real.log` |
| `cmake --build build/m2-vma-clang -j 2 --target we_process we_core_tests`; `build/m2-vma-clang/we_core_tests '[dense]' --reporter compact` | **12 cases / 207 assertions** on final behavior; `build-clang-handoff.log`, `dense-clang-handoff.log` |
| Native `cmake --build --preset asan -j 2 --target we_core_tests`; `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 build/asan/we_core_tests '[dense]' --reporter compact` | **12/207**, no reported sanitizer errors; `build-asan-handoff.log`, `dense-asan-handoff.log` |
| `.venv-dense/bin/python -m unittest discover -s tests/integration -p 'worker_dense*_test.py' -v` | **18/18**, no skips; `python-dense-final.log` |
| Pinned style command below | **4 files**, three tidy translation units, warnings as errors; `style-pinned-complete.log` |

Pinned style command:

```sh
LD_LIBRARY_PATH="$PWD/artifacts/tools/llvm-23.1.1/usr/lib" \
PATH="$PWD/artifacts/tools/llvm-23.1.1/usr/bin:$PATH" \
python3 tools/check_cpp_style.py --build-dir build/m2-vma-clang \
  include/world_explorer/application/dense_inputs.hpp src/application/dense_inputs.cpp \
  apps/we_process/main.cpp tests/unit/dense_input_tests.cpp
```

Four new synthetic C++ cases cover correct unequal-axis depth admission and
rehash-resistant calibration/dependency/inventory attacks, modified PNGs and
headers, symlinks, cancellation, duplicate keys and unsupported resize recipes.
Together with existing dense cases: **12 cases / 207 assertions**.
Six Python cases include actual encoded PNGs with an independent known-ramp filter,
recipe drift, corrupt PNGs, overlap/occupied paths, symlinks, and cancellation
before/after pixel publication with no terminal manifest.

Initial failures are retained: fixture comparison required explicit JSON string
extraction; independently entered fixture intrinsics differed by floating-point
operation order. The first pixel oracle incorrectly assumed discrete downsample
filtering preserved affine ramps exactly; replacing it with the independent
normalized triangular filter tests actual filtering, including boundaries.
Tidy's const naming fixes and initializer-spacing repair do not weaken checks.
Sandbox ASan test discovery fails because LeakSanitizer cannot run under ptrace
(`build-asan.log`); the native build/test above passes with leak detection enabled.
The first preservation observer misclassified six existing interpreter symlinks;
checking their pinned target bytes gives no missing/changed entries. Initial
observer output remains in `initial-audit.json` / `initial-audit.log`.

Final preservation evidence `audit.json` combines the 1,692-file pre-task baseline
with the preceding dense pilot's 22,019-file baseline: **22,183 distinct entries**,
zero missing/changed, including retained videos/runs, frozen fixtures, prior pilot
and the pose/training environments. It also binds the six changed code/test files.
No existing environments or dependencies were installed/modified. Seven native/
Clang/adverse CLI checks retain argv/exits in `cli-checks.json`: both compilers
admit the real output; wrong manifest/source/recipe hashes, rehashed principal-point
forgery and occupied resizer output reject with exit **2**.

`python3 tools/check_docs.py` and `git diff --check` pass. Default
`python3 tools/check_dependency_manifest.py --local-evidence` remains exit **2**
(`Changed evidence: cmake/dependencies.cmake`); selecting
`--manifest third_party/dependency-audit-input-20261016.json --local-evidence`
also exits **2** (`Current runtime payload differs`). Logs:
`dependency-current.log`, `dependency-selected.log`. Fresh loader/mapping/owner
receipts and redistribution qualification remain open; no receipt was changed
to claim a pass.

Resized backend camera/model adaptation and a pinned overlapping-neighbor recipe
remain next. Production job cancellation/kill/reap/resume, fusion, compressed
storage, measured scale, collision/walking and mesh export remain open. A0–A6/AP
gates remain unstarted; no M4 platform qualification, long-stress rerun, upload
or remote push occurs.
