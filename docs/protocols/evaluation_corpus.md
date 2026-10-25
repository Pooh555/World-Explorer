# Sealed asset evaluation corpus v1/v2/v3/v4/v5/v6

[`evaluation_corpus.hpp`](../../include/world_explorer/application/evaluation_corpus.hpp)
adds coordinator metadata in `application`, above capture selections and recipe
inspection. A0/AP0 remain **IN_PROGRESS**. A seal freezes identities and missing
prerequisites; it cannot qualify a real corpus, authorize execution, certify
consent/scale or contribute an automatic success. All current fixtures are synthetic.

## Independent pins and ownership

`admit_asset_corpus` takes a definition and separately selected owned capture and
recipe inputs. Every capture binds its label, exact manifest product,
`CaptureSelectionId`/revision, original receipt hash and current immutable-registry
generation/manifest hash. The registry must actually contain the admitted selection
bytes under its semantic key. No pin or source path is discovered from the corpus.
Receipt pins transitively bind exact source hashes, rights refs, decoded camera
calibration/poses, roles and annotations under the existing
[capture selection](capture_selections.md) contract. No context can be substituted
by changing an unbound label. Different captures may have different world contexts.

Candidate recipe pins bind exact product/revision/content, original receipt,
recipe hash, epoch/map revision and caller-observed host capacities. Existing
[recipe inspection](asset_recipes.md) checks actual owned recipe bytes, component
code/environment/model/input/terms/review hashes, configuration/prompt/seed and
resource/candidate budgets. Recipe source inputs are not automatically associated
with corpus frames; that future worker boundary remains explicitly unverified.
The host observation is frozen metadata, not a measured resource qualification.

The privately constructed result owns canonical metadata, recipe inspections,
blockers and shared input snapshots. Copies survive removal of caller aliases and
source/output directories. It exposes no new worker input or execution method;
existing `InferenceCaptureInputs` remains the separate role firewall. A manifest
does not prove an undeclared historical influence absent from that graph.

## Evaluation prerequisites and blockers

Each capture has ten optional evidence refs, all bound to exact original receipt
nodes: capture envelope, object/structure inventory, visibility, independent scale,
training temporal blocks, held-out inspection path, backsides, empty background,
changed light and evaluation protocol. The protocol ref is intended to freeze
sampling/masks/renderer/tone mapping/lights/reviewer rubric. Schema 1 keeps these bodies opaque: presence yields `<ROLE>_UNVERIFIED`, absence `<ROLE>_MISSING`.
Independent scale must be present exactly when the admitted capture selects a
measurement, with the identical ref. Neither case creates measured scale.

Missing HELD_OUT frames adds `HELD_OUT_PATH_MISSING`. Every capture retains
`SOURCE_RIGHTS_UNVERIFIED` and `REAL_CAPTURE_AUTHENTICITY_UNVERIFIED`. SYNTHETIC,
PILOT and RELEASE are declared tracks, not certification; SYNTHETIC additionally
reports `SYNTHETIC_INPUTS`. Merely changing that label cannot clear authenticity,
rights or evidence blockers. Reusing a full source hash under another capture
label is refused, preventing duplicate submissions inside this manifest.

Recipe blockers from native inspection remain. This path additionally retains
`CAPTURE_INPUT_BINDING_UNVERIFIED`, `INSTALLED_FILES_UNVERIFIED`,
`REVIEW_NOT_SELECTED`, `RUNTIME_CLOSURE_UNVERIFIED` and
`WORKER_LIMITS_UNVERIFIED`. Component/review and installed-tree inspectors are
separate contracts; no corpus field can self-approve their work. Scope strings
identify capture labels, `recipe-<label>` or `corpus`; consumers must also retain
the definition when reporting them. No ready/success boolean is exposed.

## Encoding, bounds and seal publication

The canonical JSON has exactly `schema_version=1`, `corpus`, `revision`, `track`,
`captures`, `recipes`. Capture rows contain `label`, `product`, `selection_id`,
`revision`, `receipt_sha256`, `commit` and `evidence`. The commit has `generation`
and `manifest_sha256`; every evidence role appears with a ref or null. Recipe rows
contain `label`, `product`, `receipt_sha256`, `recipe_sha256`, `world_epoch`,
`pose_revision`, `host`. Host fields are `contracts_only`, `host_ram_bytes`,
`device_vram_bytes`, `free_disk_bytes`, `cpu_threads`, `queued_jobs`. Refs use the
existing exact product ID/revision/content hash format. Labels/identity are
1–64 ASCII lowercase letters, digits, underscores or hyphens; revision is positive.
Capture and recipe labels are each unique; row order normalizes by label.

Limits: 256 KiB canonical bytes, 1–16 captures, 256 selected frames in aggregate,
1–16 recipes and ten evidence refs/capture. Tables and strings are bounded before
copying the definition. Existing per-handle receipt/capture/recipe limits apply.
Strict decode rejects duplicate keys, raw NUL, nesting over 16, unknown/missing
fields, unsupported schemas/enums, floating/negative integers, malformed hashes
and noncanonical bytes. Decode requires an independently selected corpus ID,
revision and SHA-256, then repeats input/registry admission. Blockers are derived,
never accepted from serialized claims.

`publish_asset_corpus` writes the single `sealed-corpus` record through the
[immutable registry](asset_metadata_io.md), always at base generation zero.
Exact operation replay preserves bytes; a changed seal or another operation
cannot append to it. Revised freezes need a separately selected output directory;
this API does not enforce experiment policy across directories. Source receipt
trees and capture registries are protected from overlap. Capture commits are
rechecked immediately before publication; this cooperative filesystem check does
not lock unrelated registries against hostile concurrent mutation.

`read_asset_corpus` requires independently selected generation-one commit and
corpus hash, exactly one committed record, and current selected original inputs.
Pending blobs expose no corpus. Cancellation preserves the intent/staged bytes;
exact replay completes manifest-last publication. An already admitted handle
survives source removal, but a fresh read requires the selected capture registry.
Later unrelated generic registry writes invalidate typed reads and publication
replay. No tuning, evaluation, solver, download, model install, map/scene mutation,
corpus-level run accounting or GUI/CLI workflow is implemented here.

## Explicit typed scale in schema 2

Schema 2 adds the required boolean `typed_scale=true` to the exact root field set.
`AssetCorpusDefinition::m_require_typed_scale` selects it. False/nonboolean/missing
values fail; schema 1 still rejects this extra field and retains byte-identical
encoding. The existing capture/recipe rows, independent commit/hash pins, bounds
and single-seal publication are unchanged. A different schema changes the corpus
hash and needs a separately selected seal directory.

For each present independent-scale ref, the separately selected capture input must
supply an [admitted typed measurement](capture_measurements.md) bound to that exact
ref/capture product/receipt. Missing or mismatched handles fail admission and fresh
reads. Missing scale evidence remains `INDEPENDENT_SCALE_MISSING`; supplying a
handle without a selected ref fails. Schema 1 rejects typed handles, preventing
ambiguous diagnostics under unchanged serialized bytes. A typed handle does not
require a separate measurement registry when the already committed capture pins
its original receipt bytes; that registry is an additional owned metadata path.

Schema 2 removes `INDEPENDENT_SCALE_UNVERIFIED` only after structural admission and
retains `MEASUREMENT_AUTHENTICITY_UNVERIFIED`, `GEOMETRY_CORRESPONDENCE_UNVERIFIED`
and optional `SYNTHETIC_SCALE_DIAGNOSTIC`. Other opaque evidence/rights/runtime
blockers remain. This strengthens declared structure without granting real scale,
worker execution, scene alignment or AP qualification.

## Typed inventory and visibility in schema 3

`AssetCorpusDefinition::m_require_typed_inventory` selects schema 3, whose exact root
fields additionally contain `typed_inventory=true` and explicit boolean
`typed_scale`. False/nonboolean/missing inventory flags and nonboolean scale flags
fail. Scale true retains schema-2 requirements; false retains schema-1 opaque scale
checks. Schemas 1/2 keep their exact encodings and reject inventory handles.

Inventory and visibility refs must be both present or both missing. Present refs
require one independently selected [private typed inventory handle](evaluation_inventory.md)
matching both refs, the exact capture product and original receipt. Missing or
mismatched handles fail fresh admission/read. Missing pairs retain explicit missing
prerequisites; supplying a handle without selected refs fails. Structural admission
replaces the two opaque blockers with `INVENTORY_LABELS_UNVERIFIED`,
`INVENTORY_COMPLETENESS_UNVERIFIED`, `VISIBILITY_CORRESPONDENCE_UNVERIFIED`.
Other capture/scale/rights/runtime blockers stay open. A new schema changes the
corpus hash and requires a separately selected seal; it grants no execution or
physical-truth authority.

## Typed temporal and inspection evidence in schema 4

`AssetCorpusDefinition::m_require_typed_paths` selects schema 4. Exact root fields
add `typed_paths=true` and explicit boolean `typed_scale`/`typed_inventory` flags.
Those flags select the existing scale/inventory requirements independently. Missing,
false or nonboolean path flags and nonboolean earlier flags fail. Schemas 1–3
retain their exact bytes and reject supplied path handles.

Temporal-block and inspection-path refs must be both present or both absent.
Present refs require the independently supplied [private path handle](evaluation_paths.md)
to match both refs, original capture product and original receipt. A mismatched or
missing handle fails admission/fresh read; a handle without refs fails. Missing
pairs retain explicit missing prerequisites. Typed admission replaces the opaque
`TEMPORAL_BLOCKS_UNVERIFIED`/`INSPECTION_PATH_UNVERIFIED` blockers with
`TEMPORAL_SAMPLING_UNVERIFIED`, `INSPECTION_INDEPENDENCE_UNVERIFIED`,
`EVALUATION_POSES_UNVERIFIED` and `PRETUNING_FREEZE_UNVERIFIED`. Any UNASSESSED or
UNCERTAIN path entry adds `EVALUATION_POSE_UNCERTAINTY_DECLARED`. Even all
DECLARED_RELIABLE entries retain the physical pose blocker. Independent-source
hashes cannot establish physical novelty or a freeze before tuning. No execution
or AP success authority is granted; revised seals need separately selected outputs.

## Typed controlled references in schema 5

`AssetCorpusDefinition::m_require_typed_references` selects schema 5. Exact root
fields add `typed_references=true` and explicit boolean `typed_scale`,
`typed_inventory`, `typed_paths`. Earlier flags select their existing requirements
independently. Missing/false/nonboolean reference flags and nonboolean earlier
flags fail. Schemas 1–4 retain their exact encodings and reject supplied reference
handles. Revised freezes require separately selected seal outputs.

Each present BACKSIDE/EMPTY_BACKGROUND/CHANGED_LIGHT ref requires exactly one
[admitted controlled-reference handle](controlled_references.md) of that kind.
Up to three caller-selected handles are allowed; duplicate, missing, foreign or
unselected handles fail. The handle must bind the exact evidence product, original
capture product/receipt and both inventory/visibility refs explicitly selected in
the corpus. This requirement holds even when `typed_inventory` is false; its
opaque inventory diagnostics remain in that mode. Kinds may be absent independently,
retaining their `<ROLE>_MISSING` blocker without a fabricated reference set.

For each present typed kind, the opaque `<ROLE>_UNVERIFIED` blocker is replaced by
`REFERENCE_CORRESPONDENCE_UNVERIFIED`, `PHYSICAL_CONDITIONS_UNVERIFIED` and
`LIGHTING_CALIBRATION_UNVERIFIED`, under scope `<capture_label>/<ROLE>`. If any
pair lacks a baseline/reference lighting product, that scope also retains
`LIGHTING_MEASUREMENT_MISSING`. Partial quantities inside a supplied observation
remain explicit in the handle; presence does not prove complete calibration.
Rights/authenticity/other evidence and recipe/runtime blockers remain unchanged.
No real completion/relighting score, required-prop coverage, worker input or AP
success authority is produced by this structural admission.

## Typed capture envelope in schema 6

`AssetCorpusDefinition::m_require_typed_envelope` selects schema 6. Exact root
fields add `typed_envelope=true` and explicit boolean `typed_scale`,
`typed_inventory`, `typed_paths`, `typed_references`. Earlier flags select their
existing checks independently. Missing/false/nonboolean envelope flags and
nonboolean earlier flags fail. Schemas 1–5 retain their exact encodings and reject
supplied envelope handles. A changed freeze needs a separately selected seal.

A present CAPTURE_ENVELOPE ref requires one independently supplied
[private admitted envelope](capture_envelope.md) matching that exact product,
original capture product and receipt. Missing/foreign handles fail admission and
fresh reads; a handle without a selected ref fails. Absent evidence retains
`CAPTURE_ENVELOPE_MISSING`. Typed admission replaces `CAPTURE_ENVELOPE_UNVERIFIED`
with `SOURCE_COVERAGE_UNVERIFIED`, `CAPTURE_CONDITIONS_UNVERIFIED`,
`CAPTURE_CONSENT_UNVERIFIED`, `CAPTURE_QUALITY_UNVERIFIED`. Native metadata agreement
does not prove physical capture properties or consent.

Derived declarations additionally retain `SOURCE_FRAME_COUNT_UNKNOWN`,
`CAPTURE_CONDITIONS_UNASSESSED`, `DYNAMIC_CAPTURE_DECLARED` and
`AUTOMATIC_CAPTURE_EXPOSURE_DECLARED` when applicable. Unknown declarations are
allowed in a sealed partial corpus and never become a preflight/quality pass.
Existing rights/authenticity/evidence and recipe/runtime blockers remain open;
worker execution and real product/automatic-yield qualification are unchanged.
