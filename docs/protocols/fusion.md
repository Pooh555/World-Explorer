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
| `mesh.npz`: `positions`, `normals` float32 V×3; `triangles` int64 T×3 | Raw Open3D extraction at weight threshold 1e-6, world coordinates; no color, inferred floor, filling, component cleanup or simplification |

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
Native scene admission of this new format is not implemented.

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
