# Observed sparse TSDF pilot

`workers/fusion/` is an isolated Linux CPU development pilot under plan §5.1.
It consumes the [native pose-admitted depth/RGB cache](depth.md#pose-admitted-visibility-export),
never private photometric maps. It produces immutable **partial observed geometry**,
not a native scene/collision revision, production reconstruction job, complete
mesh export, measured Q5 result or walkable environment. The
[component/resource selection](../research/observed_fusion.md) owns backend terms.

## Frozen inputs and policy

The externally pinned UTF-8 request has exactly these fields (paths resolve
relative to the request):

```json
{
  "schema_version": 1,
  "cache": "NATIVE_ADMITTED_CACHE_DIRECTORY",
  "receipt_sha256": "NATIVE_RECEIPT_SHA256",
  "recipe": "FROZEN_RECIPE.json",
  "recipe_sha256": "RECIPE_SHA256"
}
```

Generate the recipe with `contract.selected_recipe()` before running; its complete
fixed policy, worker dependency digest and hash-locked installer recipe must
match the current code. This detects drift, not trusted execution or an installed
byte attestation. The pilot checks selected Open3D/NumPy/Pillow versions; dated
installed-file inventories separately verify the actual environment. Duplicate
keys, nonfinite numbers, unknown request fields and policy/code/hash changes
reject. Existing destinations and containment overlaps with request, recipe or
cache reject. Every input view remains in the output denominators.

The native reader rechecks all copied dependencies and exact depth planes.
World points use axial optical Z, integer pixel centers and inverse checked
camera transforms. Unit pose scale is required. Robust extent is the largest
axis span between **1st/99th percentiles of all valid positive-confidence world
depth points**, NumPy linear percentile interpolation. It selects voxel size
`extent/512`; it does not crop outliers or discard views. Truncation is four
voxels. Units remain arbitrary pose/camera units; no metric default is applied.

A pixel must be valid with positive confidence, agree with **at least two other
selected sources** within 1% relative axial depth and one pixel Euclidean back
round trip, and have no known source occluding it by more than 1% depth. Source
lookup is nearest integer, `floor(pixel+0.5)`, with half-pixel image bounds.
Unknown/zero-weight sources contribute neither agreement nor occlusion. These
are additional frozen pilot checks; existing backend filtering is preserved.
They are internal consistency, not independent geometric truth.

## Sparse meaning and outputs

Open3D CPU sparse blocks hold project-integrated attributes. Blocks cover accepted
surface points plus a one-block halo. Projected voxels integrate only accepted
pixels **within ±truncation**; farther front space and space behind that band stay
unknown. Confidence is the integration weight, never a probability. Backend
support is retained separately and never multiplied into confidence. Allocation
alone establishes no observation or free space.
Before backend narrowing, block/halo coordinates must fit a ±2²⁰ lattice range
with corner margin; this also limits float32 world-position precision loss.
Larger translated worlds reject and need an explicit rebasing recipe.

Assets are closed/flushed before terminal publication:

| File / arrays | Meaning |
|---|---|
| `observed-voxels.npz`: `lattice` int32 N×3 | Integer world lattice, position = lattice × voxel size; absent entries unknown |
| `tsdf`, `weight` float32 N | Weighted mean normalized signed axial distance, positive weight; positive TSDF faces the observing camera |
| `observation_count`, `support_sum` uint32 N | Integrating view count and sum of original backend support counts |
| `front_count` uint32 N | Integrating observations with positive signed distance, only in the local band; not a full ray-carved free-space map |
| `sigma2_sum` float64, `sigma_weight` float32 N | Sum(weight × input sigma²) and weight for observations having sigma; zero sigma weight means unavailable, never certainty |
| `mesh.npz`: `positions`, `normals` float32 V×3; `triangles` int32 or int64 T×3 | Raw Open3D extraction at weight threshold 1e-6, world coordinates; no color, inferred floor, filling, component cleanup or simplification |

Use `numpy.load(..., allow_pickle=False)`. RMS input sigma is
`sqrt(sigma2_sum/sigma_weight)` where available; it is not fused surface-error
uncertainty. Mixed availability stays explicit. Mesh normals describe extracted
geometry; captured radiance and relightable material products are absent.

The terminal `PARTIAL_OBSERVED_TSDF_V1` `manifest.json` pins request/receipt/recipe/
pose identities, epoch/revision, units, extent/resolution, exact file byte counts/
SHA-256, per-view valid/unknown/accepted/rejected counts, sparse unknown/front/
sigma counts, mesh counts and supervisor observations. Walking is always false.
`result.pending.json` is an uncommitted child result and **never consumable**;
only successful supervision publishes the terminal receipt exclusively. Parent
directory flush failure can leave a terminal file: inspect its hash before retry.
Native admission is described below; the [runtime mesh view](scene.md#observed-mesh-runtime)
adds immutable scene publication separately from this worker.

## Run and failure boundaries

```sh
artifacts/environments/fusion-cpu/bin/python workers/fusion/run.py REQUEST.json REQUEST_SHA NEW_DIRECTORY
```

The separate CPython 3.12 environment is installed from
[`environment-lock.txt`](../../workers/fusion/environment-lock.txt) with hash
checking; no other worker environment is changed. The supervisor uses argument
arrays, four OpenMP threads, a 300 s deadline and 4 GiB child RSS sampled every
20 ms from Linux `/proc`; these sampled limits are not hard OS memory quotas or
performance qualification. Worker caps: 18 views, 48M input pixels, 32,768 blocks
of 8³ voxels, 65,536-voxel processing chunks and 512 MiB outputs. Overflow fails
without coarsening or view exclusion. Raw output size is checked before writing;
encoded size is checked before terminal publication.

SIGINT/SIGTERM request cancellation; the supervisor sends child-group TERM, waits
at most two seconds, then KILLs/reaps. Cancellation exits 130; bad input, resource
limits and backend errors fail without a terminal receipt. Partial owned files
may remain; no overwrite/resume is provided. Direct `--child` invocation is an
internal implementation detail and cannot publish a terminal receipt. Full
application ownership, job kill/reap and resume qualification remain separate.

## Bounded full-sequence CPU worker

`contract.selected_recipe(sequence=True)` selects the separate fixed
`OBSERVED_TSDF_CPU_SEQUENCE_V1` policy. The same request and supervisor syntax
accept a pinned `POSE_ADMITTED_DEPTH_SEQUENCE_V1` union, with up to 120 frames;
the legacy recipe still accepts only one bounded native cache. Each child keeps
the existing 18-view, 48M-pixel and 768 MiB encoded-input limits.

The sequence worker exhausts the checked native iterator before fusion. It
copies only depth/validity/confidence/support/optional sigma planes into exclusive
NPY scratch under the new output directory, releasing each decoded child before
reading the next. At most one reference and one source view are mapped during
consistency filtering; maps are explicitly closed. RGB is checked by native-cache
consumption but contributes no fusion weight or color product.

The scratch reservation is separately bounded to 4 GiB, including plane files,
mask files, NPY header allowances and three float64 percentile axes. Exact pooled
linear percentiles partition these owned on-disk axes; batch percentiles are
never averaged. The worker checks every source across child boundaries for both
agreement and known occlusion, even after sufficient agreement exists. Per-view
denominators and optional uncertainty availability keep their original meaning.

Accepted masks feed one sparse grid at global `extent/512`, with the original
32,768-block, four-voxel truncation, 512 MiB product, 4 GiB sampled RSS and 300 s
deadline limits. Integration scans views in order, mapping each once per pass;
each voxel retains the same ordered confidence/support/sigma updates. There is
no independent batch mesh or scale merge. Failed attempts retain scratch and
partial products without a terminal receipt. A successful worker removes only
its own newly created scratch before terminal publication. Scratch is internal
work data and never accepted as a reusable stage.

The retained geometry coordinator exhausts the same union reader before owning
or reusing a job, including all later child dependencies. Complete stages keep
their existing four-file inventory; cancelled stages retain their scratch and
restart in a new attempt. The retained shared GUI/CLI pipeline consumes these
jobs through the native admission below. The fresh-depth pipeline also admits
the complete sequence terminal through the adapter below.

## Owned retained-cache geometry job (Linux development slice)

`tools/geometry_job.py` accepts the existing pinned fusion request, its exact
SHA-256, a current pose asset and its SHA-256, then a new job directory. Run it
with the isolated fusion Python interpreter:

```sh
artifacts/environments/fusion-cpu/bin/python tools/geometry_job.py \
  REQUEST.json REQUEST_SHA256 CURRENT.weposes POSE_SHA256 NEW_JOB_DIRECTORY
```

It admits every byte and plane of the selected native depth/RGB cache through
`load_admitted_cache`, or exhausts `iter_admitted_sequence` for the explicit
sequence recipe, and checks its pose identity against the separately pinned
current pose file. It requires the fixed fusion recipe/code/environment lock.
An immutable owner binds request/cache/pose, worker/coordinator bytes and Python
path. A nonblocking Linux file lock excludes a second cooperative writer.
Foreign occupied directories reject before a lock file is created.

The stage is `stages/fusion-OWNER_SHA-attempt-N/`. The existing supervised fusion
worker runs in a process group; cancel sends TERM, then KILL after three seconds
and reaps. Incomplete attempts remain untouched. A restart checks every complete
stage asset against its terminal manifest and reuses only an exact matching
stage. Otherwise it launches a new attempt without deleting partial bytes.
`manifest.json` in the job root is published last and pins the owner and selected
stage manifest. A failed or cancelled job has no new terminal job receipt.
Symlinks, changed inputs, a different owner, malformed stages and unknown job
entries reject. The original cache, fusion outputs and prior scenes are not
rewritten. This owns retained-cache fusion only: fresh CUDA depth production,
native `.wefusion` conversion/admission, scene handoff, metric geometry and walk
remain separate. Local `flock`/directory flush is not a power-loss guarantee.

### Native terminal adoption and scene publication

`adopt_geometry_job` in `application` and the shared CLI own conversion,
pose-bound admission and native bundle publication for one completed retained-cache
job. They do not launch the fusion/depth solver. Use a new native output
directory, separate from the Python job and all inputs:

```sh
build/dev/we_process adopt-geometry REQUEST.json REQUEST_SHA256 NEW_NATIVE_JOB
build/dev/we_viewer --scene NEW_NATIVE_JOB/stages/attempt-0/scene.wexplore
```

The externally pinned request is a flat JSON object with exactly these fields:

```json
{
  "schema_version": 1,
  "job": "completed-python-job",
  "job_manifest_sha256": "<SHA-256 of job/manifest.json>",
  "fusion_request": "fusion-request.json",
  "fusion_request_sha256": "<SHA-256 of the original fusion request>",
  "poses": "current.weposes",
  "pose_sha256": "<SHA-256 of the current canonical poses>",
  "python": "path/to/isolated/fusion/bin/python",
  "converter": "path/to/tools/convert_fusion_asset.py",
  "converter_sha256": "<SHA-256 of the selected converter>"
}
```

Paths resolve against the request directory. JSON is limited to 1 MiB and 32
levels; raw NUL, duplicate keys, wrong fields/versions and bad pins reject.
Inputs/output must not overlap, including the indirect cache/recipe dependencies.
Symlink aliases reject except the explicitly selected Python executable; its
resolved path and file digest join the immutable native owner. Environment
package qualification remains separate.

Before native ownership, admission checks the exact terminal/owner/stage graph,
every source asset size/hash, complete stage inventory and current pose pin and
epoch/revision. A writer lock excludes cooperative concurrent native publication.
Each conversion uses argument arrays in a supervised child and a new
`stages/attempt-N/observed.wefusion`; cancellation terminates/reaps the child.
The resulting asset passes `load_fusion_scene` against current poses before
`write_bundle` publishes `scene.wexplore`. All raw voxel/coverage metadata and
source frames survive unchanged; no cleanup or color bake is selected here.

The root terminal receipt is written last and pins owner, attempt, asset and
bundle manifest. Replay rechecks source bytes, poses, converted asset, complete
bundle and its content hash against direct source admission, skipping conversion.
Corrupt committed output rejects without repair. Incomplete attempts remain
untouched; restart reserves the next attempt (at most 128) rather than overwriting
them. Once bundle publication begins, the coordinator finishes the receipt as a
commit boundary; cancellation before that point leaves no new terminal receipt.
A crash between bundle and receipt publication may leave an unselected bundle.
Directory flush/locks do not establish power-loss guarantees.

Ordinary Open scene/`--scene` consumes the resulting bundle through the existing
scene update path. Failed adoption never replaces a previously selected scene.
The [native adoption evidence](../validation/2026-10-19-native-geometry-job.md)
covers real populated-attempt cancellation/resume and full native viewing.
The unified retained-cache path below now connects solver ownership to this
adoption boundary. Fresh depth is integrated below; metric alignment, collision
and Q5/N2 remain open.


### Unified retained-cache solver and viewer job

`run_geometry_job` and `we_process run-geometry REQUEST.json SHA256 NEW_JOB`
now own the retained-cache solver and native adoption as one Linux development
job. Select the local isolated fusion interpreter explicitly. The pinned request
uses exactly the native-adoption fields `schema_version`, `fusion_request`,
`fusion_request_sha256`, `poses`, `pose_sha256`, `python`, `converter` and
`converter_sha256`, plus:

```json
{
  "kind": "RETAINED_GEOMETRY_PIPELINE_V1",
  "coordinator": "path/to/tools/geometry_job.py",
  "coordinator_sha256": "<SHA-256 of that coordinator>"
}
```

These fields extend one flat object; `job` and `job_manifest_sha256` are absent.
Paths resolve against the request. The existing bounded, duplicate/NUL-rejecting
reader validates exact request/script/pose pins and indirect cache/recipe output
separation before ownership. A top-level immutable `owner.json` and `writer.lock`
bind the request plus Python bytes/resolved path. Unknown, linked, foreign and
concurrently owned output rejects. Worker packages remain separately qualified.

Under `stages/solver`, the explicit coordinator runs or checks retained fusion.
The native owner passes `--parent-supervised`: the coordinator and fusion
supervisor stay in its process group; the fusion supervisor owns its backend
child group. Cancellation sends TERM immediately. The backend supervisor has two
seconds to kill/reap its child, the coordinator three seconds to reap its
supervisor, and the native owner five seconds before KILL. The general POSIX
process control now accepts a bounded TERM-to-KILL interval (default two seconds,
maximum five); existing commands keep their defaults. Windows job-object
termination is unchanged; this solver entry rejects non-Linux hosts.

After a successful child boundary, the application rechecks request and execution
identities, pins the solver terminal manifest, publishes exact `adoption.json`,
then invokes `adopt_geometry_job` under `stages/native`. Both owners retain their
incomplete attempts; restart reuses only checked solver output and reserves a new
native conversion attempt. A committed bundle replays strictly. Changed source
or current-pose bytes reject before native publication. No source stage, prior
scene or occupied caller output is repaired or overwritten. Native bundle
publication retains its existing commit semantics.

The viewer exposes **Import video → Build observed geometry**, with
request, SHA-256 and output fields. Its owned background thread uses this same
application command, bounded logs and Cancel button. Processing pauses scene
rendering while retaining GPU assets. Successful publication loads the checked
bundle into the ordinary immutable snapshot/update path; a pending snapshot
prevents another job. Failure/cancellation before publication preserves the
previous scene. Startup diagnostics accept:

```sh
build/dev/we_viewer --geometry-request REQUEST.json --geometry-request-sha SHA256 \
  --geometry-output NEW_JOB --frames 90
```

An optional `--scene PRIOR.wexplore` opens the prior scene before starting this
job. Other startup processing requests are mutually exclusive. GUI/CLI share the
same pins, recovery and output ownership; this path produces raw partial observed
surfaces only. The fresh-depth entry below extends it. Full-sequence coverage,
cleanup/baking job stages, measured alignment, collision/walking and quality gates
remain separate.
[Verification](../validation/2026-10-19-geometry-pipeline.md) includes populated
solver/converter recovery and actual native scene preservation/handoff.

## Native partial-geometry admission

`tools/convert_fusion_asset.py` converts existing, externally pinned terminal
outputs to a single immutable `.wefusion` file. It performs no fusion, cleanup,
resampling or scene publication and leaves original outputs intact. Use the
existing isolated fusion Python environment; no new backend/environment is needed:

```sh
artifacts/environments/fusion-cpu/bin/python tools/convert_fusion_asset.py FUSION_DIRECTORY MANIFEST_SHA REQUEST.json REQUEST_SHA NEW.wefusion
build/dev/we_process inspect-fusion NEW.wefusion ASSET_SHA MANIFEST_SHA RECEIPT_SHA RECIPE_SHA CURRENT.weposes POSE_SHA
```

The converter checks source file sizes/hashes and bounds ZIP expansion and NPY
headers before loading arrays, without pickle. It accepts exact float32/int32/
uint32/float64 source planes and checked int32 or int64 triangle indices. It sorts
voxel records lexicographically by lattice, preserving every value; positions,
normals and triangle order remain unchanged. Exclusive closed/flushed publication
refuses existing destinations and input containment overlaps. As with the pilot,
a directory-flush error can leave a published file; inspect its hash before retry.
Conversion is not native admission. The final asset hash pins the converted
payload; original NPZ hashes identify provenance but the native reader does not
decompress NPZ or independently repeat fusion. An independent retained-output
comparison verifies the conversion in the [admission evidence](../validation/2026-10-19-fusion-admission.md).

The project-owned V1 layout is uncompressed, little endian, with no padding:

| Sequence | Encoding |
|---|---|
| Header, 36 bytes | `WEFUSN01`, uint32 metadata byte count, uint64 voxel/vertex/triangle counts |
| Metadata | UTF-8 JSON object with exactly `manifest`, `receipt`, `recipe`, `request`; each value is the exact original document string |
| Observed voxel records, 44 bytes each | int32 lattice xyz; float32 TSDF and weight; uint32 observation count, support sum, front count; float64 sigma² sum; float32 sigma weight |
| Vertices, 24 bytes each | float32 world position xyz followed by float32 normal xyz |
| Triangles, 12 bytes each | uint32 vertex indices abc |

`decode_fusion_asset` in `scene_store` checks the same
externally SHA-pinned bytes it parses, all embedded document hashes and the
request → receipt/recipe → pose graph. Duplicate JSON keys, raw NUL, nesting above
32, nonfinite values, unsupported semantics, trailing/truncated bytes and count
aliasing reject. Fixed maxima are **256 MiB encoded**, **4 MiB metadata**,
**16,777,216 voxels**, **4M vertices**, **8M triangles**. Legacy receipts keep
**18 views / 48M pixels**; the explicit sequence receipt/recipe pair permits
**120 views**, with each contiguous child still limited to **18 views / 48M pixels**.
Sequence admission checks canonical child names, pinned child identities, the
complete partition, unique frames, contiguous manifest indices and root valid/
unknown totals. Caller limits may tighten encoded/geometry counts.
The exact encoded length is checked before any
count-based allocation. Reads/hashing/voxel/mesh loops check cancellation.

Epoch/revision must match the caller's context. Coverage retains every ordered
input view and full-grid valid/unknown/accepted/rejected denominators. Resolution
is extent/512 with four-voxel truncation, in arbitrary units. Unique sorted
lattices remain within ±2²⁰; TSDF is finite in [-1,1], weight positive and no larger
than observation count, support/front counts bounded by their observations, and
sigma weight/sum finite and coherent with availability. Allocated unknown counts,
front-band counts and sigma counts must agree. Mesh positions are finite within
the lattice precision extent, normals finite and unit within squared-length 0.001
or explicitly zero for raw degenerate extraction, indices in range and distinct
within each triangle. This does not establish topology or geometric accuracy.

`load_pose_bound_fusion` in `application` reads a
separately pinned canonical pose asset and verifies every selected observation,
calibration ID, tracking/invalidation state and composed world camera transform.
It returns owned partial geometry without mutating caller files or publishing a
scene/collision revision. `inspect-fusion` reports its asset/dependency identities,
coverage, unknown/front/sigma denominators and arbitrary units. Walking stays
false. The [runtime mesh path](scene.md#observed-mesh-runtime) adds native scene
admission and normal-color inspection. Component analysis below precedes cleanup;
[certified local cleanup](mesh_cleanup.md) now derives separate immutable proxies.
Measured scale and complete export remain separate work.

## Read-only component and support analysis

`analyze_fusion_components` in `scene_store` consumes admitted immutable fusion
arrays. The application exposes the same pose-bound admission as `inspect-fusion`:

```sh
build/dev/we_process analyze-fusion INPUT.wefusion ASSET_SHA MANIFEST_SHA RECEIPT_SHA RECIPE_SHA POSES.weposes POSE_SHA
```

Stdout is `FUSION_COMPONENT_SUPPORT_DIAGNOSTIC_V1` JSON with the original source
pins, full coverage/unknown metadata and every component. Components connect
triangles through **shared vertex indices**, including vertex-only contacts;
coincident positions are never welded. IDs follow first input triangle order.
Unused vertices are counted separately. Area is the double-precision cross-product
area of every input triangle, in arbitrary units squared; exact zero-area triangles
remain in the report and input.

For each referenced vertex, the diagnostic samples the eight integer corners of
`floor(position / voxel_size) + {0,1}³`, using the exact float positions promoted
to double, without snapping. Absent lattice entries remain unknown. Histograms
retain all nine observed-corner counts (0–8); weights, observation/support/front
counts and optional sigma sums retain their original meanings. These are
**repeated vertex/corner samples**, not unique voxels, independent views, surface
confidence, error estimates or full-ray free space. Positive-corner weight min/max
are zero when there are no known corners. Unknown-corner triangle/area counts
include any triangle with a vertex stencil below eight known corners; unbacked
counts require a vertex with zero known corners. The latter is a subset, so the
two areas must not be added.

The floor-cell stencil is directional and sensitive to float positions near grid
boundaries. Incomplete stencils do not establish false surfaces; component size or
unknown corners alone authorize no deletion. This version makes **no cleanup or
simplification decision**, changes no mesh and computes no cleaned-surface error.
Original-to-cleaned distances and an explicit error bound are required for the
subsequent cleanup/simplification product. Walking stays disabled.

Input counts are capped at the native interchange limits. The admitted asset
retains a derived complete sequence frame count for downstream analysis. Sequence
observation counts may reach that inventory; original backend support keeps the
native per-observation maximum of 1,000,000, independently of reference-view
count. Legacy diagnostics retain their existing 18-view/support bounds.
Analysis additionally
caps reports at 65,536 components and reserves at most 128 MiB for its algorithm
arrays/component records (`10 × vertices + reserved_components × sizeof(record)`;
input arrays and JSON serialization are separate bounded allocations). Caller
limits may tighten counts/work bytes. Invalid indices, nonfinite/out-of-range
positions (floor-cell coordinates within ±2²⁰), unsorted/invalid voxel evidence,
exhausted budgets and cancellation
reject without mutating caller data or emitting a partial CLI report. Scalar
accumulation overflow rejects. Native admission still owns complete file/schema/
dependency validation. [Real and analytic evidence](../validation/2026-10-19-fusion-components.md)
qualifies this diagnostic only, without Q5/N2 or automatic component removal.
The separate [cleanup contract](mesh_cleanup.md) owns its fixed support policy,
whole-surface error certificate and derived native requests.

## Fresh depth through the shared geometry job

`run_geometry_job`, `we_process run-geometry` and the viewer's **Import video →
Build observed geometry** now also accept `FRESH_GEOMETRY_PIPELINE_V1`. This Linux
entry connects the [owned CUDA depth job](dense_inputs.md#owned-cuda-depth-production)
to the retained-cache path. The earlier nine-view fresh solve and complete
63-frame retained-depth reuse have separate local evidence; both produce raw
partial normal-color geometry only.

The pinned flat request has exactly these fields (paths resolve against it):

```json
{
  "schema_version": 1,
  "kind": "FRESH_GEOMETRY_PIPELINE_V1",
  "dense_request": "dense-request.json",
  "dense_request_sha256": "<owned depth request SHA-256>",
  "dense_python": "path/to/clean/dense/bin/python",
  "dense_coordinator": "path/to/tools/dense_job.py",
  "dense_coordinator_sha256": "<coordinator SHA-256>",
  "visibility_template": "visibility-template.json",
  "visibility_template_sha256": "<template SHA-256>",
  "retained_template": "retained-template.json",
  "retained_template_sha256": "<template SHA-256>",
  "fusion_recipe": "fusion-recipe.json",
  "fusion_recipe_sha256": "<fixed CPU fusion recipe SHA-256>"
}
```

The visibility template is the existing native depth-cache export request with
an empty `outputs` array. The retained template is the existing retained geometry
request with `fusion_request` and `fusion_request_sha256` omitted. Both select the
same canonical current-pose path and SHA; dense manifest/recipe/pose selections
must also match native admission. Source/template paths are resolved before
internal request publication. Native resized input admission runs before ownership
or CUDA launch. Inputs, scripts and indirect source directories cannot overlap
outputs; Python executable aliases are explicitly resolved and pinned.

The pinned depth recipe selects the adapter: legacy selections require a
schema-1 visibility template and pilot fusion recipe; the bounded temporal
sequence requires schema 2 and the sequence fusion recipe. These combinations
are checked before ownership or child launch. Templates must select the same
canonical pose path as well as the same checksum.

`job_detail::depth_job_outputs` rechecks the pinned request/recipe, terminal owner,
bounded attempt names and every result/prepared document against independently
prepared native frame identities. For a sequence it requires all 19–120 input
indices, disjoint nine-reference batches, radius-three support halos and the
exact ordered result inventory. Every solved asset is hashed, including support
views that are absent from final outputs. Only references enter `visibility.json`,
each once in manifest order. Float/boolean indices, stale or duplicated batches,
changed prepared bytes, unsafe/linked paths and late support corruption reject.
The legacy adapter retains its ordered noncontiguous selections and 18-view cap.
This admission reads without changing caller files; it does not certify depth
quality. This read-only helper publishes nothing; cancellation and failures
preserve caller files and previously selected results.

A top-level immutable owner/writer lock protects the entire job. The dense owner
runs/reuses under `stages/depth`. The application pins its terminal/owner/result
graph, checks the bounded attempt/asset paths and builds `visibility.json` from
actual selected results. Native pose-bound export creates `stages/cache-N` and
publishes its manifest last; `cache.json` commits its selection and visibility
request hash. Interrupted cache exports remain immutable, and restart reserves
a new attempt (at most 128). A committed cache is hash-bound and subsequently
checked in full by the admitted cache reader; corruption rejects without repair.

The application then publishes exact `fusion.json` and `retained.json` and calls
the unchanged retained-cache pipeline under `stages/geometry`. Conversion and
native bundle publication keep their existing admission and commit boundaries.
Every replay rechecks depth source/environment identity, depth outputs, current
native inputs, cache and committed geometry; no CUDA backend is relaunched for
an exact completed depth stage. A failed depth/cache/geometry stage leaves prior
visible scenes and valid private results intact. No fallback environment, source
selection, download or recipe retuning is implicit.

The existing CLI/viewer syntax applies, with this fresh request and a new job:

```sh
build/dev/we_process run-geometry FRESH_REQUEST.json SHA256 NEW_JOB
build/dev/we_viewer --geometry-request FRESH_REQUEST.json --geometry-request-sha SHA256 \
  --geometry-output NEW_JOB --frames 90
```

The visible product still has arbitrary scale, unknown gaps and walking disabled.
Neither slice completes a room or the M3 milestone. Sequence captured-color
processing, measured alignment/collision, cleanup/baking job stages and full
quality/platform/rights gates remain separate. Completed-depth reuse does not
claim a repeated CUDA solve through the fresh entry.
[Native/real evidence](../validation/2026-10-20-owned-dense.md) records cancellation,
retained attempts, the complete fresh-depth chain and strict replay.
[Sequence adapter evidence](../validation/2026-10-20-fresh-sequence.md) records
complete retained-depth reuse, cache cancellation/resume, native handoff and
sanitizers without another CUDA solve.

## Owned mesh postprocessing

[The explicit postprocessing profile](mesh_jobs.md) extends the shared geometry
entry point with immutable cleanup/native/bake/GLB stages and pinned saved-bake
reuse. Retained/fresh raw defaults remain unchanged. Actual local sequence checks
are [recorded separately](../validation/2026-10-20-mesh-job.md); measured scale,
collision/walking and complete-room/quality gates remain open.
