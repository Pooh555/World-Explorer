# Observed sparse TSDF pilot — 2026-10-18 (Asia/Hong_Kong)

M3 remains **IN_PROGRESS**. Both native-admitted retained caches now produce
confidence-weighted sparse TSDF and raw meshes through the isolated Linux CPU
worker. Software previews show fragmented surfaces and large gaps, not complete
room geometry. No scene/collision update, measured scale, Q5/N2 or asset gate.

Repository root is the default cwd. Parent: `5713010`; implementation identity is
the final worker bundle SHA
`fbe011fc6a5b4e645a154730b57d856e7ff9b7a0e1632f28b951b35577d2045e`.
Ignored evidence root `R=artifacts/validation/m3-observed-fusion-20261018` retains
initial runs and `final/source/` frozen source. The [contract](../protocols/fusion.md)
owns exact bounds, weighting, cross-view and publication semantics;
[selection](../research/observed_fusion.md) owns component/terms research.

## Identity and provisioning

`workers/fusion/environment.in` / `environment-lock.txt` select CPU Open3D 0.19.0,
NumPy 2.2.6, Pillow 11.3.0 and complete transitive hashes. Lock SHA:
`893bda37b637c24768d3e37b60f7b907b85f3363b137749ca86e5d278ee4dabc`.
No local Open3D/wheel was found. After freezing the separate environment/resource
plan, actual commands were:

```sh
UV_CACHE_DIR="$PWD/artifacts/cache/uv-fusion" uv pip compile workers/fusion/environment.in --python .venv-dense/bin/python --generate-hashes --output-file workers/fusion/environment-lock.txt
UV_CACHE_DIR="$PWD/artifacts/cache/uv-fusion" uv venv artifacts/environments/fusion-cpu --python .venv-dense/bin/python
UV_CACHE_DIR="$PWD/artifacts/cache/uv-fusion" uv pip sync workers/fusion/environment-lock.txt --require-hashes --python artifacts/environments/fusion-cpu/bin/python
UV_CACHE_DIR="$PWD/artifacts/cache/uv-fusion" uv pip check --python artifacts/environments/fusion-cpu/bin/python
```

Sandbox resolution exited 2 (DNS); native resolution/install/check exited 0.
Logs: `resolve.log`, `resolve-native.log`, `venv.log`, `install.log`,
`pip-check.log`. CPython **3.12.15**, Linux x86_64. `environment.json` inventories
**78 distributions / 16,802 files / 142 notice paths**, verifying listed SHA-256
RECORD hashes; its SHA is
`79f78345edda0fe4b0f4cefdef28fbc744a770061961caa67f2487a9f565b6eb`.
Local-only evaluation; archive/source/third-party redistribution closure remains
open. No model/data download or existing environment installation occurred.

## Real bounded results

Inputs are the [prior native exports](2026-10-18-depth-visibility.md), retaining
every selected view/filter. Final recipe SHA:
`2fa967039db8db0b67d0ea6903b05e933d0f3105ae20253137df273287349aea`.
Immutable command-array definitions are `R/final/commands.json`, SHA
`94d38d08b7e8194a425058e070794bde62280804894138cbf7908f97bf31ad96`;
both commands exit 0, with exact arguments/timing in `*-exit.json` and raw
`temporal.log` / `parallax.log`. No MVS, pose or fitting rerun.

| Observation | Temporal 9 views | Parallax 18 views |
|---|---:|---:|
| Positive valid input observations | 1,537,092 | 2,806,753 |
| Additional fusion-filter accepted observations | 974,231 | 1,451,742 |
| Accepted on unchanged first nine references | 974,231 | 500,226 |
| Accepted / full grid, first-nine per-view range | 2.349–6.539% | 1.535–3.356% |
| Robust arbitrary-unit extent / voxel | 4.707521 / 0.00919438 | 12.230620 / 0.02388793 |
| Allocated blocks / observed voxels | 12,514 / 159,135 | 22,287 / 256,592 |
| Allocated but unknown voxels | 6,248,033 | 11,154,352 |
| Raw vertices / triangles | 32,074 / 47,559 | 61,598 / 82,248 |
| Supervisor seconds / peak child RSS sampled at 20 ms | 7.125 / 932,601,856 B | 18.975 / 1,552,052,224 B |

Final terminal receipts (`R/final/<name>-fusion/manifest.json`):

- Temporal: `f1ee21e793aa3020d02ad4830afffe4ca56eaabc5e27b43e2b191c5b2684d562`.
- Parallax: `94e17ceeeeed24fe4998825929ba2da95dfd2af6e0b8e064aed7a8d42cff262a`.

Single development observations, no warmup/repetitions; other CPU checks ran on
the host. These are not P1/hardware benchmarks. Different extents/resolutions and
view sets prohibit interpreting mesh counts as quality improvement. All real
sigma remains unavailable. Positive local front-band observations never turn
unknown rays/allocated blocks into measured free space. Native asset admission,
component cleanup, simplification, color/materials and complete export remain open.

## Checks, corrections and preservation

| Actual command / command reference | Exit and observation |
|---|---|
| `OMP_NUM_THREADS=4 OPENBLAS_NUM_THREADS=1 artifacts/environments/fusion-cpu/bin/python -m unittest discover -s tests/integration -p 'worker_fusion_test.py' -v` | 0; **8/8**, no skips, 1.134 s; `fusion-tests-final.log`. Analytic asymmetric plane/weighted zero crossing, unknown stripe remains open, optional sigma, occlusion/unknown sources, recipe/schema/hash, block budget/cancel and large-world rejection |
| `R/test-commands.json`: `dense-regression` (argv/env/exact stdin) | 0; **52/52**, no skips, 14.329 s; `dense-regression.log`; existing dense backend environment |
| `R/test-commands.json`: `related-tests`, before final range guard | 0; 59 cases, **8 skips** requiring the separate pycolmap backend; `related-tests.log`. Skips are not passes; all new cases subsequently pass in final focused run |
| `ctest --preset dev -R '^worker_integration$' --output-on-failure` | 0; final **1/1**, 19.67 s, `worker-integration-final.log`; default dependency-light integration scope. No C++ changed/rebuilt |
| `OMP_NUM_THREADS=4 OPENBLAS_NUM_THREADS=1 artifacts/environments/fusion-cpu/bin/python "$R/final/check_outputs.py"` | 0; complete output hashes/schema/counts/finite/index/unknown invariants; **512 scalar voxels per run**, no exclusions, maximum normalized TSDF difference **1.318e-5 / 1.165e-5**. Independent scalar camera/visibility/weight/support arithmetic, not GT; `output-audit.json/log` |
| `python3 "$R/final/check_failure.py"` | 0; wrong request SHA, existing output and cache-overlap reject; SIGSTOP child + TERM parent exits **130**, forced KILL/reap in **2.068 s**, no terminal receipt/survivor; `failure-checks.json/log`, `cancellation.log` |
| `artifacts/environments/fusion-cpu/bin/python "$R/final/preview.py"` | 0; `real-fusion-preview.png`, software orthographic normals-colored raw mesh inspected; fragmented geometry/gaps confirmed, no viewer qualification. Initial helper font lookup failed; corrected font path, retained `R/preview.log` |
| `python3 "$R/audit.py"` | 0; **24,457 protected files**, zero changed/missing, including retained jobs/videos/scenes/fixtures, earlier evidence, pose/train files and **1,188** dense environment files; unchanged seven-file GPU bundle `ead07a16e0bacc6db394af6ca6bcb27bda8401612b33d1b4542786839e97d98f` |
| `python3 tools/check_docs.py`; `git diff --check` | Both 0; **820 local targets / 172 files**, `docs.log`; whitespace clean, `diff-check.log` |

The exact regression command record SHA is
`54864079e77d10cd1fa47aa19b90dc8ddd18d7be8300e4179427dc0903fba6dd`.

Initial seven-case tests and real pilots passed. Final review added a pre-narrowing
lattice range/float32 precision guard for extreme world translations, with an
eighth adverse case. Initial requests/recipes/results remain intact; necessary
current-code replays under `final/` reproduce counts and pass the scalar audits.
No thresholds were relaxed, views removed, or old input/output bytes replaced.
Documentation checks and commit identity are recorded with the final handoff;
native fused-geometry admission is the next executable step.
