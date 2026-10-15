# Synthetic floating origin — 2026-10-15 Asia/Hong_Kong

Scope/status: **M1/E1 IN_PROGRESS**. A separate binary-grid diagnostic verifies
joint scene/camera translation and camera-origin reupload. All **12** new native
Vulkan/gsplat comparisons pass at **≥72.923747 dB / maximum one RGB8 level**.
Positive/negative offsets preserve native, quantized reference and float reference
images byte-for-byte. This is synthetic coordinate evidence, not a real
reconstruction, streaming, packing, performance or full Q/P/N/E certification.
Cwd: repository root; parent **f03ae00**. Ignored/local raw root **R**:
`artifacts/validation/floating-origin-20261015/`.

## Contract and input

Specification §4.4 requires double world transforms and nearby float GPU origins.
Inspection confirms `renderer.cpp` subtracts the admission camera before narrowing
means, computes camera affine rows from double relative positions, and reuploads
an accepted scene when camera displacement exceeds **10,000** units. Production
renderer/shader code needed no change.

The [diagnostic contract](../protocols/viewer.md#synthetic-floating-origin)
uses 16 SH3 splats in two chunks, binary-fraction positions, identity and exact
Rx(pi)/0.5-scale transforms, four calibrated 1280×720 views and a fractional final
camera displacement. Fixed offsets are zero, **(2^30,−2^29,2^28)** and its negative.
Before the last capture the native camera moves **16,384** units along +X, then
returns, crossing the existing reupload threshold twice. Every case retains all
16 splats, six submitted frames and zero Vulkan validation errors.

Both baseline and translated profiles require the complete predeclared geometry,
SH payload, transforms, ordered cameras, origin and excursion. The comparator
checks finite numeric metadata, rejects booleans/subsets/changed profiles, binds
translated comparisons to a passing zero report and all image hashes, and checks
matching comparator/reference-library identities. Every output must be nonblank.
Native/reference and translation-invariance comparisons retain **≥45 dB / ≤8 RGB8
levels**, with no masks, fitted corrections or exclusions. The factory rejects
nonfinite or precision-losing *diagnostic translations*; production world positions
do not acquire an absolute float-range restriction.

## Identity and actual checks

`R/implementation-identity.json` pins all changed diagnostic/test files, native/core
binaries, unchanged renderer and SPIR-V. Native binary SHA:
`ecfc8083c904b7a4c046d5636b92c3f21f057a88e6ea7c00bed40cb1bb5f3964`.
Comparator SHA: `0326b43cc48507f2363860e86e58d89ed775742bce83ebfc18ca844ff3fd9704`.

| Case | Fixture SHA-256 |
|---|---|
| Zero | `bb18367100e93e7773d417a3fa0aeffd24fc3c876dd8f622cf1d6119e0460d94` |
| Positive | `6b83b5d6839211f620edaa99bbc5dbc8021b2b4bdb31c1ee0dbe6a23bebf0cc7` |
| Negative | `0f0c59f086c2e0e8d22543ed24b8e1fdc35457e03c34d2bd3922a66d0d0fd7e4` |

`R/run_checks.py` SHA
`74b5643298618b19ec6b1b949d088ef19ad73c106ffa45cda5cecd334abf9872`
records argv/cwd/exit/elapsed time, executable and selected source hashes in
`LABEL-command.json` / `LABEL.log`; native receipts include isolated home/cache
and display variables. `R/run_matrix.py` SHA
`92b873be77ee1f920d7550ac280c00565ee1fe40915b9ad23362b1d29ebccbd8`
defines six cases, with optional output/receipt suffix. Final invocation uses
`-checked`; initial results are retained separately.

| Command / receipt labels | Actual result |
|---|---|
| `cmake --build --preset dev -j 4`; `cmake --build --preset compat-cxx23 -j 4`, `build-*-checked` | Both **0**, GCC 16.2.1 C++26 native viewer/CLI/diagnostic and explicit C++23 CPU profile |
| `build/{dev,compat-cxx23}/we_core_tests 'synthetic floating origin*'`, `focused-*-checked` | **2 cases / 222 assertions** each; exact local float bits, premature absolute-float negative control, nonfinite/precision-loss rejection |
| `.venv-train/bin/python tests/gpu/reference_input_tests.py`, `input-tests` | **32/32** CPU cases, including six new profile/invariance/report-binding cases |
| `ctest --preset dev`; `ctest --preset compat-cxx23`, `ctest-*` | **265/265 / 247/247**, **70.24 / 69.30 s** CTest totals; **133 worker cases** each, Python 3.14.8; raw `*-LastTest.log` retained |
| `python3 R/probe_cli.py`, `cli-probes` | **7/7** invalid native/comparator requests rejected before output/GPU admission |
| `python3 R/run_matrix.py -checked`, `native/compare-{zero,positive,negative}-checked` | All **0**, 12 new view comparisons; all **16** native/reference translation-invariance checks have zero RGB8 error |
| Same matrix, `default/clipping/real-default-checked` | All **0**, four default + one clipping + three full **648,332-splat** sample_3 views, maximum one RGB8 level, zero Vulkan errors |
| Format/tidy 23.1.1, docs/boundaries/diff | All **0**, checked receipts; final tidy includes the new test header explicitly, no first-party diagnostics |
| `python3 R/summarize.py`; `python3 R/run_checks.py after` | Both **0**, bound identities/byte equality and unchanged protected/preceding/production inventories |

The full CPU suites preceded an equivalent integer-row extraction required by
header-inclusive tidy; both focused suites and the complete six-case native
matrix were rerun after that correction. Initial tidy logs remain. Sandbox NVML
inspection exited **9**; native inspection succeeded. Linux RTX 4060 Laptop,
driver **615.78.08**, Python 3.10.22, gsplat 1.5.3+pt24cu124, torch 2.4.1+cu124,
CUDA 12.4. This is not H_R/H_V/H_M or another OS. Runs overlap CPU work; elapsed
and owned allocation numbers are not performance or peak-device-memory evidence.

`R/regression-summary.json` binds every view/hash and proves existing default,
clipping and real fixtures/readbacks unchanged, and initial/final matrices equal.
Before/after inventories preserve **2,088 protected / 1,531 preceding evidence /
94 src/include files**, both completed scenes and every failed stage. No solver,
fit, decode, installation, download, upload or push occurred. Coverage, sanitizers,
Clang GPU and other platforms were not rerun. M1's prescribed consented fixture,
reference-device/native-platform measurements and dependency rights remain open;
[progress](../progress.md) owns the next prerequisite.
