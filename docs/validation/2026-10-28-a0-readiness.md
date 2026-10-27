# A0 recipe readiness and capture preparation — 28 October 2026

**Audit complete; A0/AP0 stay IN_PROGRESS; provisioning BLOCKED.** The large-file
observation/CLI prerequisite now exists and replays its synthetic fixtures. The
remaining path is a complete code/environment/build/rights/resource lock and a
real pilot freeze. The user requested a [capture checklist](../a1_capture_checklist.md),
not model acquisition or inference. This slice changes Markdown only; A1–A6/M4
and all quality/navigation/platform gates remain unchanged.

Repository root `/home/Pooh555/programming/World-Explorer` is default cwd;
timezone Asia/Hong_Kong, clean parent `cd649fc3e17187228ee58330252d031ddbb5cc0f`.
Ignored `R=artifacts/validation/a0-readiness-20261028` retains actual commands,
argv/cwd/exit/time, source/host snapshots, dependency records and checksums.
Commands use `python3 R/run_check.py TAG COMMAND...`.

## Observed readiness

| Actual local command/check | Exit / observation; evidence tag |
|---|---|
| Handoff's `python3 tools/check_dependency_inventory.py --local-evidence` | **2**, nonexistent file; corrected to the checker below (`dependency-command-as-written`) |
| `python3 tools/check_dependency_manifest.py` / `--require-closure` | 0 / **3**; **86 components /seven profiles /257 gaps**, rights unqualified (`dependency-default`, `dependency-closure`) |
| Same with `--local-evidence` | **2**, `Current runtime payload differs`; no receipt refreshed (`dependency-local`) |
| `python3 R/audit_local.py` | 0; **984** tracked hashes, **52** retained finish receipts and scoped environment/host metadata (`local-audit-complete`) |
| Actual `we_process inspect-external-models` on both pinned saved requests | 0 /0; reports reproduce exactly: `d904905d…b8cb13`, `77db0357…b75aea` (`large-replay-156`, `large-replay-312`) |
| `we_process validate artifacts/sample_1/scene.wexplore` | 0; scene content SHA remains `c5f19d74260fecbcdcd26ff7b00d57e25006c8e2e572ef2ed0f7f1de3defee2d` (`sample-bundle`) |
| `we_process inspect-corpus` on the unchanged missing-prerequisite request | **2**, independent corpus/seal, captures/commits and recipes missing (`real-corpus-refusal`) |
| Native read-only `nvidia-smi` query | 0; RTX4060 Laptop /615.78.08 /8,188 MiB /compute8.9 (`gpu-metadata`); no inference qualification |

Host metadata observes **15,984,948 KiB RAM**, 20 usable threads and
**326,057,099,264 bytes free** on the repository volume; no reservation/fit proof.
Selected local environments still have no SAM/Mitsuba/Dr.Jit/xatlas/meshoptimizer/
CoACD distribution metadata, and Blender/Godot do not resolve on inspected PATH.
This is a scoped metadata/path check, not an exhaustive system search or import.
Existing training torch **2.4.1+cu124** /torchvision **0.19.1+cu124** is unchanged.
The local asset host remains contracts-only. Latest full-suite and Windows-repair
checks retain their original scopes; none is rerun/promoted by this audit.

## Concrete proposal and newly resolved metadata

The [installation proposal](../asset_installation_plan.md#proposed-linux-control-variant--28-october)
now selects a Linux/CPython3.12 torch **2.5.1+cu124** /torchvision **0.20.1+cu124**
control candidate, with exact publisher hashes and HEAD sizes: **908,202,405** and
**7,287,066 bytes**. Source commit/config/checkpoint expectations remain pinned.
The two wheels plus the expected model are only a **1,071,497,937-byte subtotal**;
whole-plan download/installed/build/staging/scratch costs remain null and blocking.

`primary-collection` reads five bounded primary documents/indexes and HEAD headers;
separate wheel metadata URLs return **404**. `wheel-metadata-ranges` then reads
**1,404,199 +85,289 bytes** from exact selected wheel ZIP directories/records,
requiring 206 responses, exact ranges/ETag/If-Match, bounded inflate and CRC.
METADATA hashes are `b135cb28…56973` and `b7e5d32f…e0e033` in
`R/wheel-metadata.json`. No complete wheel/model/archive was downloaded; no package
tree was materialized, installed or executed; only metadata records were decoded. Partial reads do not verify whole-wheel SHA.

The exact Torch metadata exposes 12 NVIDIA packages, Triton3.1.0 and Python3.12
SymPy1.13.1/setuptools dependencies; transitive artifact/build/notice/size closure
remains unresolved. The pinned SAM build permits extension errors by default and
has unconstrained build dependencies; the proposal makes CUDA/failure flags
explicit. Its builder also permits a null checkpoint and hub downloads. Future
workers must require the pinned local checkpoint/config and forbid those
fallbacks. Primary links and full hashes live in the installation proposal and
`R/primary-snapshots.json`; `R/recipe-readiness.json` records all remaining blockers.

The first scratch audit assumed every historical finish JSON was an object;
one was a list. The read-only helper now handles both; the failed log remains.
No source/old evidence was changed.

`preservation-run` checks all **984** tracked hashes: five intended Markdown
changes, **979 unchanged**, two new Markdown guides/reports. All **378** protected
hashes, **72** large-fixture source files, **52** historical finish receipts,
**25** source-identity receipts, **20** old primary documents, selected binary/
media/bundle and seven new primary/metadata snapshots match. `docs-final` and
`diff-final` pass; `R/preservation.json` and `R/finish.json` retain the full checks.
No code/build/environment changes occurred; C++/Python formatting, rebuild and
sanitizer reruns are inapplicable to this Markdown-only slice. Native Windows
confirmation remains pending after the separate CRT repair. The next executable
step is the transitive artifact lock in [progress](../progress.md), not M4.
