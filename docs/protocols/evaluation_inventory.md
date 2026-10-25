# Evaluation inventory and visibility v1

[`evaluation_inventory.hpp`](../../include/world_explorer/map_store/evaluation_inventory.hpp)
adds coordinator-only frozen declarations in `map_store`. A0/AP0 remain
**IN_PROGRESS**. It binds selected object/structure identities to admitted
evaluation annotations; labels, inventory completeness and physical correspondence
remain unverified. No inference input, segmentation result or AP pass is produced.

## Independent selection and complete tables

`inspect_evaluation_inventory` takes independently selected inventory/visibility
product refs, required item rows and visibility cells, original owned capture/
receipt, and expected world/map context. Items contain a positive `ObjectTrackId`,
kind PROP/FLOOR/WALL/CEILING/OPENING, label and required flag. There are 1–64 unique
tracks and at least one required item. Labels are 1–64 ASCII lowercase letters,
digits, underscores or hyphens. Repeated class labels are allowed on distinct
tracks. These limits do not establish the A1 pilot's required object/shell scope.

The visibility table covers **every item × every selected noninference view**,
including optional items and UNKNOWN cells. Exact view rows bind capture/frame,
calibration, image, pose asset and frame pose revision to the admitted capture.
Rows are unique by track/capture/frame, use no inference views and cannot omit or
invent a view. Independent rows and serialized rows must match; order normalizes
by identity. Up to 64 evaluation views and 4,096 cells are supported, within the
existing capture/DAG/byte limits.

| State | Required annotation binding |
|---|---|
| VISIBLE | Selected evaluation annotation, exactly one ACCEPTED association matching the item track, matching class label, positive VISIBLE mask count |
| FULLY_OCCLUDED | Same identity/label checks, zero VISIBLE and positive OCCLUDED count |
| OUT_OF_VIEW | No annotation; explicit declaration |
| UNKNOWN | No annotation; explicit unknown |

Every selected evaluation annotation is used exactly once. Reuse, stale refs,
wrong views, ambiguous accepted identities, mismatched labels or ignored annotations
fail. An unresolved/ambiguous annotation cannot silently become ground truth under
this profile. Class/association/mask values are admitted declarations, not measured
truth; no IoU, precision/recall or completeness metric is inferred from agreement.

## Schema, dependencies and role separation

The inventory has exactly `schema_version=1`, `world_epoch`, `pose_revision`,
`recipe_sha256`, `capture`, `items`, `manual_ancestor`. Item rows contain `track_id`,
`kind`, `label`, `required`. The visibility record adds `inventory`, `views`, `cells`
instead of `items`; cell rows contain `track_id`, `capture_id`, `frame_id`, `state`,
`annotation` (exact ref or null). Views use the existing full observation/capture
view format. Product refs bind exact IDs/revisions/content hashes.

The inventory depends only on the selected capture manifest. Visibility depends
exactly on inventory plus every selected evaluation image/pose/annotation product,
deduplicated by ID. Both records bind the actual graph recipe, capture product,
context and manual ancestry. Declared evaluation ancestry stays coordinator data;
it does not classify a job's assistance or authenticate an annotator's act.

The added `AdmittedCaptureSelection::validate_evaluation_separation` checks up to
16 explicit evaluation roots from the exact original receipt/context. It collects
their content/recipe/action hashes and upstream declarations, stopping at selected
source-view products and the capture-manifest boundary. Explicit roots are never
skipped as boundaries. These tokens join the existing forbidden evaluation set,
then all selected inference image/pose closures are checked. This catches aliases
without making shared capture setup metadata into evaluation labels. Shared opaque
label recipes remain conservatively refused. The original handle is not mutated.

This is a check of declared selected inference closures and supplied evaluation
roots. Legacy opaque callers do not discover new record types automatically;
undeclared historical influence, dishonest provenance and future worker adoption
remain outside its proof. The typed reader always supplies both roots. The owned
inventory has no conversion to `InferenceCaptureInputs`; narrow inference payloads
remain byte-identical when only these evaluation declarations change.

Inventory bytes are at most 64 KiB and visibility bytes 256 KiB. Exact keys/types,
integer IDs, supported enums, lowercase hashes, no raw NUL/duplicate keys and depth
at most 16 are enforced before adoption. Labels/selected counts are bounded before
owning selected tables. Existing graph bounds, including 64 parents/node, still
apply; not every independent maximum necessarily fits all byte/DAG limits at once.
Pinned whitespace is retained rather than normalized.

## Ownership, publication and corpus integration

The private handle owns normalized selections and exact record bytes. It retains
capture product/receipt/context and combined declared ancestry, without caller
aliases or worker-facing evaluation labels. Copies survive source deletion.

`publish_evaluation_inventory` rechecks original receipt/context and atomically
publishes `inventory-<product_id>-<revision>` and
`visibility-<product_id>-<revision>` through the existing immutable registry, with
source/output overlap protection. Cancellation after either staged blob exposes
no committed records; exact retry recovers. `read_evaluation_inventory` requires
an independently selected current commit and both committed hashes, then repeats
native capture/receipt/selection admission. Old pins require resync. No source
repair, map/scene mutation, worker, download or GUI/CLI flow is added.

[Corpus schema 3](evaluation_corpus.md) explicitly requires typed inventory for
present paired inventory/visibility refs and preserves earlier schemas. Structural
admission retains `INVENTORY_LABELS_UNVERIFIED`, `INVENTORY_COMPLETENESS_UNVERIFIED`
and `VISIBILITY_CORRESPONDENCE_UNVERIFIED`. Real pilot evaluation and A1–A5 product
qualification still require independent inputs and actual output evidence.
