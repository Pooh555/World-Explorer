# Retained pose/fit calibration — 2026-10-12, Asia/Hong_Kong

Parent: `2329a8f`; implementation commit contains this report. Cwd: repository
root. M1 remains IN_PROGRESS; M7–M8 deferred. No remote push/upload or repeated
COLMAP/gsplat run. Specification and user runbook remain unchanged.

## Change and identity

Shared GUI/CLI admission now reads the hash-selected undistorted COLMAP camera
before map preparation/optimizer launch, and rechecks it before fit publication.
The isolated-worker architecture is unchanged: a ≤64-byte native interchange
reader supports one SIMPLE_PINHOLE/PINHOLE camera, without SDK linkage. All fit
original/output dimensions and intrinsic entries must match the camera and
explicit nearest-even, per-axis resize/half-pixel recipe. Capture OPENCV
calibration is a different image domain. [Implemented contract](../protocols/jobs.md)
records bounds/tolerance; [official format](https://colmap.github.io/format.html)
and [versioned reference layout](https://github.com/colmap/colmap/blob/3.11.1/scripts/python/read_write_model.py)
support the endian/record definition. This is original parsing code, not copied
solver code. Unsupported multi-camera/distorted training models fail explicitly.

`identity.json` hashes 128 sorted `CMakeLists.txt` and `.cpp`/`.hpp`/`.fbs`/`.py`
files in `apps/include/src/tests/schemas/workers` as relative UTF-8 path + NUL +
binary file SHA-256:
`566fdada943235ed7577144d5fca52e87a93d1bfe2c66b3099f5a182f015c929`.
Dev CLI SHA: `6260e6429bb2b3175a3b20368f1aef68b861411436f1bfc880ddcdbb52641df9`.

## Local checks

Arch Linux; GCC 16.2.1, Clang/libc++/format/tidy 23.1.1, CMake 4.4.4; existing
system-library development configurations. Ignored logs/probes/identities:
`artifacts/validation/fit-calibration-20261012/`.

| Actual commands | Result |
|---|---|
| [DEV, COMPAT, LIBCXX, ASAN](checks-2026-10-12.md) | Build/test exit 0; **84/84 each**; `build-*-final.log`, `tests-*-final.log` |
| `clang-format --dry-run --Werror` on six changed C++ headers/sources | pass |
| `clang-tidy -p build/dev src/reconstruction_adapters/training_camera.cpp src/application/job_validation.cpp tests/unit/training_camera_tests.cpp tests/unit/reconstruction_job_tests.cpp --warnings-as-errors='*'` | exit 0; no unsuppressed diagnostics; `tidy-final.log` |
| `python3 tools/check_boundaries.py`; `python3 tools/check_docs.py`; `git diff --check` | exit 0 |
| `python3 artifacts/validation/fit-calibration-20261012/verify.py` | exit 0; `verification.json`, `real-probe-final.log`, individual CLI logs/requests |

ASan/UBSan/LSan build/discovery/tests use authorized native access. Separate TSan,
Clang C++26 and hosted native profiles were not rerun. Five new cases cover
asymmetric/simple cameras, rehashed malformed/unsupported/count/size/finite-value
inputs, checksum/cancellation, nearest-even anisotropic resize, and rejection
before map/scene creation. Extended coordinator cases reject focal/principal/
dimension changes and preserve prior scenes. Tiny fixtures remain synthetic.

The initial private recovery request mistakenly included empty `decode_stage`;
the parser rejected it before admission. The probe now omits that optional field
and requires the actual fit-dependency rejection. Initial failure logs remain
retained; they are not evidence for that boundary.

## Retained real boundaries and native viewer

Original/recovery training cameras are independently **4109×2209 / 4091×2203**
PINHOLE models; the original fit resizes to **1280×688**. Original publication and
cross-process restart succeed. Six copied fit-manifest mutations fail (exit 2)
without changing output map/scene manifests. The original fit against a newly
prepared recovery map fails its dimension dependency. A full isolated recovery
copy also adopts/restarts its 63-pose map; changed pose journal SHA and rehashed
decode journal SHA both fail without changing its prior map. No backend START,
pixel decoder, solver or optimizer executes in these probes.

**84 protected files**, including video and prior JSON inputs/manifests, retain
hashes. Both maps retain distinct canonical assets (`c99c5155…` original,
`efac98bd…` recovery; full identities in the [preparation report](2026-10-12-pose-preparation.md)).
New scene `artifacts/sample2-fit-boundary-20261012/original.wexplore` retains all
**304,867 splats / 63 frame dependencies**. Its 73,168,080-byte payload SHA remains
`790caca73b368f90d893850bfaf4c9703af55b4da78d4900022498fe46b04893`.

Actual native command, exit 0:

```sh
build/dev/we_viewer --scene artifacts/sample2-fit-boundary-20261012/original.wexplore --source-view --max-splats 500000 --frames 120 --screenshot artifacts/validation/fit-calibration-20261012/native-view.ppm --settings artifacts/validation/fit-calibration-20261012/settings.json
```

RTX 4060 Laptop GPU / driver 615.78.08 (`nvidia-smi --query-gpu=name,driver_version --format=csv,noheader`):
120 frames, 304,867 resident splats, 142,296,064 renderer-owned allocation bytes,
zero Vulkan/synchronization/shutdown errors. Private screenshot inspected via
lossless PNG conversion; appearance ready, arbitrary scale, walking unavailable.
This upright spectator reset is not calibrated rendering parity, peak memory or
a performance measurement. The scene exceeds the prescribed ≤250k fixture;
full real fresh chaining, reference hardware/corpus and native platform gates
remain unqualified. Next work lives in [progress](../progress.md).
