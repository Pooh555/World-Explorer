# Local foundation evidence — 11 October 2026

Working directory: repository root. Source identity: implementation commit
containing this report, initially based on `06f3aa8`. User specification/runbook
edits remain unstaged. Scope: Arch Linux development profile, not H_R/H_V or
native Windows/macOS/AMD qualification. Large logs and imagery stay ignored.

| Actual command | Result | Private local output |
|---|---|---|
| `cmake --preset dev` / `cmake --build --preset dev -j 4` | exit 0, GCC 16.2.1, C++26 | `artifacts/validation/dev-build.log` |
| `ctest --preset dev --output-junit ../../artifacts/validation/core.xml` | exit 0, 18/18 | `artifacts/validation/core.log`, `core.xml` |
| `cmake --preset clang -DWE_BUILD_VIEWER=OFF` / `cmake --build --preset clang -j 4` | exit 0, Clang 23.1.1, C++26 | `build/clang` |
| `ctest --preset clang --output-junit ../../artifacts/validation/clang.xml` | exit 0, 18/18 | `artifacts/validation/clang-tests.log`, `clang.xml` |
| `cmake --preset asan` / `cmake --build --preset asan -j 2` | exit 0 | `build/asan` |
| `ctest --preset asan --output-junit ../../artifacts/validation/asan.xml` | exit 0, 18/18, address/undefined/leak sanitizers | `artifacts/validation/asan.xml` |
| `python tools/check_boundaries.py` | exit 0 | console |
| `clang-format --dry-run --Werror` on first-party C++ | exit 0, 23.1.1 | console |
| `clang-tidy -p build/dev` on bundle, scene store, update sink, process launcher | exit 0, no unsuppressed diagnostics | `artifacts/validation/tidy.log` |
| `build/dev/we_viewer --diagnostic-fixture --frames 24 --screenshot artifacts/validation/diagnostic-fixed.ppm --settings artifacts/validation/settings.json` | exit 0; 45 synthetic splats; zero validation errors | `artifacts/validation/gpu-smoke.log` |
| `build/dev/we_gpu_smoke artifacts/validation/overlap-final.ppm` | exit 0; 46 frames; overlap, replace/delete/resize; zero validation errors | private screenshot |

GPU: RTX 4060 Laptop GPU, 8 GB, Vulkan 1.4.351, driver 615.78.08. Native access
required for GPU and LeakSanitizer; sandbox driver/ptrace failures are not passes
or evidence of missing hardware. Synchronization validation enabled. Independent
two-splat center RGB expectation (130,4,68), tolerance 3; observed (130,4,68).
Revision 3 deletion leaves no resident splats. Diagnostic allocation 32,136,576
bytes is renderer-owned, not total driver/process peak. No FPS gate claimed.

Tests cover camera conventions, immutable revisions, publication/corruption,
replay/idempotency, bounded queues, cancellation/process cleanup, deferred GPU
residency and fly input. Worker decode/error tests use an explicitly synthetic
FFmpeg two-second video; those tests do not certify reconstruction.

## Consented real pose run

Original source SHA-256:
`31fa05e1c483228f8ac7b1f8ab0958e889625ae89b1e177276b57566ece28632`.
Sequential pose failure: components 2/23/10 frames. Explicit exhaustive-guided
recovery failure: components 22/46 of 92 frames. Failed outputs preserved in
`artifacts/sample-job` and `artifacts/sample-recovery`.

Replacement authorized and moved to `data/local/sample.mp4`, SHA-256:
`51263d9991dcbc2136d0809ecba1a8ba1b261896f3b222b7afba19784a153ef6`.
20.948333 s, H.264 3840×2160 at 30 FPS. Actual command:

```sh
build/dev/we_process run --request artifacts/sample2-request.json --worker-python .venv-worker/bin/python
```

Exit 0, one connected model, **63/63 selected frames registered**. Stage times:
inspection 0.159 s, decode 92.066 s, poses 265.764 s. Private output:
`artifacts/sample2-job`; log: `artifacts/validation/sample2-poses-run.jsonl`.
Pose stage identity:
`7cbf023a96698e282b3bc4debfd6b5378533c0b8dcef21580c6f2a14bf444aec`.
Python 3.12.15, official pycolmap 4.2.1, NumPy 2.2.6, Pillow 11.3.0.
Original PTS/frame indices retained; integer-center canonical calibration.
Scale arbitrary, geometry/walk unavailable. This is not an M1 appearance scene.

Unqualified: actual gsplat fit, reference rendering/metrics, TSan/coverage,
native platform CI, release dependencies/packages, corpus Q/P/N/E gates.

The renderer device/swapchain extraction initially exposed destruction-order and
render-pass leaks. Fixed before the implementation commit; final smoke log
`artifacts/validation/overlap-final.log` includes shutdown and no validation messages.
SDL key bindings stay in the viewer, preserving the CPU-only navigation boundary.

Environment probe retained from the prior handoff: authorized native
`vulkaninfo --summary` detected the above GPU/driver; sandbox-only ICD loading
failed. Local environment additionally recorded CMake 4.4.4, SDL 3.4.18, Vulkan
headers/loader 1.4.363, OpenSSL 3.6.5 and system Python 3.14.8 (distinct from the
Python 3.12.15 pose environment). Foundation commit: `ea289fc`; no remote push.
