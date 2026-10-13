# Appearance worker recipes

## Executed local development recipe

`fit.py` implements the explicit `local_gsplat_v1` recipe from
[ADR 003](../../docs/adr/003-local-gsplat-development-recipe.md). It uses real
gsplat 1.5.3 rasterization and `DefaultStrategy` on NVIDIA, degree-3 SH, sparse
SfM positions, training-only observation colors and Adam. It has completed a
7,000-step fit of the consented replacement capture, produced a checked scene,
and displayed that scene natively. This is not a Q2/Q3-certified preset.

Tested tuple: Python 3.10.22, PyTorch 2.4.1+cu124, gsplat 1.5.3+pt24cu124,
official pycolmap 3.12.6, NumPy 1.26.4, Pillow 11.3.0, SciPy 1.15.3. Exact
installed versions are in [local-environment.txt](local-environment.txt). This
inventory is not a complete distribution-hash lock or release rights clearance.
The [installed-byte receipt](../identity/local-development-20261013.json) and
[audit protocol](../../docs/protocols/worker_identity.md) now bind the local
version/file/notice inventory; they do not supply the missing wheel archives or
metric/backbone qualification. Audit is independent of fitting and does not alter
recipe sources, ownership or compatible optimizer checkpoints.
The official prebuilt wheel avoids rebuilding CUDA kernels with this host's
CUDA 13.4/GCC 16. Dependencies must be explicitly installed before processing.

```sh
uv venv --python 3.10 .venv-train
uv pip install --python .venv-train/bin/python torch==2.4.1 torchvision==0.19.1 --index-url https://download.pytorch.org/whl/cu124
uv pip install --python .venv-train/bin/python -r workers/gsplat/local-environment.txt
git clone --branch v1.5.3 --depth 1 https://github.com/nerfstudio-project/gsplat.git .deps/gsplat
```

Run using the same native process/cancellation commands as other workers:

```sh
build/dev/we_process fit --source .deps/gsplat --data JOB/POSE_STAGE/training --video VIDEO --output FIT_JOB --steps 7000 --max-image-dimension 1280 --max-splats 2000000 --worker-python .venv-train/bin/python
build/dev/we_process compile-fit FIT_JOB/result.json OUTPUT.wexplore
build/dev/we_viewer --scene OUTPUT.wexplore --source-view --max-splats 500000
```

Image resizing is mandatory and explicit; its per-axis calibration transform,
image hashes, original dimensions, dataset split and worker source hashes are
recorded. Training uses the first 48 of the example's 63 frames, two guard frames
and a final 13-frame temporal holdout. Poses use the full sequence. This does not
substitute for independent-video Q3. LPIPS remains unavailable, never zero.

SSIM uses the specified 11×11 Gaussian window/sigma 1.5 in full float precision.
TF32 is disabled because its moment subtraction corrupted low-contrast SSIM;
the CUDA regression compares against an independent double-precision formula.
The earlier failed fit/checkpoint remains separate from corrected results.

Ctrl+C/termination preserves a complete local checkpoint (parameters, six Adam
states, scheduler, strategy accumulators and CPU/CUDA RNG). Resume with the same
arguments plus `--resume FIT_JOB/checkpoint.pt`; only deliberately selected local
checkpoints are supported, loaded with `weights_only=True`. Input/config/source
hash changes reject resume. A PLY is never treated as an optimizer checkpoint.
Fit has a one-hour execution budget and bounded image cache. Growth exceeding
the explicit transient splat budget fails without trimming or silently lowering
quality. Completed assets are content-addressed; `result.json` publishes last.

## Upstream benchmark candidate — unexecuted

`train.py` wraps upstream gsplat **1.5.3**, commit
`937e29912570c372bed6747a5c9bf85fed877bae`, trainer SHA-256
`79319e1cd7404e4d1ba0c425634235c39e6054f0643b904a01feea6179462c05`.
It does not implement or substitute a fake optimizer.

Provision an isolated environment using that commit's example requirements and a
PyTorch/CUDA/compiler tuple supported by those dependencies. The examples use
rmbrualla's `pycolmap` parser fork; keep it separate from the official pycolmap pose
environment. Set `appearance_python`, `gsplat_source` and the reviewed cached
`alexnet_weights_sha256` in the job request. Set `stop_after` to `appearance`.

Do not execute untrusted checkpoints. The adapter fits from native SfM data, runs
offline, disables the network viewer/video generation, exports an annotated ASCII
PLY and records training provenance. A conservative Gaussian-growth guard fails
before exceeding the configured transient count; it never trims or quietly reduces
quality. Upstream checkpoints do not contain full optimizer/RNG state.

**Unresolved upstream qualification:** full transitive package/hash lock, CUDA extension
build, AlexNet weight rights/hash provisioning, real training execution, temporal
holdouts (the upstream default split is not Q3), baseline parity, memory limits and
Q2 unlimited-corpus recipe. No appearance quality or recovery result is certified.
The wrapper may still expose integration errors until exercised on valid poses.

Primary reference: [upstream pinned examples](https://github.com/nerfstudio-project/gsplat/tree/v1.5.3/examples).
