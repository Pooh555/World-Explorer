# Bounded observed fusion selection — 2026-10-18

Plan §5.1 selects Open3D sparse TSDF storage/extraction. This first CPU pilot
selects **open3d-cpu 0.19.0**, NumPy 2.2.6 and Pillow 11.3.0 in a new isolated
CPython 3.12 environment. No Open3D install or cached wheel was found locally.
The [published CPU wheel metadata](https://pypi.org/pypi/open3d-cpu/0.19.0/json)
offers this interpreter/platform; this is an explicit experimental selection,
not qualification of the newer release mentioned in the plan or any GPU path.

The [VoxelBlockGrid API](https://www.open3d.org/docs/release/python_api/open3d.t.geometry.VoxelBlockGrid.html)
supports sparse attributes and extraction. Its standard integrator does not take
project confidence/support/sigma planes. Use project-owned CPU integration into
these attributes, following the supported
[custom integration interface](https://github.com/isl-org/Open3D/blob/v0.19.0/examples/python/t_reconstruction_system/integrate_custom.py).
Keep explicit positive weight and unknown-space masks; do not equate allocated
blocks with observations. No dataset helpers, model downloads or backend defaults
that fetch data may run. Backend types stay inside the isolated Python worker.

The [core license](https://github.com/isl-org/Open3D/blob/v0.19.0/LICENSE) is MIT;
[third-party notices](https://github.com/isl-org/Open3D/blob/v0.19.0/3rdparty/README.md)
need separate installed-wheel/source closure before redistribution. Installing
for local evaluation does not close dependency/model/dataset rights gates.
Freeze a hash-locked transitive recipe before installing; save installer and
installed-file inventories in ignored evidence. Existing environments stay intact.

Pre-run resource plan: CPU only, four OpenMP threads, at most 18 admitted views /
48 million input pixels, 32,768 blocks of 8³ voxels, chunked projection and
activation, 512 MiB output, 4 GiB observed child RSS and 300 s per real pilot.
The block limit bounds voxel attributes, not process peak. A supervisor must
terminate/reap on RSS/time overrun; no terminal manifest on failure. Run asymmetric
analytic surfaces, occlusion and unknown-boundary checks before real inputs.
Freeze extent/512, truncation four voxels and fixed cross-view gates before
opening outputs. A resource failure is retained, never an excuse to drop views
or silently coarsen resolution. All output remains arbitrary-scale, partial
observed geometry with walking disabled; Q5 needs independent measurements.
