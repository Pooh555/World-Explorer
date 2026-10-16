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

[Observed checks](../../docs/validation/2026-10-17-dense-worker.md) include the
synthetic plane, real three-view output, native C++ admission, sparse coverage,
CPU rejection and cancellation failure. Full source-neighbor selection, a checked
smaller grid, worker-protocol/job integration, resume, fusion, compressed scene
storage and measured geometry remain M3 work.
