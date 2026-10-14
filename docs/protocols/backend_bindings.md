# Observed Linux backend and viewer mappings

The [summary](../../third_party/backend-bindings-20261015.json) supplements the
[loader audit](runtime_dependencies.md) with actual fixed backend imports and an
empty native viewer. Its five observations preserve profile, native/sandbox
context, module versions, CUDA availability, exclusions and mapped ELF hashes.
Raw receipts retain local module paths, file identities and owned process IDs;
public records omit those details. No source archive or redistribution rights are
established by a mapped-file observation.

From the repository root:

```sh
python3 tools/check_backend_bindings.py
python3 tools/check_backend_bindings.py --local-evidence
python3 tools/check_dependency_manifest.py --local-evidence
```

Default checks bind the observer/baseline source, complete profile/node inventories,
counts, byte sums and baseline differences. Local checks additionally bind every
raw receipt, reconstruct its exact public projection and rehash current backing
files. A changed build, library or observer fails; capture fresh evidence rather
than editing a hash to erase a failure. The dependency manifest binds this summary;
its strict closure check retains all earlier source/rights gaps. Check exit **0**
means scoped evidence consistency, **2** means invalid/stale evidence.

For a fresh import observation, create a dedicated work directory under ignored
`artifacts/`, select the matching existing environment and a new receipt path:

```sh
mkdir -p artifacts/backend-probe/run-001/work
env -i PATH=/usr/bin:/bin LC_ALL=C PYTHONDONTWRITEBYTECODE=1 \
  HOME="$PWD/artifacts/backend-probe/run-001/work" \
  TMPDIR="$PWD/artifacts/backend-probe/run-001/work" \
  XDG_CACHE_HOME="$PWD/artifacts/backend-probe/run-001/work/cache" \
  TORCH_HOME="$PWD/artifacts/backend-probe/run-001/work/torch" \
  TORCH_EXTENSIONS_DIR="$PWD/artifacts/backend-probe/run-001/work/extensions" \
  TRITON_CACHE_DIR="$PWD/artifacts/backend-probe/run-001/work/triton" \
  WE_BINDING_WORK="$PWD/artifacts/backend-probe/run-001/work" \
  OMP_NUM_THREADS=2 MKL_NUM_THREADS=2 \
  .venv-train/bin/python -B tools/probe_backend_bindings.py \
  --profile training --output artifacts/backend-probe/run-001/training.json
```

Profiles are fixed: training imports torch, NumPy, Pillow, SciPy spatial, pycolmap,
gsplat/prebuilt `gsplat.csrc` and Triton/libtriton; poses imports NumPy/Pillow/
pycolmap using `.venv-worker`; metrics-cpu imports torch/torchvision/TorchMetrics
using `.venv-metrics`. Direct prebuilt extension import avoids gsplat's optional
JIT fallback. No tensor allocation, optimizer, model construction or checkpoint
load is requested. CUDA queries report availability/count and PyTorch context
initialization separately. Native access is required for a device observation;
sandbox unavailability does not mean the host lacks a GPU.

The Python audit policy rejects process/compiler launches, Internet socket
connects/DNS, pickle class resolution, known checkpoint/video filename suffixes
and file-open writes outside the selected work directory. `torch.load` is disabled
immediately after torch import. This is a diagnostic guard on Python audit events,
not a native-code security sandbox or a guarantee about every filesystem API.
Capture runs only the already selected local packages. It never provisions them.

Linux [`/proc/PID/maps`](https://man7.org/linux/man-pages/man5/proc_pid_maps.5.html)
provides mapping permissions and device/inode/path identities. The collector
checks current regular backing files against those identities before hashing,
checks opened-file/path state through the read and rechecks mappings/backing state
before publication. Paths are constrained to this repository, `/usr` and
`/opt/cuda`. Anonymous/virtual, deleted/ambiguous, non-ELF/device and outside-root
regions are explicitly counted. Deleted suffixes and escaped pathnames are not
treated as reliable file identities. Limits: **8 MiB / 16,384 regions / 512 files /
4 GiB per ELF / 16 GiB total**. Receipts publish atomically without replacing an
existing entry; exit **0** is a completed observation, **2** an input/capture
failure, **3** a failed import or guard denial.

The dated [viewer observer recipe](../validation/2026-10-15-backend-bindings.md)
launches only its owned empty viewer with isolated settings and input trace,
collects maps after the first rendered frame, then waits for normal exit. It does
not attach to another process, open a scene or invoke a worker. Mapping is evidence
of a loaded backing file; it does not prove symbol binding, plugin selection,
relocated/private memory contents, every lazy kernel/provider/codec path, rendered
scene fidelity or reference performance. The tested viewer's generic
`fixture: user-selected` output is labeled **empty viewer** in this evidence.

The [installed owner supplement](binding_owners.md) associates the 137 newly
observed ELF files with current worker/system metadata and notice bytes. It checks
that subset independently; it does not upgrade stale overlapping runtime evidence.
