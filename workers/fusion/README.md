# Observed fusion CPU pilot

Experimental M3 partial TSDF/mesh output from exact native-admitted depth receipts.
Read the [contract](../../docs/protocols/fusion.md) before running. No application
scene update, scale calibration, complete room, material export or walking gate.
Unknown regions remain unknown; no backend/model/data download occurs at run time.

The local-only environment is independent of pose, training and dense workers:

```sh
uv venv artifacts/environments/fusion-cpu --python .venv-dense/bin/python
uv pip sync workers/fusion/environment-lock.txt --require-hashes --python artifacts/environments/fusion-cpu/bin/python
```

Provision only after checking the [selected component/resource plan](../../docs/research/observed_fusion.md).
Existing environments/output directories must never be replaced. Generate and
pin a new `contract.selected_recipe()` JSON and the exact request/receipt hashes.
For a complete native cache union, use `selected_recipe(sequence=True)`; it
computes one pooled extent and one grid with bounded scratch and all-source
consistency, preserving the existing fusion resource limits. The retained
shared GUI/CLI geometry path owns cancellation/reuse and native scene admission;
the fresh-depth terminal adapter remains a separate integration step. The
[sequence evidence](../../docs/validation/2026-10-20-sequence-fusion.md) identifies
the retained real requests and commands without a new MVS solve.

```sh
artifacts/environments/fusion-cpu/bin/python workers/fusion/run.py REQUEST.json SHA256 NEW_DIRECTORY
OMP_NUM_THREADS=4 OPENBLAS_NUM_THREADS=1 artifacts/environments/fusion-cpu/bin/python -m unittest discover -s tests/integration -p 'worker_fusion_test.py' -v
```

The synthetic cases check asymmetric camera/world placement, analytic weighted
surface position, occlusion, support/unknown accounting, optional sigma and an
open missing stripe. They do not certify measured reconstruction quality.
