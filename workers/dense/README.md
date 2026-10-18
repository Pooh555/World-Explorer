# Isolated COLMAP CUDA development pilot

This is a **partial three-view qualification tool**, outside the application job
pipeline. It does not publish a scene, fuse geometry, establish measured scale,
qualify Q5/N2, or enable walking. Saved poses, fits and scenes remain inputs only.
The [native-input contract](../../docs/protocols/dense_inputs.md) owns preparation;
[depth interchange](../../docs/protocols/depth.md) owns project output semantics.

The local Linux x86_64 / CPython 3.12.15 profile uses official
[COLMAP CUDA wheels](https://colmap.github.io/pycolmap/index.html). Exact six-package
versions are in `environment.txt`; `environment-lock.txt` pins accepted installer
archive hashes. The [selected development receipt](../identity/dense-development-20261017.json)
binds the actual selected archives and RECORD-checked installed bytes. The
CUDA distribution occupies a separate `.venv-dense`; never install it over the
CPU `.venv-worker` or gsplat `.venv-train` environments.

```sh
UV_CACHE_DIR=.deps/uv-cache uv venv --python .venv-worker/bin/python .venv-dense
# Explicit dependency installation; requires access to pinned public packages.
UV_CACHE_DIR=.deps/uv-cache uv pip install --python .venv-dense/bin/python \
  --require-hashes --only-binary :all: -r workers/dense/environment-lock.txt
```

The checked run used offline installation with
`--no-index --find-links .deps/dense-wheels-20261017` after verifying each selected
wheel against official PyPI metadata and the lock. No weights are required.
Core COLMAP BSD terms do not clear bundled/transitive libraries or CUDA runtime
redistribution. Archive/notice identification is not complete rights/SBOM
qualification; this profile is not an approved distributable package.

## Pinned pilot

The recipe is a versioned JSON object with the exact fields validated by
`pilot.validate_recipe`. Its complete PatchMatch dictionary must match installed
4.2.1 defaults with only these operational settings: GPU 0, native grid
(`max_image_size=-1`), 1 GiB cache setting, two I/O threads, and consistency-graph
writing. Filtering/geometric consistency remain enabled, with two consistent
source views required. No implicit fallback or download occurs.

The recipe also binds `environment.txt` and `pilot.worker_digest()` (the canonical
hash map of both Python implementation files). It explicitly declares
`PARTIAL_THREE_VIEW_QUALIFICATION` and selects indices **[0,4,8]** from the ordered
native manifest. Its confidence policy is consistent-source fraction, **not
probability**; standard deviation remains unavailable. Produce a new exact recipe
and native manifest for a different worker/configuration. The local report retains
a concrete recipe; there is no generic unpinned “latest” launch.

First run the native application's `we_process prepare-dense` with that full recipe
and independently selected canonical inputs. Then launch in an owned subprocess:

```sh
.venv-dense/bin/python workers/dense/pilot.py \
  --manifest INPUTS.json --manifest-sha256 INPUTS_SHA \
  --stage COMMITTED_POSE_STAGE \
  --recipe RECIPE.json --recipe-sha256 RECIPE_SHA --output NEW_DIRECTORY
```

Use a process owner with a timeout and cancellation grace/kill/reap behavior. The
local qualification uses a 900-second timeout. SIGINT/SIGTERM forward to the
backend token, but the observed GPU operation did **not** stop cooperatively within
six seconds; the separate cancellation probe required owner SIGKILL. This tool
therefore does not qualify the production worker cancellation protocol. Never
launch it synchronously on a viewer thread. Existing output directories are
refused; failed/partial workspaces remain inspectable and are not resumable jobs.

Before GPU work, the tool verifies manifest/recipe bytes, profile/bounds, selected
model/image hashes, decoded RGB8 dimensions and model-to-manifest calibration/
pose correspondence. It copies only owned workspace inputs. Python socket creation
is blocked; that guard is not an OS network sandbox. No model acquisition exists.
The worker checks cancellation before export and terminal publication. The
separate cancellation probe leaves no `result.json`.

Backend depth and graphs stay in `stereo/`. `colmap_io.py` reads bounded
little-endian native maps, validating graph coordinates, distinct known source
indices, counts and records. Pinned 4.2.1 graphs encode **column then row**;
[upstream source](https://github.com/colmap/colmap/blob/4.2.1/src/colmap/mvs/consistency_graph.cc)
and an asymmetric test establish that ordering. Positive finite geometric depth
with two consistent source observations becomes valid axial camera-Z; support is
**three including the reference**. Other pixels normalize to explicit unknown,
with +0 depth/confidence/support. Confidence is the fixed recipe fraction, never
inferred uncertainty. Normals are retained in backend form but not yet admitted
as project assets.

`assets/*.wedepth` bind the original observation, calibrated image, canonical
poses and full recipe. Assets are closed/flushed and exclusively linked first;
the final `result.json` is published atomically last, labeled
`PARTIAL_PILOT_COMPLETE`. No scene transaction occurs. Native 4104-wide output
needs explicit depth limits; `we_process inspect-depth FILE SHA --max-dimension
8192` checks its format, while `load_dense_inputs` plus `load_pose_bound_depth`
perform independent input/pose admission.

[Observed native-pilot checks](../../docs/validation/2026-10-17-dense-worker.md)
retain their original recipe/source identity and scope. Adding the neighbor-count
parameter to the shared depth encoder changes implementation hashes; historical
recipes are not rebound or rerun. Default two-source encoding preserves the
three-view policy; the new recipe pins its actual implementation separately.

## Resized neighbor experiment

`neighbors.py` is a second, **partial development experiment**, outside the shared
job/protocol pipeline. It consumes separately admitted native and resized
manifests, the committed native pose stage and an exact full recipe. It owns a new
workspace, adapts all model views and copies nine selected PNGs. No pose solve,
appearance fit, scene revision, measured scale, fusion or walking occurs.

The recipe declares `PARTIAL_NINE_VIEW_NEIGHBOR_QUALIFICATION`, the resized input
profile, nine consecutive manifest `selection_indices` and `neighbor_radius: 3`.
Sources are the preceding/following three selected views, giving **3–6 sources**
per reference. The source graph closes over all nine views; there is no hidden
automatic source expansion. The full PatchMatch dictionary must equal pinned
4.2.1 defaults with the same operational changes as the native pilot: GPU 0,
`max_image_size=-1`, 1 GiB cache, two threads and consistency-graph output.
Geometric consistency/filtering, five iterations and two required consistent
sources are unchanged. The explicit resized grid belongs to the input profile;
the solver must not implicitly resize it again.

The full recipe also contains the checked `resize` object, environment hash,
`neighbors.worker_digest()` and the same confidence declaration as the native
pilot. The current digest binds `neighbors.py`, `resized_model.py`, `colmap_io.py`,
`pilot.py`, `resize_inputs.py`, `source_selection.py` and `plan_sources.py`.
The earlier five-file receipt retains its original source identity. A new full recipe requires newly bound resized
input metadata. Previously admitted images/depth keep their original identities.
No generic unpinned launch, package acquisition or remote fallback is provided.

`resized_model.adapt_model` reads an owned, hash-checked native model, validates
all image/camera/pose/calibration identities and promotes its single undistorted
camera to **PINHOLE**, preserving distinct X/Y focal lengths. Native COLMAP image
coordinates scale by their actual per-axis ratios; canonical integer-center
intrinsics additionally use the documented half-pixel transform. Both tracked
and untracked keypoints scale; track links, poses, 3D positions and colors stay
unchanged. Pixel-based point errors are recomputed. Read-back checks validate the
actual serialized camera; asymmetric optical probes verify rays. This follows
[COLMAP's native camera domain](https://github.com/colmap/colmap/blob/4.2.1/src/colmap/scene/camera.cc)
and its [MVS input requirements](https://github.com/colmap/colmap/blob/4.2.1/src/colmap/mvs/patch_match.cc).

After producing the resized images with `resize_inputs.py` and independently
checking them using `we_process check-resized-dense`, launch:

```sh
.venv-dense/bin/python workers/dense/neighbors.py \
  --native-manifest NATIVE.json --native-manifest-sha256 NATIVE_SHA \
  --manifest RESIZED/manifest.json --manifest-sha256 RESIZED_SHA \
  --recipe RECIPE.json --recipe-sha256 RECIPE_SHA \
  --stage COMMITTED_POSE_STAGE --output NEW_WORKSPACE --prepare-only
# Omit --prepare-only for the bounded GPU run, using another new workspace.
```

`prepared.json` identifies checked private backend inputs and the exact source
configuration; it is not a resumable job or a depth result. Existing, linked or
overlapping output workspaces are refused. The full run writes immutable depth
assets first and publishes `result.json` last as `PARTIAL_NEIGHBOR_PILOT_COMPLETE`.
The worker checks cancellation between preparation/export steps and forwards
signals to the backend token. Use an owning process with timeout, grace, kill and
reap; the native pilot's non-cooperative GPU cancellation is not fixed by this
experiment. Python socket creation is blocked, not an OS network sandbox.

Positive finite geometric depth with at least **two distinct consistent source
observations** becomes valid axial camera-Z. Support includes the reference;
confidence is `(support - 1) / actual_selected_source_count`, still a relative
weight, not probability or uncertainty. Support exceeding that reference's source
inventory rejects. Missing/insufficient depth remains explicit unknown with zero
depth/confidence/support. Backend normals are retained but not admitted as project
assets. The `.wedepth` outputs use existing default bounds for the explicit grid.

[Local neighbor evidence](../../docs/validation/2026-10-18-dense-neighbors.md)
separates complete model correspondence, actual bounded GPU output and native
pose admission from unimplemented room geometry. Production job/control/resume,
full-scene dense qualification, fusion, compression, scale, collision and mesh
export remain M3 work.

## Parallax-aware source experiment

The additional `PARTIAL_PARALLAX_NEIGHBOR_QUALIFICATION` scope keeps **the first
nine manifest views as references** and selects at most **18 total views**. This
is another bounded development experiment, not full-scene reconstruction. The
resized input profile/grid, PatchMatch options, geometric filters and two required
consistent sources remain unchanged. All selected sources receive their own MVS
row; all selected views are exported. The nine-reference result remains separately
identifiable through `reference_indices`.

First independently admit the retained native inputs, then plan without a GPU:

```sh
.venv-dense/bin/python workers/dense/plan_sources.py \
  --manifest NATIVE.json --manifest-sha256 NATIVE_SHA \
  --stage COMMITTED_POSE_STAGE --output NEW_PLAN.json
```

The planner checks model hashes against the committed completion and manifest,
reads private copies, and verifies every image/camera/calibration/pose against
the native metadata. It validates bidirectional track references. Multiple
explicit feature observations of the same landmark in a view count as **one**
visible landmark; they remain unchanged in the backend model. Observations must
be inside their image and in front of their camera. Missing/inconsistent track
links, nonfinite data and unsupported inputs reject.

`COVISIBLE_PARALLAX_GREEDY_V1` considers only pairs sharing at least **30 distinct
visible landmarks** with **3°–60° parallax**. Deterministic greedy selection adds
support views using gains in the references' two-source landmark coverage,
normalized per reference; ties use manifest order. Per-view source rows use a
similar coverage-gain ordering, with at most **six** sources. Auxiliary vertices
without two eligible sources are pruned to a fixed point; losing a reference's
two-source support rejects the plan. References are never removed to improve
reported yield. Source rows need not be reciprocal; every edge must close over
a selected vertex with its own geometric depth row.

The profile caps model views at 120, landmarks at 100,000, keypoints/track
references at one million each, and retained eligible pair-landmark incidences
at four million. Cancellation checks bound iteration; these are input bounds,
not process-memory or latency guarantees. Ray angles use scaled normalization
so arbitrary coordinate units do not become a metric assumption. The planner
neither estimates geometry nor proves dense coverage.

Freeze the plan's `selection`, `selection_sha256` and exact policy into a new
full depth recipe: `selection_indices`, `neighbors`, `selection_policy`,
`selection_sha256`; omit the temporal recipe's `neighbor_radius`. The supported
policy is `source_selection.DEFAULT_POLICY`. Include current worker/environment
and preprocessing identities as before. Generate newly bound resized metadata
and run `we_process check-resized-dense` before `neighbors.py` with this recipe.
The existing worker CLI and `--prepare-only` mode apply.

The worker recomputes the selection from the actual owned native model before
adaptation/solve, compares both its canonical report digest and exact rows with
the frozen recipe, and publishes `selection.json` with its preparation receipt.
Rehashing a forged row or plan digest does not bypass that check. Recipes and
previous results retain their original hashes; adding this implementation does
not upgrade the earlier temporal experiment. No implicit re-selection, source
expansion, threshold relaxation, partial-work resume or scene publication occurs.

[Local parallax evidence](../../docs/validation/2026-10-18-dense-parallax.md)
records synthetic invariants, the retained model, a changed bounded GPU recipe,
fixed-reference comparisons and native admission. Sparse support statistics and
valid-mask coverage do not certify measured geometry, Q5/N2 or walking.

The checked 18-view candidate **reduced coverage on all nine fixed references**
(3.34–9.89% → 2.60–5.36%), despite stronger sparse-track support. It remains
experimental and is not promoted as a quality improvement. Recorded projection
round trips suggest dense visibility/consistency must influence source ranking;
this is a diagnostic, not ground-truth geometry. Retain both runs and inspect the
evidence before another changed recipe; do not repeat or relax filters.

## Cached visibility and photometric diagnostic

`cached_sources.py` provides CPU-only `freeze` and `score` commands for the
retained resized development runs. This diagnostic neither invokes PatchMatch
nor produces an executable worker recipe or admitted geometry. It keeps the
entire selected cache inventory (9–18 views), the first nine references and closed
rows of six sources. It cannot search uncached views or certify a new solve.

```sh
.venv-dense/bin/python workers/dense/cached_sources.py freeze \
  --workspace COMPLETED_NEIGHBOR_WORKSPACE \
  --manifest RESIZED.json --manifest-sha256 RESIZED_SHA \
  --recipe RECIPE.json --recipe-sha256 RECIPE_SHA \
  --result-sha256 RESULT_SHA --output NEW_CACHE.json
.venv-dense/bin/python workers/dense/cached_sources.py score \
  --workspace COMPLETED_NEIGHBOR_WORKSPACE \
  --cache NEW_CACHE.json --cache-sha256 CACHE_SHA --output NEW_SCORES.json
```

`freeze` checks the pinned terminal/preparation/manifest/recipe chain, closed
source graph/config, serialized model hashes and RGB hashes. It explicitly hashes
the **current private photometric maps**: the old worker terminal did not hash
those maps, so this receipt cannot prove their original producer-time identity.
The descriptor embeds exact dependency-document bytes and relative file hashes.
`score` rechecks every file, loads model copies from owned bytes, and checks actual
camera IDs, pinhole calibration, poses and grids against the manifest. Private
maps require exact little-endian payloads and finite values; nonpositive samples
remain unknown. They never enter the public geometric-depth admission path.

Inputs are limited to 44 files, 256 MiB/file, 512 MiB total encoded bytes and
48 million decoded pixels; grids are 96–4096 × 72–4096. These admission bounds
are not a measured process-memory guarantee. Linked/escaping paths, stale
dependencies, malformed maps, occupied/overlapping outputs and cancellation
publish no new result. Output publication is exclusive and atomic. Signals are
checked between bounded operations. Python sockets are disabled in the CLI;
this is not an operating-system network sandbox. No environment change/download
or resume occurs. The seven-file GPU worker bundle remains unchanged.

The fixed `CACHED_VISIBILITY_NCC_GREEDY_V1` policy uses a 32×24 pixel grid,
axial-Z reprojection and bilinear source depth. A support sample requires a
positive-depth round trip within one pixel, ≤1% relative depth difference,
≥3° parallax, an in-bounds nine-tap RGB-to-luminance warp at offsets −2/0/2,
both patch variances ≥1e-4 and normalized cross-correlation ≥0.1. Invalid depth,
occlusion disagreement, textureless or inverted patches supply no support.
This sampled depth warp is **not a replay of COLMAP's plane-based NCC/filter**.
No backend option or product acceptance threshold changes.

Per-row greedy ranking prioritizes a second supporting source, then first-source
support, then quantized NCC/round-trip/parallax quality; ties use canonical view
order. All comparisons use the **same frozen maps**. A second disjoint pixel grid
is evaluated after ranking. It is the same capture/cache, not an independent
held-out scene; the cache itself depends on the old source graph. Reports retain
per-reference and per-pair results, code/cache identities and
`gpu_recipe_ready: false`. No quality promotion follows from training support.

[Local CPU evidence](../../docs/validation/2026-10-18-dense-visibility.md) records
an unsuccessful candidate: training two-source support rises 107→126 samples,
but separate-grid support falls 84→81, with five references worse. No GPU retry
was justified. Both the sparse-only and cached scoring candidates remain
experimental; measured geometry/fusion/walking remain open.

## Spatial and texture reliability diagnostic

`reliability.py` extends the private-cache diagnostic with **128×96 reference
centers per view**, 8×6 interior-image tiles and three reference-patch variance
classes: `FLAT` below 1e-4, `LOW` from 1e-4 to below 1e-3, and `HIGH` at least
1e-3. Every sampled center remains in its tile/class denominator, including
unknown depth and zero support. Six border pixels are outside the sampled domain;
these statistics are sampled interior coverage, not full-plane reconstruction.
Each tile contains 256 sampled centers. Reports include joint tile/class counts,
unsupported counts and explicit conditional populations for depth-disagreement
quantiles. Empty groups retain zero counts and null fractions/quantiles.

The fixed `DENSE_GRID_GREEDY_STRATIFIED_DIAGNOSTIC_V1` policy preserves V1's
support gates and greedy objective. Only sampling density and diagnostics change;
there is no texture/tile reweighting or backend-filter change. Reference centers
at quarter-cell training and three-quarter-cell evaluation phases are disjoint;
grids below **268×204** reject rather than shrinking/deduplicating the sample set.
Patch neighborhoods and source warps can overlap across stages. Both phases use
the same capture/cache and do not establish independent held-out validation.

```sh
.venv-dense/bin/python workers/dense/reliability.py plan \
  --workspace COMPLETED_NEIGHBOR_WORKSPACE \
  --cache CACHE.json --cache-sha256 CACHE_SHA --output NEW_TRAINING.json
.venv-dense/bin/python workers/dense/reliability.py check \
  --workspace COMPLETED_NEIGHBOR_WORKSPACE \
  --cache CACHE.json --cache-sha256 CACHE_SHA \
  --plan NEW_TRAINING.json --plan-sha256 TRAINING_SHA --output NEW_EVALUATION.json
```

`plan` uses only training reference centers, keeps all cache vertices and the
first nine references, and freezes closed six-source rows plus training evidence,
policy, implementation and cache identities. `check` recomputes the complete
training receipt before evaluating those rows on the second grid. Rehashed row,
statistic, policy or code changes reject; evaluation never ranks sources. Both
commands publish exclusively, cap reports at 16 MiB, and inherit the cache's
path/byte/pixel/camera/pose guards. Cancellation is checked between bounded pair
operations and before publication. CLI Python sockets are disabled; no OS network
sandbox, GPU cooperative cancellation or production job/resume claim follows.

Per-pair first-failure counts partition all samples in this order: unknown
reference, outside source projection, unknown source, behind-camera return,
round-trip disagreement, relative-depth disagreement, low parallax, unknown or
outside patch, flat reference, flat source, NCC disagreement, supported. This
ordering is accounting, not causal attribution. Conditional relative-depth p90
and round-trip medians use known source depths with positive returned Z and
always report that population size. The legacy 768-center diagnostic retains
identical numerical results; its implementation digest changes with shared code.

[Local dense-grid evidence](../../docs/validation/2026-10-18-dense-reliability.md)
records aggregate separate-grid support **1,501→1,579**, but regressions on the
first two references and high-texture support **958→930**. **96,397/110,592**
evaluation centers are `FLAT` and supply no support by the existing policy;
**317/432** candidate tiles have zero support. A same-42-pair comparison across
both retained caches finds large disagreement in textured regions too. The
candidate remains diagnostic-only (`gpu_recipe_ready: false`); no new solve or
quality promotion is justified by the aggregate count.

## Pose-admitted geometric depth

The native `we_process export-depth-visibility` application command now exports
selected `.wedepth` assets after independently rechecking resized inputs and
canonical pose context at default depth limits. The
[request/receipt contract](../../docs/protocols/depth.md#pose-admitted-visibility-export)
defines paths, byte/pixel bounds and manifest-last publication. It preserves
original depth/validity/support/confidence/optional uncertainty bytes, copied RGB,
camera metadata and dependencies. It neither reruns MVS nor updates a scene.

`admitted_visibility.load_admitted_cache(root, receipt_sha256, cancelled)` checks
the pinned native receipt, copied files/domain, all planes and actual RGB8
decoding. It returns immutable arrays and optical camera transforms for existing
CPU diagnostics. Native admission is established by the observed export command
and pinned receipt; this consumer does not independently certify a forged
receipt or reconstruction accuracy. There is no private photometric-map fallback.

[Local checks](../../docs/validation/2026-10-18-depth-visibility.md) preserve all
**27** original depth assets across the nine/18-view runs. Fixed-nine valid
observations remain **1,537,092 / 790,453**. The same 42-pair sampled comparison
includes **474,641 / 496,003 unknown-reference observations out of 516,096**;
conditional agreement cannot hide that missing coverage. No source re-ranking,
new solver run, metric scale, fusion, Q5/N2 or walking qualification follows.
