# Synthetic scene labels — 2026-10-12, Asia/Hong_Kong

Scope: M1/E1 local correctness, **IN_PROGRESS**; no milestone/platform certification.
Imported synthetic bundles were rendered without a synthetic label because UI,
ready status and terminal classification used only `--diagnostic-fixture`.
They now derive classification from the active immutable snapshot's explicit
[provenance](../protocols/scene.md). The toolbar label remains visible when panels
are hidden; replacing a scene updates classification. Reference fixture v3 uses
the canonical marker. No filename inference or reconstruction changes.

## Identity and checks

Cwd: `/home/Pooh555/programming/World-Explorer`. Parent
`40b97c12a30603daa4b8930e7875e2456836d5f2`; dirty implementation inventory SHA-256
`787ea869bae67f10a391743664c0f19b36c239107708504264bc7ebc692cc53b`.
Raw evidence prefix **R**: ignored `artifacts/validation/render-reference-20261012/`;
`label-identity.json` retains source and viewer binary hashes. Arch/GCC C++26,
format/tidy 23.1.1, native RTX 4060 Laptop / 615.78.08; environment and v2 renderer
measurements remain scoped in [reference evidence](2026-10-12-render-reference.md).

Actual commands, all exit 0:

```sh
cmake --build --preset dev -j 4
ctest --preset dev --output-on-failure
clang-tidy -p build/dev apps/we_viewer/job_state.cpp apps/we_viewer/main.cpp tests/ui/job_state_tests.cpp tests/gpu/reference.cpp
clang-format --dry-run --Werror apps/we_viewer/job_state.hpp apps/we_viewer/job_state.cpp apps/we_viewer/main.cpp tests/ui/job_state_tests.cpp tests/gpu/reference.cpp
build/dev/we_gpu_reference artifacts/validation/render-reference-20261012/after-v3
build/dev/we_viewer --scene artifacts/validation/render-reference-20261012/after-v3/fixture.wexplore --frames 1 --screenshot artifacts/validation/render-reference-20261012/label-v3.ppm --settings artifacts/validation/render-reference-20261012/label-v3.json
build/dev/we_viewer --scene artifacts/validation/render-reference-20261012/after/fixture.wexplore --frames 1 --screenshot artifacts/validation/render-reference-20261012/label-legacy.ppm --settings artifacts/validation/render-reference-20261012/label-legacy.json
build/dev/we_viewer --scene artifacts/sample2.wexplore --source-view --max-splats 500000 --frames 1 --screenshot artifacts/validation/render-reference-20261012/real-label.ppm --settings artifacts/validation/render-reference-20261012/real-label.json
python3 tools/check_boundaries.py
python3 tools/check_docs.py
git diff --check
```

DEV **91/91** (87 core/worker + four viewer cases). Added case covers no scene,
canonical/legacy markers, replacement with real/unannotated content and returning
to synthetic content. No unsuppressed tidy warnings. Logs:
`build-label-final.log`, `tests-label.log`, `tidy-label-final.log`.
Initial namespace-alias omission caused compilation/tidy failure; corrected before
these passing runs (`build-label.log`, `tidy-label.log` retained).

V3 fixture JSON SHA-256:
`970273807404e7ac50e984ce8557386f4aa599a9022627df7a24178ca19d2e6f`.
Rendering inputs and all four native PPM files are **byte-identical to v2**;
only provenance/identity changes. `native-v3.log`: 18 splats, zero validation errors.
No new CUDA comparison is claimed for this metadata change.

## Native observations and limits

Before repair, the legacy-bundle command with `label-before` output/settings
returned 0 but misclassified its 18 splats as `user-selected` (`label-before.log`).
After repair, canonical and legacy imports report `synthetic`, nine frames each,
zero validation errors (`label-v3.log`, `label-legacy.log`). Canonical settings
hide both side panels and the controls overlay; the losslessly converted screenshot
still shows **SYNTHETIC SCENE** and **Ready: synthetic diagnostic fixture**.
Retained real scene reports `user-selected`, 304,867 splats, 186 frames and zero
validation errors (`real-label.log`). Counts are not FPS measurements.

Private captures/jobs and earlier v2 evidence remain untouched. CPU-only sources
are unchanged; the preceding 87-test C++23/libc++ runs were not repeated. Native
Windows/macOS, manual input/HiDPI/latency, saturation/termination and broader renderer
qualification remain pending. No synthetic fixture completes a real capture gate.
