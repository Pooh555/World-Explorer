# A0 exit and installation-plan audit — 26 October 2026

**Audit complete; A0/AP0 remain IN_PROGRESS.** Existing synthetic admission and
recovery evidence supports the implemented slices, not the full §11.4 schemas,
an executable asset recipe/corpus freeze or a real product. Provisioning is
**BLOCKED** by missing component/cost/review/fit bindings. Independent CPU contract
work remains executable. A1–A6/M4 and all full AP gates retain their statuses.

Cwd: repository root; timezone Asia/Hong_Kong. Parent
`10dc6b140b49fbce591dbf8ce5151a3db817a91c`; initially clean.
`R=artifacts/validation/a0-exit-audit-20261026` (ignored). This slice changes
documentation only. The [installation proposal](../asset_installation_plan.md)
owns component references, explicit unknowns and the gated installation sequence;
the specification and existing protocols remain authoritative.

## Contract and evidence findings

Reviewed plan §§11.2–11.4/11.6–11.8, current public contracts, native implementations,
worker entry points, 23 retained A0 finish records and the
[last integration evidence](2026-10-26-corpus-inspection.md). Its 739/714 CPU tests
and 99-case sanitizer pass remain historical results; they were not rerun here.

| Specification boundary | Actual coverage / remaining work | Status in this scope |
|---|---|---|
| §11.3 dependency/revision/recovery | Closed DAG, owned receipts, immutable metadata publication, exact replay, declared manual ancestry/actions and draft attempts have CPU fixtures. Actual asset workers, their checkpoints and complete semantic invalidation/enforced limits remain open | VERIFIED implemented CPU slices; full AP0 IN_PROGRESS |
| §11.4 definitions/instances/observations/provenance | [Typed metadata](../protocols/asset_definitions.md), observation/capture roles, regions and owned histories exist. Room/support refs and many payloads remain opaque; no live asset assembly transaction follows from metadata admission | VERIFIED bounded metadata slices |
| §11.4 `geometry_product` / `material_product` | [Generated static profile](../protocols/asset_definition_edits.md) admits ≤512 vertices/faces and one generated constant material. Full observed/high/runtime geometry, correspondence/uncertainty, tangents/groups, topology/UV evidence and texture-channel/fit-light/exposure/relighting profiles are absent. M3 mesh data is not this full asset contract | IN_PROGRESS |
| §11.4 `structure_graph` / `completion_product` | No typed native room/opening/support graph or completion/observed-anchor/alternative-result contract was found in the reviewed source roots. Generic product refs/region labels do not decode those records | NOT_STARTED full record contracts |
| §11.4 reusable `export_manifest` | Existing [captured-unlit GLB](../protocols/gltf.md) is a restricted M3 profile. Shared catalog/assembly PBR mappings, LOD/collider/rights sidecars, engine resource reuse and ordinary Godot import remain open | NOT_STARTED asset export contract |
| §11.6 capture/evaluation freeze | Typed scale, inventory, temporal/path, controlled references, envelopes and protocol masks/configs plus corpus v7 are synthetic CPU contracts. No independently supplied real pilot/release corpus, source consent/measurement/reference set or executed masked metric runner is qualified | VERIFIED synthetic slices; real freeze IN_PROGRESS |
| §11.7 recipe/installation | [Explicit proposal](../asset_installation_plan.md) now names source-reference commits, one checkpoint metadata identity, isolation and stop conditions. Full archive/environment/tool inventories, installed sizes, independent reviews and stage fit/enforcement are missing | IN_PROGRESS; provisioning BLOCKED |

These gaps follow the actual implementations in `asset_catalog`, `scene_store`,
`application`, `schemas` and `workers`, including
[`static_payloads.hpp`](../../include/world_explorer/asset_catalog/static_payloads.hpp).
No graph/receipt declaration can supply the missing semantic schemas or qualify
an algorithm. Native PBR rendering and Godot product checks remain A1 work; platform
qualification remains after locally validated A0–A5.

## Current host, selected data and references

The native GPU query reports RTX 4060 Laptop, driver **615.78.08**, **8,188 MiB**,
compute capability **8.9**. RAM is **15,984,956 KiB** (~15.24 GiB), 20 usable CPU
threads; the sampled repository volume has **331,141,640,192 free bytes** (~308.40
GiB). Free memory/disk are transient observations, not reservations or fit evidence.
This is below proposed H_AP24/H_AP48 capacities; individual bounded control
experiments still require their own installed, measured profile.

The final scoped metadata scan includes repository `.venv*` and the known
`artifacts/environments/{fusion-cpu,dense-clean-20261020}` trees. It finds preserved
torch 2.4.1 training/metric profiles, pycolmap/pycolmap-cuda12 and Open3D CPU 0.19.0
fusion. It finds no SAM/Mitsuba/Dr.Jit metadata in those selected trees; Blender/
Godot resolve nowhere on the inspected PATH. This is not an exhaustive system or
user-cache absence claim, nor a package import/runtime qualification.

`we_process validate artifacts/sample_1/scene.wexplore` exits 0: scene
`149348248299280656`, revision 1, one chunk; content SHA-256
`c5f19d74260fecbcdcd26ff7b00d57e25006c8e2e572ef2ed0f7f1de3defee2d`.
The selected source record still binds media SHA-256
`51263d9991dcbc2136d0809ecba1a8ba1b261896f3b222b7afba19784a153ef6`.
This reopens saved captured appearance only. No new source-rights, independent
scale, pilot/reference-capture or completed/PBR result is inferred. The prior
explicit missing-corpus request remains a refusal, not a real corpus admission.

Nine public repositories supplied **20 reference documents /216,064 bytes** at
recorded commits. Official SAM 2.1 tiny model-card/tree metadata additionally pins
an expected checkpoint size/hash in the proposal. All 20 document hashes were
verified locally; checkpoint payload bytes and full source archives were not
retrieved. These reference snapshots are not installed locks or independent rights
reviews. Current setup/terms confirm the documented advanced-device and restricted
renderer gaps; immutable primary links are in the proposal and research addendum.

## Actual commands, identities and limits

| Command / observation | Exit / result | Raw evidence under `$R` |
|---|---|---|
| `python3 "$R/audit_local.py"` before tracked edits | 0 / 867 tracked hashes, 23 retained finish identities, selected metadata/media check | `local-audit.log`, `local-audit.json`, `tracked-before.json`, `host-local-final.json` |
| `nvidia-smi --query-gpu=name,driver_version,memory.total,compute_cap --format=csv,noheader,nounits` | 0 / native read-only device query | `gpu-native.txt` |
| `build/dev/we_process validate artifacts/sample_1/scene.wexplore` | 0 / saved bundle loads | `sample1-bundle.log` |
| `python3 "$R/collect_primary.py"` | 0 / nine commit/document snapshots; zero collection errors | `primary-collection.log`, `primary-snapshots.json`, `primary/` |
| `python3 "$R/collect_checkpoint_metadata.py"` | 0 / public card/tree only; zero errors | `checkpoint-collection.log`, `sam2-checkpoint-metadata.json` |
| `python3 "$R/check_preservation.py"` | 0 / 867 tracked hashes checked: five intended Markdown edits, 862 unchanged; 23 prior finish records and 378 protected hashes unchanged; 20 primary documents rehashed | `preservation.log`, `preservation.json` |
| `python3 tools/check_docs.py` | 0 / 1,544 local targets in 290 files | `docs-final.log` |
| `git diff --check` | 0 / clean whitespace | `diff-final.log` |

Reference receipt SHA-256:
`a04f2d7e27ed79fd495a6a8db1a9738d64d76076caebf588c9f1f63908ec6997`;
checkpoint-metadata receipt:
`55215530ebcf937bb3197e2d663ce80d2437ea06efc5e6ce925e02c2d9cda152`;
local-audit identity:
`c7bdf5526b1740e4aeb51ba567b21c5dee361fdbc9561696bda570cf45b728e7`.
Collection caps each response at 512 KiB and uses at most four concurrent requests.
Native permission was used for the device query and public metadata retrieval.

Corrections/limits: the initial `.venv*`-only probe omitted the two known artifact
environments; the retained final scan includes both. Mitsuba's hosted installation
page was inaccessible through the browser; pinned source README/license documents
support the scoped recheck instead. No model/source-archive/package acquisition,
installer, import probe, solve, fitting, new render or runtime inventory refresh
ran. Runtime evidence remains stale. No media upload, remote push or publishing.
Code/build/sanitizer/Black checks are inapplicable to the documentation-only diff;
current code and earlier evidence are preserved rather than assigned new passes.
