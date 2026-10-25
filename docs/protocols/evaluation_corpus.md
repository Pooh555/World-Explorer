# Sealed asset evaluation corpus v1/v2

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
