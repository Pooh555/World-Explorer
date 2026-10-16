# Isolated CUDA MVS pilot — 2026-10-17 Asia/Hong_Kong

Scope/status: **M3 IN_PROGRESS**. Native backend execution, depth interchange and
pose admission are checked on this Linux/NVIDIA host. The three-view result is
partial, with very low coverage; no complete geometry, Q5/N2/P1 or release gate
is certified. Parent **d6eba9a**; implementation is the commit containing this
report. Cwd: repository root. Ignored evidence root **R**:
`artifacts/validation/m3-dense-worker-20261017`.

## Selected backend and identity

[Official COLMAP guidance](https://colmap.github.io/pycolmap/index.html) identifies
the Linux `pycolmap-cuda12` package. The selected 4.2.1 CPython 3.12 wheel's SHA
is `5d9b4ecd575481a03827586b4829fff1857dc6fd3aa63b2d60be590d84ac77dc`.
[PyPI-published provenance](https://pypi.org/project/pycolmap-cuda12/4.2.1/#files)
binds that digest to `colmap/colmap` publishing commit
`bd1fcf654d2dd8fefa1466999c190a246f83f4b9`; independent Sigstore verification is
not performed. The [compact receipt](../../workers/identity/dense-development-20261017.json)
binds all **six selected archives / 167,855,846 bytes**, requirements/installer
lock and installed receipt. Separate `.venv-dense`: CPython **3.12.15**, COLMAP
**4.2.1**, NumPy **2.2.6**, Pillow **11.3.0**, CUDA toolkit metadata **12.9.2.0**,
CUDA runtime **12.9.79**, cuRAND **10.3.10.19**. All **1,188 listed files /
432,780,736 bytes**, six RECORD inventories and eight notice entries check;
before/after installed receipts are byte-identical.

Archives were fetched from checksum-selected official PyPI URLs, inspected, then
installed offline; no weights/download fallback. COLMAP's notice covers its core,
not all bundled/static components. GNU runtime/CUDA and other transitive rights,
source/SBOM completeness and redistribution remain unqualified. No release
manifest is upgraded. Existing CPU pose/training environments are unchanged.

## Actual native outputs

Device: **RTX 4060 Laptop GPU, 8,188 MiB, driver 615.78.08**. Sandbox NVIDIA
inspection fails; native access succeeds, so no GPU-absence inference is made.
The isolated [pilot](../../workers/dense/README.md) checks immutable inputs and
copies them to an exclusively owned workspace. It pins the complete PatchMatch
configuration, worker code bundle, requirements, canonical poses, models and
original/calibrated images. Native grid, five iterations, geometric filtering and
two consistent source views are retained; only operational cache/thread/device
settings differ from backend defaults. Assets publish exclusively before the
atomic terminal manifest. No scene or collision update occurs.

The first synthetic run aborts when writing to absent output directories; its
log is retained (`synthetic-native.log`, exit **134**). After correcting fixture
preparation, a textured **160×120**, three-camera plane at known **Z=4** runs in
**5.463 s**. Each output has **15,334–15,665** valid pixels, median Z
**3.999938–3.999953**. Per-image axial absolute-error p95:
**0.001602 / 0.001328 / 0.001857**; p99 **0.007239 / 0.012362 / 0.060752**.
Explicit synthetic bounds are [3,5]; real processing does not reuse those bounds.
The synthetic project-format export is labeled `SYNTHETIC_DIAGNOSTIC` and passes
native inspection. Upstream [graph source](https://github.com/colmap/colmap/blob/4.2.1/src/colmap/mvs/consistency_graph.cc)
and the asymmetric test verify **column then row**, not a transposed interpretation.

Real input is the unchanged saved sample_1 solve, selecting ordered indices
**[0,4,8]** (frames **1/5/9**) at **4104×2208** with the other two selected views
as sources. This is an explicit qualification subset, not the full workload.
Outer wall time **172.083 s**, worker MVS/export time **171.229 s**; no warmup,
repetition or reference-performance claim. Sampled peak RSS **1,392,812 KiB**,
observed HWM **1,602,308 KiB**, global device usage **1,110 MiB** including the
competing desktop; samples are not complete allocation peaks. Supervisor timeout
is 900 seconds and is not reached. Exact argv/resources: `real-execution.json`.

| Frame | Valid pixels / 9,061,632 | Valid fraction | Project depth SHA-256 |
|---|---:|---:|---|
| 1 | 71,483 | 0.788853% | `bc8c62e364c5adcb57f02017c414b998f40b2189765d51b0ca5bd0f8cad58f1f` |
| 5 | 94,414 | 1.041909% | `9c5067ac8681572aaec01d92dd4f65f187ecfa74a88e1d28288789fe85351864` |
| 9 | 189,880 | 2.095428% | `778630abc1b8a98e513e0db4506838dbc4bc0c1d0daecca266e221894d422d3f` |

Valid pixels have **three actual observations including the reference**, relative
weight 1 under the fixed source-fraction recipe, and **unavailable uncertainty**.
Everything else remains explicit unknown with canonical +0 planes. Arbitrary
camera units never become metres. No coverage/quality threshold is lowered.
Input manifest SHA:
`4ecb2975f79b1507cb1f1fbf0c697f9942da3d6fb56941d898f40f9445935961`;
full recipe SHA:
`aaaf08a712fa1a87decc34af1ae33d8bb9a22c8a967d4a7a03e3a4b7323adbeb`;
terminal SHA:
`c12161585cfa46c3fae3f298ae9c62278c5ddf2d576b70f7351ccb7b3de067d2`.

## Checks and limitations

All commands below exit **0** unless explicitly stated. GCC **16.2.1 C++26 Debug**
and Clang **23.1.1 explicit C++23/libstdc++ Debug** profiles are used.

- `UV_CACHE_DIR=.deps/uv-cache uv pip compile workers/dense/environment.in --python
  .venv-worker/bin/python --generate-hashes --output-file workers/dense/environment-lock.txt`;
  public metadata/archive fetches via `R/fetch_metadata.py`, `R/fetch_wheels.py`;
  `uv venv` and offline `uv pip install`: exact selection/install logs in R.
  Initial sandbox DNS failures remain recorded; native network access succeeds.
- `.venv-dense/bin/python tools/audit_worker_environment.py --requirements
  workers/dense/environment.txt --output R/installed.json` and separate final
  receipt: all identities/requirements match; no unlisted distributions.
- `.venv-dense/bin/python R/make_synthetic.py`, native `R/run_synthetic.py`;
  `python3 R/supervise_real.py`: fixture, options, native logs and measured output
  retain their own identities. Concrete native preparation argv is `prepare-real.json`.
- `python3 R/admit.py`: generated observer compiled against actual C++ application
  libraries, `load_dense_inputs` independently repeats preparation, then all **three**
  real assets pass `load_pose_bound_depth`; **three** wrong-frame admissions reject.
  Exact compile/link/run argv in `admit-commands.json`. Initial observer compilation
  omitted the private JSON include path; correcting the observer requires no
  production change.
- `.venv-dense/bin/python R/check_real_outputs.py`: independent NumPy/pycolmap read
  of every exported plane/header and native consistency record, exact zero valid-Z
  difference, identity/unknown/support checks. **15 CLI probes** include GCC/Clang
  byte-equal summaries, expected default 4096-cap rejection, explicit 8192 success,
  and malformed dimension options. `independent-output-checks.json`.
- `cmake --build --preset dev -j 4`; `ctest --preset dev --output-on-failure -j 4`:
  **303/303**, **24.95 s**. Final worker suite after splitting portable read checks
  from Linux-only directory-fsync tests: **178 cases** each on Python 3.10/3.12/3.14,
  including **twelve new synthetic cases**. Final focused CTest worker run also passes.
  `worker-python*-final.log`, `ctest-worker-final.log`.
- `cmake --build build/m2-vma-clang -j 2 --target we_process`; clang-format/tidy
  **23.1.1** with `--warnings-as-errors=*`, include/docs/whitespace checks pass.
  No new native viewer, sanitizer, platform or full coverage run is inferred.

The separate cancellation probe signals at 3 seconds, exhausts a six-second grace
and requires **SIGKILL**, returning **-9** after **6.114 s** from the signal.
No terminal result is published and the prior completed pilot stays byte-identical
(`cancel.json`). This does not qualify cooperative cancellation or the production
worker protocol; the owner must kill/reap an unresponsive backend. CPU-only backend,
occupied-output and bad-manifest probes reject before publishing new workspaces.

After the GPU run, a final admission regression rejects a dangling output symlink
without creating its target. The GPU-tested Python source bundle is retained in
`R/pilot-source`; source comparison shows only that two-line entry guard differs
from the final worker, and the GPU/export bodies are unchanged. The current full
recipe/native manifest are frozen in `final-recipe.json` / `final-inputs.json`;
`final-preflight.json` passes current input admission with **no second GPU solve**.
GPU results above retain their original recipe/code identities and are not
retroactively rebound to the final guard.

Final preservation/identity evidence is `R/audit.json`: **22,019 protected non-cache
files** from saved sources/runs/frozen fixtures and the existing pose/training
environments retain their baseline bytes. No pose solve, appearance refit, long
stress, media upload or push occurs. Current loader/mapping receipts remain stale
and need refresh; the new dense environment is not covered by their old profiles.
M3 still needs broader neighbor/grid preparation, job/protocol integration with
bounded cancellation/resume, fusion, compressed storage, measured scale, collision
and export. A0–A6/AP gates remain unstarted; platform qualification is not begun.
