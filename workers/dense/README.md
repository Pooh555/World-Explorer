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
pilot. The digest binds `neighbors.py`, `resized_model.py`, `colmap_io.py`,
`pilot.py` and `resize_inputs.py`. A new full recipe requires newly bound resized
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
