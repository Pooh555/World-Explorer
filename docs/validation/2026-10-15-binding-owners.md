# Newly mapped ownership/notices — 2026-10-15 Asia/Hong_Kong

Scope/status: **M1/E2 IN_PROGRESS**. Installed ownership/notice associations for
the 137 newly observed ELF files are checked; archives, bundled terms, rights,
reference hardware and full gates remain unqualified. Cwd: repository root;
parent `aa9c29b`; implementation is the local commit containing this report.
Raw ignored evidence: `artifacts/validation/binding-owners-20261015/` (**R**).

## Scope and identities

[Public summary](../../third_party/binding-owners-20261015.json) SHA-256:
`fa8e07762276bb6fb8e7844e33570268be29371c79739b82102efa175d3d984a`.
It binds five audit sources, the preceding mapping/selection, three original
installed receipts, the package tool and immutable `R/owners-final-checked.json`.
`R/identity.json` binds additional test/runner/manifest and evidence identities.

| Association | Checked result |
|---|---|
| Worker distributions | **117 ELF files / ten distribution instances**: training NumPy/Pillow/SciPy/torch; pose NumPy/Pillow; metrics NumPy/Pillow/torch/torchvision. All selected ELF bytes match retained verified RECORD hashes/sizes; current METADATA/RECORD and **25 notice files** match original receipts |
| System packages | **20 ELF files / six packages**: libdecor 0.2.5-1, libxcb 1.17.0-1, libxkbcommon 1.13.2-1, nvidia-utils 615.78.08-1, vulkan-validation-layers 1.4.363.0-1, wayland 1.26.0-1; current owner/metadata/list queries agree on repetition, **five installed notices** hashed |
| Unresolved associations/terms | **Zero unattributed selected ELF files**. Validation-layer package declares Apache-2.0 but has no separate notice in its queried installed license paths. **49 association gap entries** retain archive/source/bundled-term limits; no rights clearance |

Union: **137 ELF files / 579,037,910 bytes / 16 owner groups / 30 notices /
64 read-only package queries**. File bytes are inventory, not RSS or GPU memory.
Original wheel/source locks and rights statuses are retained; this association
does not infer that an archive is absent merely because it is not bound here.
The [protocol](../protocols/binding_owners.md) documents capture/check boundaries.

## Observed system drift

The initial full local dependency check fails before implementation on absent
`/usr/lib/libsnappy.so.1.2.2`. Read-only comparisons find snappy
**1.2.2-3 → 1.3.1-1** (current file `libsnappy.so.1.3.1`) and zlib
**1:1.3.2-3 → 1:1.3.2-4**, with changed bytes at `libz.so.1.3.2`.
`R/current-payload-drift.json` retains expected/current hashes, missing paths and
actual package-version query argv/exits/output. Of the old loader's 213 files,
one changed and one disappeared; of the old mapped 208, one changed. **All 137
new association targets retain their original hashes**. These observations do
not determine who changed the system packages or when.

Old loader/mapping summaries and raw receipts are untouched. The new supplement
checks the unchanged new targets independently; it does not silently update zlib
or substitute the new snappy path. Full local validation remains failed. Fresh
runtime observations are the next executable task.

## Actual verification

Arch x86-64, GCC **16.2.1**, CMake/CTest **4.4.4**. `R/run_checks.py` and
`*-commands.json` retain argv, cwd, source hashes, exits, times and log paths.

| Commands, from root | Actual result / logs in R |
|---|---|
| `python3 tools/audit_binding_owners.py --output R/owners-final-checked.json` | **0**, all 137 associations; `capture-0.log`, immutable raw receipt |
| Owner summary check, default then `--local-evidence` | Both **0**, complete raw/current projection and fresh package queries; `owners-{0,1}.log` |
| Capture targeting the existing checked receipt | Expected **2** before queries, unchanged receipt hash; `existing-output-command.json` / `.log` |
| Default manifest check; default `--require-closure` | **0 / expected 3**, original **249** source-component gap entries remain; `manifest-0.log`, `closure-0.log` |
| Local manifest check, with/without `--require-closure` | Both **2**, absent old snappy file after successful new-owner checking; `manifest-{1,2}.log`. A stale evidence failure takes precedence over closure reporting |
| `python3 R/run_checks.py focused`; `workers` | **15/15 new / 132/132 worker cases** each on Python **3.14.8 / 3.12.15 / 3.10.22**, no local skips; corresponding logs |
| Dev configure/build `-j 4`/CTest | All **0**, C++26 **260/260**, 60.73 s; `dev-{0..2}.log` |
| `compat-cxx23 -DWE_BUILD_VIEWER=OFF` configure/build `-j 4`/CTest | All **0**, explicit C++23 CPU **242/242**, 59.86 s; `compat-{0..2}.log` |
| Docs / boundaries / diff | All **0**; `static-commands.json` / logs |

Synthetic cases check changed ELF/metadata/RECORD/notices, post-hash changes,
package-database drift, ambiguous/unowned files, receipt rebinding, missing RECORD
hashes, independent budgets, omitted owners/notices, forged rights, namespace/
prefix escape, missing private evidence and immutable publication. Preliminary
receipts keep earlier source identities; only the final checked receipt feeds the
public summary. Current-query verification never imports a worker/backend.

**2,088 protected files / 94 tracked src/include files** match the prior saved-run
baseline/parent (`R/preservation.json`). Videos, completed scenes, failed stages,
recipes, installed worker environments, instructions/specification and earlier
receipts are preserved. No installation, reconstruction, model/checkpoint load,
GPU/UI run, upload or push. C++ formatting/tidy/sanitizers were not rerun for this
Python audit slice. Windows fixture rerun and full platform/performance/rights
qualification remain pending; [progress](../progress.md) owns the next task.
