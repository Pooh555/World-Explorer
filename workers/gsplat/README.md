# Pinned appearance adapter — not yet executed

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

**Unresolved qualification:** full transitive package/hash lock, CUDA extension
build, AlexNet weight rights/hash provisioning, real training execution, temporal
holdouts (the upstream default split is not Q3), baseline parity, memory limits and
Q2 unlimited-corpus recipe. No appearance quality or recovery result is certified.
The wrapper may still expose integration errors until exercised on valid poses.

Primary reference: [upstream pinned examples](https://github.com/nerfstudio-project/gsplat/tree/v1.5.3/examples).
