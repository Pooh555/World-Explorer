# Current handoff

Updated 2026-10-25 (Asia/Hong_Kong). **M1: VERIFIED (Linux development exit);
M2: VERIFIED (Linux/NVIDIA development exit); M3: VERIFIED (local software exit).** No full Q/P/N/E
gate is certified. **A0/AP0: IN_PROGRESS (CPU contracts, typed metadata and owned history).** A1–A6/AP1–AP9:
NOT_STARTED; M7–M8: DEFERRED_BY_USER.

[ADR 020](adr/020-m3-development-exit.md) establishes an M3 local development
exit that does not require an independent measurement for internet-sourced
`sample_1`. This enables the M3→A0–A5 sequence after the software checklist is
reviewed; it does not certify Q5/N2, establish real metric scale, or change A1's
own independent-scale pilot requirements. The [completed review](validation/2026-10-23-m3-development-exit.md) now supports
that local handoff; all full gates remain open.

[Build/style/inventory repair](validation/2026-10-20-native-inventory-repair.md)
separates the non-Linux geometry fallback from its solver body, removes all 24
Black failures without AST changes, and refreshes loader/import/viewer/owner
receipts. Those checks remain historical. The
[23 October selected inventory refresh](validation/2026-10-23-m3-dependency-refresh.md)
passed default/local checks against fresh loader, backend/viewer mapping and
owner receipts. It retains the Jolt inventory's 257 source/rights gaps across
86 components/seven profiles. Exact workspace LLVM 23.1.1 format/tidy and pinned
Black checks pass without changing global compilers or worker environments.
Use `artifacts/tools/llvm-official-23.1.1/bin` for the exact C++ style tools.
The earlier full Windows log confirms C4702/C2220 unreachable-code failures.
Local non-Linux branch checks pass; native Windows rerun remains pending.
The 21 October Windows excerpt compiles the previously failing source after the
[enum-conformance repair](validation/2026-10-21-windows-jolt-enums.md), then
reports 95 CRT link mismatches: Jolt `/MTd` versus tests `/MDd`. The
[runtime alignment](validation/2026-10-21-windows-jolt-crt.md) follows the parent
CRT setting; full native Windows build/test confirmation remains pending.
The [24 October quality-policy repair](validation/2026-10-24-windows-quality-json.md)
addresses the newly supplied MSVC C2666 JSON/string-view comparison failure.
C++26/C++23 builds and 21 decoded-admission cases pass locally. The subsequent
[decode-coordinator repair](validation/2026-10-24-windows-decode-json.md) addresses
the same overload ambiguity in `video_decode.cpp`; the supplied log compiles the
previously repaired reader and asset-catalog objects before this new failure.
Full native Windows build/test confirmation remains due. Relinking makes the saved runtime payload
receipt stale: default inventory passes, `--local-evidence` exits 2. Fresh runtime
observation is required before claiming current local inventory consistency.

The [public-header repair](validation/2026-10-24-native-span-includes.md) addresses
the supplied GCC 13/MSVC missing-`std::span` failures with direct includes in four
headers and a Python-only public-header CTest guard. Local C++26/C++23 builds and
598/573 CPU tests pass; native Windows/macOS/GCC 13 CI confirmation remains due.
The subsequent [Windows record-store log and repair](validation/2026-10-24-windows-record-lock.md)
reaches 568/569 CTest passes, including the header guard. Its one failure hashes
the exclusively held writer lock. The test now takes full byte snapshots before
acquisition and after release, checks `WOULD_BLOCK` while held and successful
retry afterward. Local suites pass; a complete native Windows rerun remains due.

The supplied [macOS draft-test failure](validation/2026-10-25-macos-draft-json.md)
instantiates undefined libc++ byte character traits in an added JSON test.
An explicit bounded character view repairs the test without changing its
assertions or production contracts. Local libc++ reproduction and GNU focused
checks pass; a native macOS rerun remains due. This build repair preserves the
current A0 asset-definition edit handoff below.

[ADR 019](adr/019-assets-before-platforms.md) orders M3 → A0–A5 → M4 → M5 → M6,
then A6 engine packaging. The [asset track](technical_plan.md#11-offline-asset-production-track)
adds reusable objects/structure, labeled completion, PBR and shared glTF/GLB
assembly: first visible end-to-end in A1, locally validated through A5.
[Research](research/offline_asset_pipeline.md) and
[rescheduling](validation/2026-10-17-assets-before-platforms.md) qualify no capability;
[original planning](validation/2026-10-17-offline-asset-planning.md) retains its
superseded after-M6 schedule. Those tasks installed/processed/uploaded nothing.
M1–M6 order/thresholds remain; native/reference release evidence is due by M6.

## Current A0 work

[Asset dependency admission](validation/2026-10-23-asset-dependencies.md) validates
a bounded closed DAG, exact revision/context, canonical identity and transitive
manual ancestry. The [byte-receipt slice](validation/2026-10-24-asset-receipts.md)
adds strict file admission into privately owned snapshots and immutable owned
publication, unchanged replay, cancellation and controlled process-crash recovery.
The [recipe-lock slice](validation/2026-10-24-asset-recipes.md) now inspects strict
recipe fields, exact dependency/source bindings and resource/rights declarations
from those owned bytes. Unknown installation costs, local restrictions, uncleared
distribution and insufficient/contract-only hosts remain explicit blockers.
The [component-evidence slice](validation/2026-10-24-asset-component-evidence.md)
now binds bounded inventory/file/archive/config/prompt/terms bytes and independently
selected review records from a second snapshot. Self-selection cannot clear a
review blocker. The [installed-tree slice](validation/2026-10-24-asset-installed-components.md)
now reconciles explicitly selected disjoint portable roots with exact closed
inventories, two bounded hash scans and caller-data preservation. Runtime closure,
archive correspondence, legal authority and measured/enforced worker limits
remain unverified; successful observations retain explicit blockers. The
[protocol](protocols/asset_installed_components.md) defines the cooperative-filesystem
scope and forbids treating results as execution tokens.
The [region-provenance slice](validation/2026-10-24-asset-region-provenance.md)
now admits bounded complete face/texel partitions, exact source-view bindings,
separate evidence/confidence and graph-bound declared manual lineage. Source
correspondence and actual human actions remain unverified; the
[protocol](protocols/asset_region_provenance.md) records these limits. No mutable
catalog, worker, asset production, completion/PBR output or viewer feature is
implemented by those slices. The [typed asset/instance slice](validation/2026-10-24-asset-schema.md)
now admits reusable immutable definition handles and bounded instance sets, with
exact references, pivot/unit/material declarations, provenance and manual lineage.
The [protocol](protocols/asset_definitions.md) distinguishes metadata admission
from payload/rights/scale qualification and persistent revision ownership.

The [metadata I/O slice](validation/2026-10-24-asset-metadata-io.md) adds strict
canonical byte decoders, independently selected receipt/provenance bindings and
bounded owned publication. Durable operation intents, immutable blobs and a
manifest published last preserve committed revisions through matching retry,
cancellation and a controlled process crash. Per-directory semantic ID/revision
conflicts are rejected. The [protocol](protocols/asset_metadata_io.md) defines
ownership, budgets and recovery scope; no live scene update or payload qualification
is implied.

The [object-observation slice](validation/2026-10-24-object-observations.md) now
binds strict observation/mask records to independently selected image and actual
native pose-asset bytes. It validates current frame/calibration/pose revisions,
contained integer-center grids, complete visibility partitions, hypotheses and
accepted/rejected evidence. Rejected dependencies retain manual ancestry; no
segmentation, verified tracking or map transaction is implied. The
[protocol](protocols/object_observations.md) owns fields and bounds.

The [capture-selection slice](validation/2026-10-24-capture-selections.md) now
binds independently selected source/view/role/annotation tables to owned receipt
and actual pose bytes. A separate inference-only handle excludes evaluation
images/labels and complete receipts/pose assets; known upstream aliases and
unselected/held-out pose-fit observations are rejected. Immutable publication
retains exact bytes and semantic revision ownership. The
[protocol](protocols/capture_selections.md) scopes this to selection/dependency
conformance, not a sealed real corpus or verified consent/scale/fit history.

The [manual-action journal slice](validation/2026-10-25-manual-actions.md) now
records bounded declared actions through a shared native application command,
with exact receipt/target/context binding, immutable retry/crash recovery and
separate setup/evaluation/assistance time sums. `we_process inspect-actions`
reads independently pinned history. The [protocol](protocols/manual_actions.md)
keeps actor/time/authenticity limits explicit; its inference helper rejects
setup/evaluation actions. A declaration alone does not authorize an edit.

The [draft replacement slice](validation/2026-10-25-draft-mutations.md) now binds
selected assistance to exact before/after receipts, product revisions and journal
pins through the shared application command. It requires new manually descended
revisions across the affected closure, protects all input/journal directories and
publishes separate durable intent/result phases. Prepared interruptions expose
no outputs; exact retries recover. The [protocol](protocols/draft_mutations.md)
keeps bodies opaque and limits enforcement to this new path, not legacy admission.

The [typed mask-edit slice](validation/2026-10-25-object-mask-edits.md) now
validates both native observation/mask versions and independently selected capture
roles before any intent. Exact action-bound committed results produce owned
inference masks with derived manual ancestry; pending/invalid/evaluation inputs
cannot create them. Role-manifest and observation pins are bound in versioned
intent identity. The [protocol](protocols/object_mask_edits.md) keeps this separate
from worker execution and geometry/material descendant qualification.

The [attempt-outcome slice](validation/2026-10-25-draft-attempts.md) now adds
separate owned pending/failed/cancelled/committed attempt history, exact replay
and controlled crash recovery around generic and typed mask commands. A new
attempt can resume the same mutation while preserving the old cancellation;
physical outcome decisions recover before work reruns, even without a staged
blob. The [protocol](protocols/draft_attempts.md) separates historical publication
from current typed adoption and product/automatic-yield accounting.

The [typed asset-edit slice](validation/2026-10-25-asset-definition-edits.md) now
validates generated triangle-mesh and constant PBR payloads for both edit kinds,
with exact bounds/regions, semantic change checks and derived manual ancestry.
Explicit v3 intents attach before/after definitions outside their input receipts
and commit them atomically with replaced products, avoiding a receipt hash cycle.
Owned typed reads require the committed draft; attempt history retains failures
and cancellation. The [protocol](protocols/asset_definition_edits.md) limits this
to the generated static profile, without worker/render/export or real AP evidence.

The [sealed-corpus slice](validation/2026-10-25-evaluation-corpus.md) now binds
independently selected committed capture manifests and candidate recipe/resource/
rights pins in a canonical immutable seal. Exact selected reads, replay and staged
cancellation recovery are checked on synthetic fixtures. Missing evidence and
opaque supplied refs remain explicit blockers; no real corpus or measurement is
qualified. The [protocol](protocols/evaluation_corpus.md) preserves the capture-role
firewall and forbids using a seal as an execution or AP-pass token.

The [typed measurement slice](validation/2026-10-25-capture-measurements.md) now
binds selected inference view/calibration, original capture source and exact native
scene geometry to the existing world-frame anchor reader. Owned metadata has
atomic two-record replay and exact committed reads. Explicit corpus schema 2
requires matching private scale handles; schema 1 keeps its original bytes.
Synthetic/physical-authenticity/correspondence blockers remain; no scene/map scale
is changed. The [protocol](protocols/capture_measurements.md) owns its bounds.

The [typed inventory slice](validation/2026-10-25-evaluation-inventory.md) now
binds required object/structure tracks to complete per-evaluation-view tables and
admitted annotation/mask counts. Known content/recipe/action separation checks
protect selected inference closures. Owned two-record publication/replay and
corpus schema 3 require exact selected pins/handles; schemas 1/2 retain their bytes.
Labels, physical visibility and inventory completeness remain unverified. The
[protocol](protocols/evaluation_inventory.md) owns the contract and limits.

The [typed temporal/path slice](validation/2026-10-25-evaluation-paths.md) now binds
complete ordered role blocks, native source timestamps/world camera values and a
separate-source held-out path with declared uncertainty. Atomic two-record recovery
and corpus schema 4 require matching selected refs/handles; schemas 1–3 retain their
bytes. Physical accuracy, sampling novelty, independence and pre-tuning provenance
remain unverified. The [protocol](protocols/evaluation_paths.md) owns this scope.

Next executable step: implement typed controlled-reference evidence for backside,
empty-background and changed-light captures (plan §11.6). Bind independently selected
conditions/reference pairs to exact evaluation roles, original source/view/pose refs
and declared lighting/measurement units; reject stale/missing/foreign references and
inference leakage on synthetic admission/recovery fixtures. Keep physical lighting,
background correspondence and real AP4/AP6 evaluation unverified. Preserve earlier
corpus encodings when adding explicit typed enforcement. Available selected local
sample receipts have no A1 consent/measurement/background/light bindings; preserve
them and request the real pilot inputs when required. Broader worker/asset adoption
and complete real capture/evaluation qualification remain A0 work. A declaration cannot certify a human
act, consent, correct opaque descendants or automatic yield. Keep processing local
and draft metadata separate from live scene transactions; validate actual payloads
before runtime adoption through the existing update path. Current runtime evidence
still needs recapture; installed observations do not establish runtime closure or legal rights.
Unknown installation/model costs must refuse provisioning. Bounded tree checks
do not complete AP0. Full installation/runtime qualification, measured budgets
and enforced worker limits are still required. The 8 GiB host remains contracts-only; H_AP24/H_AP48
are proposed profiles, and unknown model/environment sizes block provisioning.
No model installation, inference or implicit download occurs. A1 still needs its
own consented independently scaled real pilot, held-out/empty-background and
changed-light evidence, actual native completed/PBR output and Godot import.

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
The [20 October runtime refresh](validation/2026-10-20-native-inventory-repair.md)
passed the local inventory check for its selected build. The new depth-sequence
rebuild changes native payload bytes: the current local check exits 2 until fresh
runtime receipts are captured. The prior strict check retains 253 source/rights
gaps; this rebuild qualifies no distribution closure. Dense/fusion profiles
remain separately evidenced; this selected seven-profile inventory does not
qualify their redistribution rights or the drifted installed headers.

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
  reject. Fresh depth and geometry gates remain open; unified solver-to-GUI
  ownership is now covered below.

- [Unified retained-cache geometry job](validation/2026-10-19-geometry-pipeline.md):
  shared application/CLI and viewer background controls own solver/reuse,
  conversion, pose admission and immutable scene handoff. Both full real meshes
  render with zero Vulkan errors. Real stopped-child cancellation reaps all
  descendants; populated solver/conversion attempts survive resume. Native prior
  pixels remain identical after failure/cancel. No fresh depth or geometry gate.

- [Owned fresh CUDA depth](validation/2026-10-20-owned-dense.md): separate clean
  six-wheel profile verifies all 1,188 listed files; original environments remain
  unchanged. Shared GUI/CLI now owns fresh depth → native cache admission → fusion
  → scene handoff. Active CUDA Cancel returns 130 in 3.241 s and retains 23 files;
  resume completes in 241.571 s, including 210.576 s MVS; exact replay launches no
  CUDA solve. The full 47,559-triangle partial mesh renders with zero Vulkan errors.
  Validity remains 3.34–9.89%; no geometry/scale/walking gate is promoted.

- [Bounded full-sequence depth](validation/2026-10-20-depth-sequence.md): all
  **63** pinned sample_1 reference grids pass seven owned CUDA batches and four
  independently bounded native caches. Cancellation/reuse, complete coverage
  accounting, streaming and full-volume ASan/UBSan/LSan checks pass; the normal
  and instrumented receipts match exactly. Valid observations total **15.9265%**
  of the grids, with all unknowns retained. That depth-only slice promoted no measured
  geometry/scale/walking or quality gate.

- [Full-sequence fusion](validation/2026-10-20-sequence-fusion.md): all **63** real
  frames feed one global extent and confidence-weighted TSDF, with all-source
  cross-cache agreement/occlusion checks. The retained shared GUI/CLI path passes
  stopped-child cancellation, preserved-attempt resume, exact replay and native
  scene handoff. All **39,683 vertices / 49,833 triangles** render with zero Vulkan
  errors, but remain visibly fragmented. Complete unknown/support/sigma accounting
  survives native conversion; scale remains arbitrary and walking unavailable.

- [Fresh sequence geometry adapter](validation/2026-10-20-fresh-sequence.md):
  shared GUI/CLI validates the complete owned depth graph and all support halos,
  exports each reference once, then reuses the sequence fusion/native path.
  Real 63-frame depth reuse, populated cache cancellation/resume, exact replay,
  native prior-pixel preservation and full-volume sanitizers pass. All 49,833
  triangles render with zero Vulkan errors; 388 dev / 364 C++23 checks pass.
  No repeated CUDA solve or geometry-quality promotion is claimed.

- [Sequence captured-color audit](validation/2026-10-20-color-sequence.md): complete
  63-view cache admission and versioned 120-candidate evidence retain all original
  geometry. Independent checks reproduce every one of 448,497 samples / 28,255,311
  view attempts, with exact masks, owners and RGB. Real cancellation, repeat,
  legacy byte preservation and full sanitizers pass; 393 dev / 369 C++23 checks
  pass. Conditional color support is 97.321% on the partial mesh; large seam jumps
  remain. This diagnostic precedes the sequence baking slice below.

- [Sequence captured-color baking](validation/2026-10-20-texture-sequence.md):
  explicit 120-view appearance, schema-4 persistence and real 63-view native
  viewing preserve all original geometry. The independent oracle checks every
  1,793,988 surface sample / 113,021,244 view attempts; masks/owners match exactly,
  maximum RGBA error is 9.313e-10 and known shared-edge lattice colors match.
  Native viewer/GPU diagnostics report zero Vulkan errors; full sanitizer bake
  reproduces every bundle byte. 399 dev / 375 C++23 checks pass after advancing
  two future-version rejection fixtures to schema 5. Legacy bundle bytes remain
  exact. Conditional lattice support is 98.1173%; the room remains fragmented.

- [Sequence captured-unlit GLB](validation/2026-10-20-glb-sequence.md): explicit
  V2 preserves all 120 candidate bits and uint64 identities, including mixed
  legacy scenes. Full 63-view export/import and independent geometry/PNG/mask/UV
  checks pass; four legacy exports/manifests/imports remain byte-identical.
  Native imported viewing and GPU diagnostics report zero Vulkan errors; full
  sanitizer export/import reproduce all bytes. Conservative bilinear masking
  loses 14.527% of sampled native-known interiors and adds none; captured surfaces
  remain fragmented. 403 dev / 379 C++23 checks and 58 focused sanitizer cases /
  109,177 assertions pass. This completes the narrow sequence interchange slice.

- [Owned mesh postprocessing](validation/2026-10-20-mesh-job.md): the shared
  geometry entry now owns versioned cleanup/native/bake/GLB stages, exact saved-bake
  reuse and immutable retries. Real 63-view cancellation/resume/replay preserves
  completed stages and all source data. The native viewer hands off from raw to
  cleaned/baked geometry with zero Vulkan errors; 1,625 fragments remain. Independent
  portable checks, sanitizer replay/reuse parity, 409 dev / 385 C++23 checks and
  43 sanitizer cases / 96,352 assertions pass. This closes the narrow owned-stage
  integration slice, not M3's measured geometry/walking or quality requirements.

## Next executable step and protection rules

[Source-bound inclined support](validation/2026-10-21-collision-ramp.md) now
extends the [shared capsule session](validation/2026-10-21-capsule-session.md)
with one explicit synthetic plane, plane-relative free/occupied/unknown prisms,
strict 45° support and private Jolt tangent movement/support sweeps. Independent
capsule-envelope checks, native wall sliding/unknown blocking/45° ascent,
focus/dialog pauses and terminal correction/stop pass. V1/V2 fixture and
inspection bytes remain identical. Measured walking stays disabled; steps and
transitions between planes are outside the current profile.

[Selected step clearance](validation/2026-10-22-step-certificate.md) now checks
explicitly opted-in V4 static/same-landing capsule envelopes and a conservative
five-segment lift/cross/descent certificate against selected floor tiles and explicit
free/occupied/unknown cells. Exact 0.30-bounded geometry and just-above
rejection remain from [V4 admission](validation/2026-10-21-collision-step-admission.md).
Legacy V4 documents retain geometry-only behavior. The certificate is not a
per-tick Jolt path: V4 controller creation and shared CLI/native capsule
sessions remain blocked. V1–V3 and the arbitrary-scale real
scene retain prior behavior. No measured geometry or walking gate advances.

[Bounded Jolt step proposals](validation/2026-10-22-step-proposals.md) now use
the shared private controller update and retain raw poses plus both probe
results without returning an active V4 owner. The former 0.02 minimum forward
stair distance exceeded the tick budget; it is now zero. Saved 3-unit/s
ascent/descent proposals reach the other landing through rejected rounded-cap
corner poses, while 0.3-unit/s ascent stalls at the riser. Native proposal
markers are synthetic diagnostics, not accepted movement.

[Capsule/corner motion proof](validation/2026-10-22-step-motion-proof.md) now
checks straight capsule sweeps against the selected step solid with bounded,
outward-rounded intervals and clipped-cell checks. Independent chord and
piecewise-quadratic oracles verify it. The saved diagnostic has
781 clear and 17 unsupported segments; both high-speed riser-cutting chords
are rejected. These are geometric certificates, not accepted session movement.
Legacy queries/fixture bytes and V4 controller rejection are retained.

[Owned transition cursors](validation/2026-10-23-step-cursor.md) now retain a
complete five-leg clearance certificate and advance single-leg subsegments at
bounded 120 Hz path speed. Slow ascent/descent reach both landings, including
certified airborne intermediate poses. Pauses, reversal, cancellation and stale
source checks preserve phase. This is a separate geometric diagnostic, not an
active Jolt controller; V4 creation and shared sessions remain blocked.

[Controller/clock integration](validation/2026-10-23-step-controller.md) and
[native selected-step input](validation/2026-10-23-step-native.md) now pass on
local Linux/NVIDIA. The same controller/session retains certified phase and
clock through slow ascent, descent, airborne Escape/dialog pauses, resume and
terminal Stop. Occupied corners block while ticks advance. All 12 native input
checks pass with zero Vulkan errors; shared CLI/native startup rejects short
unsupported spawns and mixed-profile schemas. The earlier locked-desktop
attempts remain failed historical evidence. This closes selected synthetic-step
acceptance, not measured walking or general mixed-surface transitions.

The current viewer also reopens the retained real captured-color result with
all 39,373 vertices/49,213 triangles and zero Vulkan errors. Its manifest is
unchanged; the inspected output remains fragmented, arbitrary-scale and
unwalkable. No solve, fusion, bake or export is rerun.

[Persistent candidate tracking](validation/2026-10-23-frame-tracking.md) retains
its 24/63 selection and completed 24/24 guided-pose map. The new
[calibration preflight](validation/2026-10-23-calibration-preflight.md) adds an
explicit v3 profile: coherent scale changes are ambiguous between zoom and
camera/scene motion, so an alarm refuses the single-calibration decode. Synthetic
zoom/forward-motion ambiguity, gradual zoom, native accept/restart/reject and
rehashed native history checks pass. Timestamp sampling remains the default;
v1/v2 policy digests and behavior remain supported.

Real sample_1 v3 **rejects**: 23 ambiguous-scale observations, 34 intervals with
insufficient support, four assessed and two unassessed. All 63 v2 decisions and
thumbnail bytes are identical; its 24 frames are provisional in the failed v3
attempt. The ledger is retained and no new RGB completion or pose solve occurs.
This is not confirmed zoom or fixed-intrinsics certification. Automatic lens
segmentation is unsupported. All 378 protected media/result files are unchanged.
The retained map stays arbitrary-scale with geometry unavailable. The [five-item review](validation/2026-10-23-m3-development-exit.md) closes M3
for the local software profile; A0 contract work now follows above,
and M4 has not begun.

The M3 review hands off to the A0 work above; it does not close measured or
asset gates. Current runtime default/local checks pass, with 257 source/rights
gaps retained. The M3 implementation was checked at 492/492 development tests,
467/467 explicit C++23 and seven native sanitizer cases/18,567 assertions;
A0's newer CPU checks are recorded separately above.

Reuse `artifacts/validation/m3-frame-tracking-20261023/guided-recovery.map`
with `prepare-poses`; its durable reopen preserves all four asset/manifest files.
Do not rerun the completed solve or spend another recovery on the unchanged
24-frame attempt. Preserve failed sequential models and every earlier ledger.
Do not tune frozen v3 thresholds to force sample_1 admission. No unchanged fit,
dense, fusion, bake or sample_2 retry is needed. All **5,184** retained real cells
remain unknown; `FRONT_DIAGNOSTIC` is not clearance. Quality, measured
geometry/walking and asset/platform gates remain open. ADR 020 permits a local
M3 software exit without sample_1's unavailable independent distance/up
reference; Q5/N2 and A1 scale evidence stay open.

[Typed depth-space admission](validation/2026-10-20-depth-space-admission.md) now
reloads the exact terminal/ledger/native bundle into an immutable owner, checks
full source/pose/recipe/frame identities and all cell/count relationships, and
rejects stale context or changed native evidence. Shared CLI and owned-thread
viewer opens use this same admission. Complete retained real reload, rehashed
false-front rejection, native viewing and sanitizer checks preserve every source
and every original diagnostic byte. M3, Q5/N2 and the asset/platform sequence
remain open.

[Observed-depth space evidence](validation/2026-10-20-depth-space.md) now streams
all **63** retained depth/RGB/pose-bound views into a versioned whole-cell ledger.
The independent CPU oracle reproduces all **326,592** relations and identifies
**2,217** center-only front tests lacking complete-cell support. All 5,184 unknown
cells and all raw mesh geometry render natively with zero Vulkan errors. Exact
repeat and full-volume sanitizer parity preserve every output byte. This closes
the diagnostic-production slice, not real free-space, Q5/N2 or M3.

[Synthetic support admission](validation/2026-10-20-collision-support.md) now pins
scene/frame/mesh/pose dependencies, complete free/occupied/unknown cells and
conservative flat-floor tiles. Continuous capsule-envelope probes block on
unknown, thin barriers, unsupported seams and stale context. The shared CLI
publishes a labeled ordinary viewer diagnostic; it never activates collision.
The real retained mesh remains arbitrary, with walking disabled. All source
assets and the repeated captured GLB remain unchanged.

[World-frame admission](validation/2026-10-20-world-frame.md) now implements exact
source/anchor-bound distance/up declarations, atomic epoch/revision changes,
schema-5 persistence, consumer transforms and shared CLI publication. Native
texture/splat parity passes within one RGB8 level; the full retained real mesh
opens with zero Vulkan errors and its GLB reproduces exactly. Synthetic alignment
stays arbitrary; a legacy metric flag is unproven. Aligned sources are immutable:
pose corrections require newer full-snapshot resync and new declarations.

The user has **no independent sample_1 distance/up reference**. This does not
block the local M3 development exit under [ADR 020](adr/020-m3-development-exit.md).
Keep that scene arbitrary and walking disabled. Continue software contracts with
labeled synthetic evidence; real measured validation additionally needs
independently measured identifiable points, an up reference and observed-surface
ground truth.
Do not infer scale/gravity/support from reconstructed floors or promote this
fragmented mesh to walkable. Keep the captured GLB profiles restricted until a
separate metric profile is implemented and validated. Platform qualification
remains after local M3 and A0–A5 work.

Use the saved raw sequence and original bake for arbitrary-scale regressions.
The owned processed result is
`artifacts/validation/m3-mesh-job-20261020/full-job/stages/bake-1/output` (manifest
SHA `0b873804a3d5aa2a02c95245a43c550ed4d2ba2240d3af1e4325e23c198c6ee2`).
Inspect that slice's `real.json`, `real-products.json`, `native.json`, `oracle.json`,
`sanitizer-parity.json`, `finish.json` and final preservation/source identities.
The full request SHA is
`66d331dd4b217b3f7284046650804f2d750fbb36d7f41aa442b53a900ebc4e2c`;
`run-geometry` replays completed stages without another solve or bake. Retain the
original bake for exports; lossy portable imports cannot be re-exported. Reserve
new outputs. No unchanged CUDA, fusion, completed bake or refit is needed.

Preserve the 916-file copied/original depth jobs, 48-file cancelled fresh cache,
152 source input files, native raw/baked/sanitizer scenes, all prior attempts,
scratch, caches, bakes, audits and owned postprocessing jobs.
Measured alignment/collision/walking, crash qualification and quality remain open;
A0 contract work is IN_PROGRESS; A1–A6 and M4 have not begun.

The style gate now enforces one first-line `#pragma once` in all headers, braces
around every control-flow body, and the C++ template's non-empty brace-list
spacing. `.clang-format` groups project, standard-library and platform includes;
`.clang-tidy` retains the one conflicting portability-check exclusion. Python
source now follows the template's export ordering and class naming conventions.
Pinned Black 26.5.1 now passes the full required command after formatting the
24 baseline failures; all 24 ASTs are unchanged. Historical failures remain in the
[geometry-pipeline evidence](validation/2026-10-19-geometry-pipeline.md).
See [style conformance validation](validation/2026-10-19-style-conformance.md).
The normal style command no longer needs guarded-copy VFS analysis for header
policy. Separately,
all 24,457 protected files match their previously observed state; 3,982 external
package headers still differ from their original byte receipts.
The libc++ check uses fresh extraction from retained archives, leaving the modified
installation untouched. The dense owner now selects the independently verified
clean profile above; old installation receipts remain non-current. Other worker rebuilds still require
separate isolated reconciliation against retained packages. No unchanged
fusion/MVS/refit or projection retuning is needed.

Keep extent/512, camera/world units and pose revisions explicit. Preserve weights,
support, sigma availability and the local observed front-band versus unknown
distinction; allocated voxels do not establish free space. No inferred floor,
hole filling, metric collision/walking or geometry-quality promotion. Keep every
fixed reference and backend filter; private photometric maps remain ineligible.
Do not retune on opened evaluation grids or alter existing environments.

Preserve saved runs, failed/decoded stages, both frozen fixtures and the pilot.
Keep CPU pose/training environments unchanged. No unchanged three-view/nine-view/18-view or
sample_2 solve retry, completed refit, long-stress rerun, implicit download or
threshold reduction. Never trim full scenes to obtain a pass. Complete room
surface coverage, broader owned mesh processing, compressed storage,
measured scale, collision/walking and general interchange remain open.
A0/AP0 contract work is IN_PROGRESS; A1–A6/AP1–AP9 and M4 remain unstarted.

Reference hardware/corpus performance, physical/native platforms, geometry,
broader fuzzing, distribution/model/dataset rights and full gates remain pending
in [requirements](requirements.md). Timestamp sampling/blur/cut/lens/coverage and
metadata/storage diagnostics retain their recorded limits; unknown/inadequate
storage fails, and metadata inspection still applies decoded-storage estimates.
Hosted CPU success is separate from viewer/release qualification; Windows
cancellation-clock repair still needs native confirmation. Sandbox ICD failure
is not GPU absence. Local commits are authorized; pushes/uploads are not.
Follow [documentation rules](protocols/documentation.md).
