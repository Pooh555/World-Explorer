# Current handoff

Updated 2026-10-19 (Asia/Hong_Kong). **M1: VERIFIED (Linux development exit);
M2: VERIFIED (Linux/NVIDIA development exit); M3: IN_PROGRESS.** No full Q/P/N/E
gate is certified. A0–A6/AP0–AP9: NOT_STARTED; M7–M8: DEFERRED_BY_USER.

[ADR 019](adr/019-assets-before-platforms.md) orders M3 → A0–A5 → M4 → M5 → M6,
then A6 engine packaging. The [asset track](technical_plan.md#11-offline-asset-production-track)
adds reusable objects/structure, labeled completion, PBR and shared glTF/GLB
assembly: first visible end-to-end in A1, locally validated through A5.
[Research](research/offline_asset_pipeline.md) and
[rescheduling](validation/2026-10-17-assets-before-platforms.md) qualify no capability;
[original planning](validation/2026-10-17-offline-asset-planning.md) retains its
superseded after-M6 schedule. Those tasks installed/processed/uploaded nothing.
M1–M6 order/thresholds remain; native/reference release evidence is due by M6.

## Usable behavior and evidence

- [Batch-worker regression repair](validation/2026-10-19-batch-worker-repair.md)
  updates the embedded diagnostic worker to the renamed Python `EventWriter`
  class; all three regressions and the full 374-test development CPU suite pass.

SDL3/ImGui viewer/CLI share isolated FFmpeg/COLMAP/gsplat processing, checked
bundles, transactional scene/map snapshots, bounded async updates, Vulkan
anisotropic projection/radix/tile rendering and fly navigation.
[Contracts](README.md) own behavior; the [validation index](validation/README.md)
retains all historical checks, identities, failures, timings and profile limits.

- [M1 exit](validation/2026-10-15-m1-exit-review.md) and
  [M2 six-item exit](validation/2026-10-16-m2-exit-review.md) close local development
  only ([ADR 012](adr/012-m1-development-exit.md), [ADR 016](adr/016-m2-development-exit.md)).
  Physical Windows/macOS viewer/GPU/display/input/HiDPI and ≤250k / 720p ≥30 FPS
  on H_V/H_M start at M4; full gates remain open.
- [ADR 015](adr/015-packed-lifetime-development-acceptance.md) accepts interrupted
  packed lifetime evidence locally: exit 2 after 4,019.936 s plus short acquisition
  regressions. **User requested no two-hour rerun.** Completed FP32 stress remains
  separate; packed two-hour completion is unclaimed.
- [Native-build refactor repair](validation/2026-10-18-native-style-repair.md)
  restores the PPM budget fixture and Windows-only renamed references, with a
  string-literal regression in the style checker. Native Windows/macOS reruns
  remain due; milestone gates and the M3 next step are unchanged.
- [macOS JSON build repair](validation/2026-10-19-macos-byte-json.md) replaces
  byte-vector JSON input with a bounded character view and rejects raw NUL
  termination. Local libc++ verification is separate from the required native
  macOS workflow rerun, which remains due.
- [Windows fusion JSON repair](validation/2026-10-19-windows-fusion-json.md) removes
  the reported MSVC C2666 overload ambiguity through typed string comparisons.
  Local C++26/C++23 checks pass; native Windows rerun remains unverified.
- [Clang signedness repair](validation/2026-10-19-fusion-components.md) makes mesh
  fingerprint byte conversion explicit, preserving the hash stream. Native
  linux-clang/macOS workflow confirmation remains due.

## Saved reconstructions

Sources: `data/local/`; outputs: `artifacts/<video>/`. Inspect each
`logs/result.json` before another attempt; `request.json`, `source.json` and stage
directories own exact selected inputs and results. Documentation reconciliation
did not restore, delete or rerun data. Both completed splat bundles retain arbitrary
scale; separate partial-mesh inspection below does not enable measured geometry
or walking.

| Video | Current saved result |
|---|---|
| sample_1 | 7k fit, **295,952 splats**; saved terminal/validation and native CLI bundle validation exit 0; Source view available |
| sample_2 | Latest source SHA `10a6405980025eadfbe12e4c68d86bbab8d41af24e0fa48f2d12ca829f0e1de3`; **FAILED**, exit 2 / POSE_DISCONNECTED after **1,137.473 s**; 90 decoded frames, largest connected model **79 (81 required)**; no fit/scene, failed stages retained. [Second replacement](validation/2026-10-16-sample2-second-replacement.md) supersedes the deleted first replacement (75/88, 80 required, 1,254.710 s) |
| sample_3 | Source SHA `4448a1d9eb93f8b7fc787f9ea037d282128438a2512fad8eb8df8544f53906da`; **80 poses → 7k fit → 648,332 splats**, terminal/validation/native bundle checks exit 0. [Viewer/holdout checks](validation/2026-10-15-sample3-evidence.md): 126 frames/zero Vulkan errors, 16/16 scores reproduce; mean **20.8708 dB / 0.815687 SSIM / 0.253078 LPIPS**, Q3 blocked. [All 80 calibrated comparisons](validation/2026-10-15-all-camera-reference.md) pass, maximum one RGB8 level |

On 2026-10-13 the user requested deletion of all old artifacts and three fresh
runs; prior raw jobs/scenes/readbacks/migration inventories no longer exist.
[Historical reset](validation/2026-10-13-fresh-video-reset.md), recovery and graph
reports retain their original sources/attempts, including older sample_3 failures;
they do not describe its completed replacement.

```sh
cat artifacts/sample_1/logs/result.json
cat artifacts/sample_2/logs/result.json
cat artifacts/sample_3/logs/result.json
build/dev/we_viewer --scene artifacts/sample_1/scene.wexplore --source-view --max-splats 1000000
```

Source view is an independent spectator reset, not measured gravity/floor/collision.
For future videos, `python3 tools/new_video_job.py data/local/VIDEO.mp4` creates
`artifacts/VIDEO/request.json` without processing; existing folders are refused.
Use `--name VIDEO_retry_2` for a separate attempt. Full source SHA-256 stays in
`source.json`; [layout](protocols/artifacts.md), source data and outputs stay ignored.

## M3 checkpoint

- [Recovery diagnostic](validation/2026-10-16-m3-recovery-analysis.md): input-bound
  bounded graph inspection/private-copy SQLite WAL reads/exclusive reports.
  sample_2's four-image model is a subset of the 79-image model; missing frames
  50–60 have weak/absent links, and frames 50/60 reference 7/12 map points against
  30 required inliers. No changed recovery run. Pose package `has_cuda=false`
  describes package capability, not GPU absence.
- [Depth admission](validation/2026-10-17-depth-assets.md): axial camera-Z,
  validity/confidence/support/optional uncertainty, exact pose/image/recipe
  dependencies; stale/lost/invalidated poses rejected. Shared I/O moved to core
  with coverage scope preserved.
- [Dense inputs](validation/2026-10-17-dense-inputs.md): checked/pinned native
  grids/poses/recipes, exclusive publication and independent admission reload.
  All 63 sample_1 frames at 4104×2208 prepare without solve/resample; independent
  image/undistortion checks and matching GCC/Clang manifests pass; 303 dev checks.
- [CUDA pilot](validation/2026-10-17-dense-worker.md): isolated `.venv-dense`,
  COLMAP 4.2.1, six pinned/RECORD-checked packages; RTX 4060/driver 615.78.08.
  Known-Z synthetic check and 172.083 s real frames 1/5/9 produce three pose-admitted
  `.wedepth` assets at native grids. Coverage only **0.789% / 1.042% / 2.095%**;
  missing pixels unknown. Cancellation needs SIGKILL after six-second grace;
  partial work has no terminal result and preserves completed pilot. This is
  backend/interchange evidence, not room geometry or Q5/N2.

- [Resized dense inputs](validation/2026-10-18-dense-resize.md): separate explicit
  profile binds native/resized bytes, recipe and per-axis half-pixel calibration.
  All 63 sample_1 frames prepare at 2048×1102 and pass native admission; 6,237
  independent filtered-pixel samples differ by at most one RGB8 level, camera
  rays by 2.22e-16. No depth or backend-model adaptation is produced.

- [Resized neighbor MVS](validation/2026-10-18-dense-neighbors.md): owned 63-view
  model preserves poses/tracks/29,846 landmarks while scaling 441,769 keypoints;
  max reprojection difference 4.55e-13 pixels. The frozen nine-view GPU recipe
  completes in 213.754 s; all nine default-limit depth assets pass pose admission
  and complete-plane export checks. Coverage remains **3.34–9.89%**; no room mesh
  or geometry gate. Sparse-track parallax diagnostic suggests the close temporal
  window limits support. CPU preparation cancellation passes; GPU/job cancellation
  and resume remain unqualified.

- [Parallax-aware planning](validation/2026-10-18-dense-parallax.md): deterministic
  co-visibility/parallax planning, graph closure and 18 native depth admissions
  pass; 30 dense Python / 308 dev / 285 C++23 checks. The **449.453 s** candidate
  loses valid coverage on every unchanged reference: **3.34–9.89% → 2.60–5.36%**,
  **1,537,092 → 790,453** valid observations. It remains experimental, without
  quality promotion. Sparse support is insufficient; cached photometric-depth
  round trips show weaker overlap/consistency. Both runs are retained.

- [Cached visibility/NCC diagnostic](validation/2026-10-18-dense-visibility.md):
  CPU-only pinned cache receipt, owned-byte model/image/depth loading, closed
  six-source rows and disjoint pixel grids. An independent scalar oracle matches
  **14,357 samples**. The candidate improves ranking-grid two-source support
  **107→126**, but separate-grid support falls **84→81**, with five references
  worse. No executable GPU recipe or solve is produced. Sparse-track/depth checks
  show stronger landmark coverage but larger unfiltered-depth disagreement tails
  in the parallax cache; shared SfM evidence is not geometry ground truth.

- [Spatial/texture reliability](validation/2026-10-18-dense-reliability.md):
  12,288 centers/view, complete tile/class denominators and separately pinned
  training/evaluation. Separate-grid support rises **1,501→1,579**, but the first
  two references and high-texture support regress; **317/432** candidate tiles
  have zero support. **96,397/110,592** centers are below the existing texture
  threshold and contribute no support by definition. A same-42-pair comparison
  finds large disagreement in textured regions too. No new GPU recipe/solve;
  47 isolated dense tests and 16,114 independent scalar oracle samples pass.

- [Pose-admitted visibility](validation/2026-10-18-depth-visibility.md): native
  application/CLI exports checked resized-domain depth, RGB and pose dependencies;
  all **27** retained depth assets copy exactly. Final C++26/C++23 replays reproduce
  their corresponding export receipts;
  **311 dev / 288 C++23** checks and focused native ASan/UBSan/LSan pass. The reader
  preserves explicit validity/support/confidence/optional sigma. Same-pair
  analysis retains **474,641 / 496,003 unknown-reference samples of 516,096**;
  conditional depth agreement does not certify complete geometry. No solve,
  source re-ranking or scene update occurs.

- [Observed fusion](validation/2026-10-18-observed-fusion.md): isolated CPU
  confidence-weighted sparse TSDF consumes both native caches without MVS/refits.
  Real raw meshes have **47,559 / 82,248 triangles** but remain visibly fragmented;
  fixed-nine accepted coverage is **2.35–6.54% / 1.53–3.36%**. Different extents and
  view counts prohibit a mesh-count quality comparison. Eight analytic/adverse
  cases, 52 retained dense/reader regressions and 1,024 independent scalar voxel
  checks pass; stopped-child cancellation kills/reaps without a terminal receipt.
  Separate hash-locked Open3D CPU environment leaves existing environments intact.
  No native mesh admission, scene update, measured geometry or walking result.

- [Native fusion admission](validation/2026-10-19-fusion-admission.md): immutable
  uncompressed voxel/mesh interchange, bounded native reader and pose-bound CLI
  inspection admit both retained real outputs. Every voxel plane and mesh array
  matches the originals; arbitrary units, complete coverage, support/weight,
  optional sigma and unknown/front-band distinctions survive. Rehashed malformed
  data and stale/lost pose contexts reject. No scene/collision publication or
  geometry-quality promotion.

- [Observed mesh runtime](validation/2026-10-19-observed-mesh-view.md): full retained
  **32,074/61,598 vertices and 47,559/82,248 triangles** load through immutable
  scene admission into the native normal-color viewer. Both visible 90-frame runs
  and depth/clip/resize/staging/rebase/update diagnostics report zero Vulkan errors.
  Full coverage/source metadata remains pinned; invalidation hides stale geometry.
  No cleanup, baked texture, measured scale, hybrid occlusion or walking result.

- [Component/support analysis](validation/2026-10-19-fusion-components.md): native
  read-only analysis retains all **800/1,864 components**, areas and TSDF stencil
  denominators. Independent original-NPZ graph/area/support checks match every
  component. Every referenced vertex has at least two observed stencil corners;
  partial stencils remain unknown, and size alone justifies no fragment deletion.
  Both full meshes reopen natively with zero Vulkan errors. No cleanup is claimed.

- [Certified local cleanup](validation/2026-10-19-mesh-cleanup.md): independent
  whole-surface certificates validate **618/668** short-edge patches, producing
  **46,323/80,912 triangles** within one quarter voxel of the raw surfaces.
  All **800/1,864 components** and boundary edges remain; raw assets are retained.
  Both native proxy views and update diagnostics pass. This bounded simplifier
  does not certify measured geometry, self-intersection freedom or walking.

- [Native mesh bundles](validation/2026-10-19-mesh-bundles.md): schema 2 persists
  full raw/proxy geometry and exact dependency/certificate text; splat-only
  schema-1 bytes stay identical. `compile-fusion` publishes checked requests to
  new bundles, opened through ordinary native scene updates. All four retained
  meshes round-trip exactly and render with zero Vulkan errors. Snapshot admission
  preserves authoring provenance; it does not replay the original voxel proof.

- [Captured-color projection audit](validation/2026-10-19-color-projection.md):
  all four full raw/proxy scenes produce pinned color/visibility/owner/seam ledgers.
  Independent checks match every one of **2,313,378 samples / 34,036,362 view
  attempts**. Known interior sample fractions are **94.22–96.90% of the partial
  mesh**, not room coverage; maximum shared-edge color jumps **0.562/0.885** remain.
  The contact sheet is diagnostic. Usable textures/export and native textured
  viewing are still pending; no geometry/material/quality gate is promoted.

Saved runs/failures/frozen fixtures and prior dense/resize evidence remain unchanged.
Relinking makes dated runtime receipt current-byte checks exit 2; fresh loader/
mapping/owner observations remain due. Check with
`python3 tools/check_dependency_manifest.py --local-evidence`. Earlier refreshes
passed local consistency; strict closure remained exit 3 for 253 source/rights
gaps. Dense redistribution rights remain open.

- [Native captured-color atlases](validation/2026-10-19-mesh-textures.md) add bounded
  score-weighted linear RGB baking, canonical UV/validity/ownership planes,
  schema-3 persistence and unlit native viewing. The original full raw/proxy
  geometry and certificates remain unchanged; unknown fragments are discarded.
  This is M3 reconstruction fallback work, not A-track PBR/asset qualification.

- [Captured-unlit GLB interchange](validation/2026-10-19-mesh-glb.md) exports all
  four retained native bakes and strictly imports the self-contained producer
  profile. Original geometry/certificates, UVs and validity/ownership survive;
  captured RGB is quantized. Standard bilinear filtering conservatively loses
  about 15% of previously supported sampled interiors. Both full proxy imports
  render natively with zero Vulkan errors; fragmented surfaces remain incomplete.
  General glTF import, GUI export jobs and asset/PBR qualification remain open.

- [Owned retained-cache geometry job](validation/2026-10-19-geometry-job.md): both
  saved native visibility caches pass exact source/pose admission and produce
  isolated real partial-TSDF stages. Replay reuses checked stage bytes; a
  cancelled parallax run returns 130 and resumes without overwriting prior
  assets; foreign occupied output remains unchanged. Both new stages independently
  convert and pass native pose-bound inspection. This Linux development slice
  has no fresh depth solve or owned native scene handoff.

- [Native geometry adoption](validation/2026-10-19-native-geometry-job.md): shared
  application/CLI owns exact terminal conversion, current-pose admission and
  immutable native bundle publication. Both full real meshes reopen and render
  for 90 native frames with zero Vulkan errors. Cancellation after real conversion
  preserves the populated attempt; restart selects a new attempt and strict replay
  skips conversion. Stale pins, corruption, foreign bundles and writer contention
  reject. Fresh depth, solver-to-GUI ownership and geometry gates remain open.

## Next executable step and protection rules

Connect the [native terminal adoption](protocols/fusion.md#native-terminal-adoption-and-scene-publication)
to the owned retained-cache solver and viewer background-job state under plan §8
M3. Begin with an explicit request that runs/reuses `tools/geometry_job.py`, then
calls `adopt_geometry_job` and hands its checked bundle to the existing immutable
scene-update path. Verify cancellation at both child boundaries, source/pose
changes, populated solver-attempt recovery and prior visible-scene preservation.
Retained native-adoption fixtures/commands are in
`artifacts/validation/m3-native-geometry-20261019/`; choose new output names.
Then integrate fresh CUDA depth behind the same boundary after isolated
worker-header reconciliation below. Keep metric alignment/collision/walking open.

The style gate now enforces one first-line `#pragma once` in all headers, braces
around every control-flow body, and the C++ template's non-empty brace-list
spacing. `.clang-format` groups project, standard-library and platform includes;
`.clang-tidy` retains the one conflicting portability-check exclusion. Python
source now follows the template's export ordering and class naming conventions.
Full Python formatting still needs the pinned Black dependency for verification.
See [style conformance validation](validation/2026-10-19-style-conformance.md).
The normal style command no longer needs guarded-copy VFS analysis for header
policy. Separately,
all 24,457 protected files match their previously observed state; 3,982 external
package headers still differ from their original byte receipts.
The libc++ check uses fresh extraction from retained archives, leaving the modified
installation untouched. Reconcile worker-header drift against exact retained
packages in an isolated profile before any future CUDA/worker rebuild; old receipts
are not current. No unchanged fusion/MVS/refit or projection retuning is needed.

Keep extent/512, camera/world units and pose revisions explicit. Preserve weights,
support, sigma availability and the local observed front-band versus unknown
distinction; allocated voxels do not establish free space. No inferred floor,
hole filling, metric collision/walking or geometry-quality promotion. Keep every
fixed reference and backend filter; private photometric maps remain ineligible.
Do not retune on opened evaluation grids or alter existing environments.

Preserve saved runs, failed/decoded stages, both frozen fixtures and the pilot.
Keep CPU pose/training environments unchanged. No unchanged three-view/nine-view/18-view or
sample_2 solve retry, completed refit, long-stress rerun, implicit download or
threshold reduction. Never trim full scenes to obtain a pass. Unified native solver/GUI
job integration, fresh depth, broader mesh processing, compressed storage, measured scale, collision/walking and
general interchange remain open.
A0–A6/AP gates are unstarted; M4 has not begun.

Reference hardware/corpus performance, physical/native platforms, geometry,
broader fuzzing, distribution/model/dataset rights and full gates remain pending
in [requirements](requirements.md). Timestamp sampling/blur/cut/lens/coverage and
metadata/storage diagnostics retain their recorded limits; unknown/inadequate
storage fails, and metadata inspection still applies decoded-storage estimates.
Hosted CPU success is separate from viewer/release qualification; Windows
cancellation-clock repair still needs native confirmation. Sandbox ICD failure
is not GPU absence. Local commits are authorized; pushes/uploads are not.
Follow [documentation rules](protocols/documentation.md).
