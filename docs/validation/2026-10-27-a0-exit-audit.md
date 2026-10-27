# Refreshed A0 exit audit — 27 October 2026

**Audit complete; A0/AP0 remain IN_PROGRESS.** The bounded native product
contracts added since the [26 October audit](2026-10-26-a0-exit-audit.md) exist
and have retained local evidence. They do not establish an executable asset
recipe, real pilot freeze or production qualification. Provisioning remains
**BLOCKED**. The first independently executable prerequisite is a distinct
large-artifact verification boundary, before model acquisition.

Cwd: `/home/Pooh555/programming/World-Explorer`; timezone Asia/Hong_Kong.
Initially clean parent `18d88bd0d859e2e4826735f487db48efa2623cf3`.
`R=artifacts/validation/a0-exit-audit-20261027` (ignored) contains command/result
JSON, logs, selected host metadata and preservation hashes. This slice changes
Markdown only; no specification, code, build, environment or model is changed.

## Specification and current implementation

Reviewed plan §§11.3–11.4/11.6–11.8, current public APIs and implementations in
`asset_catalog`, `scene_store`, `application`, `collision` and `workers`, their
protocols and retained validation receipts. The old audit's absent record/export
claims remain historical; the current coverage is:

| Boundary | Implemented bounded coverage / remaining exit work |
|---|---|
| Definition/instance/observation/provenance | Typed metadata, per-face/channel evidence, owned revision history and exact native adoption exist. Native adoption still refuses opaque room/support/lineage fields; automatic separation/association and broad prop/structure relations remain open |
| Geometry/material/structure/completion | Native high/runtime correspondence, actual channels/fit pins, partial patch/loop/opening/room graphs and exact observed-anchor protection now exist. These validate supplied declarations/arrays; automatic estimators, completion/UV/bake/material producers and real quality evidence remain open |
| Export/display/LOD/collider | Shared textured glTF/catalog/assembly, explicit LOD companions, schema-6 native PBR display, exact static structural collider input, Jolt loss checks and CLI queries exist. General decimation, convex props/contact/live physics, ordinary Godot import and real move/remove/changed-light evidence remain open |
| Recipe/capture/resource freeze | Recipe/component/review/installed-tree and corpus-v7 metadata contracts exist. Complete installed/native runtime closure, independent reviews/costs, asset-worker implementation and enforced/measured budgets remain open. Current worker timeout/output/frame and thread-environment controls do not prove asset RSS/VRAM/network-budget enforcement |

Latest [collider integration evidence](2026-10-27-collider-diagnostic.md) retains
**917/917 C++26**, **892/892 C++23**, **37 sanitizer cases /48,837 assertions**
and actual synthetic CLI products. Its selected source identity still matches
current files. `R/latest-evidence.json` checks those retained records; these
tests/GPU products were **not rerun or upgraded** by this audit.

## Concrete pre-installation gap

The [installation proposal](../asset_installation_plan.md#artifact-verification-boundary-before-acquisition)
retains the original author-metadata expectation for SAM 2.1 tiny:
**156,008,466 bytes /148.78 MiB**, expected SHA
`7402e0d864fa82708a20fbd15bc84245c2f26dff0eb43a4b5b93452deb34be69`.
It exceeds the current **16 MiB/file** evidence limit and **64 MiB** receipt/
installed-tree totals. `R/artifact-boundary-gap.json` binds the inspected source
files, arithmetic and original metadata receipt
`55215530ebcf937bb3197e2d663ce80d2437ea06efc5e6ce925e02c2d9cda152`.
This is not a downloaded-payload verification or executed large-model test.

Therefore the small-fixture v1 APIs cannot currently admit even this proposed
checkpoint, let alone a complete environment. Preserve their limits/encodings.
The next step is separately versioned streamed, bounded file observation with
independent identity/size/path selection, change/cancellation/alias refusal and
caller preservation. Mutable observations must not become immutable-byte or
execution authority. Unknown whole-plan costs, reviews, runtime closure and
resource fit remain blockers after that work; no installation is authorized.

## Actual local recheck

Native GPU: RTX4060 Laptop, driver **615.78.08**, **8,188 MiB**, compute **8.9**.
RAM: **15,984,952 KiB** (~15.24 GiB), **20** usable threads. Repository volume
free space: **329,775,276,032 bytes** (~307.13 GiB), a transient observation,
not a reservation or fit proof. The host remains contracts-only for asset inference.

The scoped `.venv*` plus known fusion/dense environment metadata still contains
the existing training/pose/dense/metric/fusion versions, with no selected
SAM/Mitsuba/Dr.Jit/xatlas/meshoptimizer/CoACD metadata. `artifacts/install/assets`
does not exist; Blender/Godot do not resolve on inspected PATH. These are scoped
metadata/path observations, not exhaustive system/cache absence or import tests.
No remote reference was refreshed; the 26 October immutable reference collection
remains dated research, with costs/reviews still unknown.

| Actual command (repository cwd) | Exit / result | Evidence under `R` |
|---|---|---|
| `python3 R/audit_local.py` | 0; 970 tracked hashes, 38 finish receipts and nine source-identity receipts recorded | `local-audit-run`, `local-audit.json`, `host-local-final.json` |
| `nvidia-smi --query-gpu=name,driver_version,memory.total,compute_cap --format=csv,noheader,nounits` | 0; native read-only observation above | `gpu-native` |
| `python3 tools/check_dependency_manifest.py` / `--require-closure` | 0 / **3**; 86 components, seven profiles, **257 open gaps**, rights false | `dependency-default`, `dependency-closure` |
| Same checker with `--local-evidence` | **2**; `Current runtime payload differs` | `dependency-local`; stale receipts remain stale |
| `build/dev/we_process validate artifacts/sample_1/scene.wexplore` | 0; saved scene/revision/chunk preserved, captured appearance only | `sample-bundle` |
| `we_process inspect-corpus` with the unchanged explicitly empty-prerequisite request | **2**; missing independent corpus/seal, captures/commits and recipes | `real-corpus-refusal`; request SHA remains `2ceafd40c6c465f9bca1933b64e81ee1b13a67f04d7adb829f9e53806718f2f3` |
| `python3 R/check_preservation.py` | 0; 970 tracked hashes: five intended Markdown changes, 965 unchanged; one new Markdown report; 378 protected hashes, 38 finish receipts, nine source-identity receipts and 20 primary documents preserved | `preservation-run`, `preservation.json` |
| `python3 tools/check_docs.py` / `git diff --check` | 0 /0; 1,727 local targets in 324 files; clean whitespace | `docs-final`, `diff-final` |

The saved bundle content SHA remains
`c5f19d74260fecbcdcd26ff7b00d57e25006c8e2e572ef2ed0f7f1de3defee2d`;
it supplies no new consent, independent scale or A1 reference evidence.
Final preservation checks retain all **378 protected hashes**, the selected
binary/media/bundle and historical receipts. Documentation/whitespace checks are
recorded in `docs-final` / `diff-final`. No install, model download/import, solve,
new render, runtime receipt replacement, upload, push or platform qualification
ran. C++/Python formatting, build and sanitizer reruns are inapplicable to this
Markdown-only diff. A1–A6/M4 and all AP/Q/N thresholds/statuses remain unchanged.
