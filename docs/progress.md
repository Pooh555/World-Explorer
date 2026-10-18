# Current handoff

Updated 2026-10-18 (Asia/Hong_Kong). **M1: VERIFIED (Linux development exit);
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

## Saved reconstructions

Sources: `data/local/`; outputs: `artifacts/<video>/`. Inspect each
`logs/result.json` before another attempt; `request.json`, `source.json` and stage
directories own exact selected inputs and results. Documentation reconciliation
did not restore, delete or rerun data. Both completed scenes retain arbitrary
scale; geometry/walking remain unavailable.

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

Saved runs/failures/frozen fixtures and prior dense/resize evidence remain unchanged.
Relinking makes dated runtime receipt current-byte checks exit 2; fresh loader/
mapping/owner observations remain due. Check with
`python3 tools/check_dependency_manifest.py --local-evidence`. Earlier refreshes
passed local consistency; strict closure remained exit 3 for 253 source/rights
gaps. Dense redistribution rights remain open.

## Next executable step and protection rules

Implement a **pose-admitted geometric-depth visibility cache** before another
source-ranking change. Read `artifacts/validation/m3-dense-reliability-20261018/`
`training.json`, `evaluation.json`, `cache-comparison.json` and `summary.json`.
The denser objective improves an aggregate but harms two fixed references; raw
photometric disagreement persists in textured regions. Do not tune on the opened
evaluation grid or treat flat-by-policy samples as proven unreconstructable.

Start from `load_pose_bound_depth` in `application/depth_assets.hpp` and the
retained nine/18-view `.wedepth` assets. Add a native checked export/receipt for
visibility analysis: default-limit pose/image/calibration/recipe admission,
immutable depth/validity/support/confidence bytes and exact dependencies. Compare
the same fixed nine references and camera pairs, keep unknown samples in every
tile/class denominator, and freeze new disjoint reference grids before any future
ranking experiment. Unfiltered private maps must never be promoted into admitted
geometry. Use the resulting supported observations to prepare real fusion work;
partial coverage and arbitrary scale still cannot certify Q5/N2 or walking.
Keep source-graph closure and backend filters; no retry or failed-reference
exclusion is justified by this candidate. See
[diagnostic instructions](../workers/dense/README.md#spatial-and-texture-reliability-diagnostic).

Preserve saved runs, failed/decoded stages, both frozen fixtures and the pilot.
Keep CPU pose/training environments unchanged. No unchanged three-view/nine-view/18-view or
sample_2 solve retry, completed refit, long-stress rerun, implicit download or
threshold reduction. Never trim full scenes to obtain a pass. Production
job/protocol integration needs bounded cancellation/kill/reap; resume, real fusion,
compressed storage, measured scale, collision/walking and mesh export remain open.
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
