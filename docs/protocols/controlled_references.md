# Controlled prop references and lighting observations v1

[`controlled_references.hpp`](../../include/world_explorer/map_store/controlled_references.hpp)
adds coordinator-only evidence in `map_store`. A0/AP0 remain **IN_PROGRESS**.
It freezes selected declarations for BACKSIDE, EMPTY_BACKGROUND or CHANGED_LIGHT
without certifying physical correspondence, actual removal, backside coverage,
calibrated lighting or relighting. Each kind is independently admissible; partial
capture availability does not require inventing the other kinds.

## Independent selections and role checks

`inspect_controlled_reference` requires a product/kind and independently selected
pair rows, original admitted inventory/capture/receipt, and expected world/map
context. Inventory and capture must come from that exact receipt and capture
product. Each pair has a positive unique ID, frozen PROP track, full baseline and
reference views, two condition labels, reference subject state and optional baseline/
reference lighting-product refs. Condition labels use 1–64 ASCII lowercase letters,
digits, underscores or hyphens. Pair order normalizes by ID; repeated subject and
baseline/reference view combinations are refused even under another ID.

The baseline must be an exact selected INFERENCE view. The reference must be an
exact selected noninference view with distinct image bytes. Native capture admission
already binds actual calibrated camera poses/source hashes; this path neither
relocalizes nor changes those values. Same-source temporal references are allowed;
a different source hash is not required and would not prove physical independence.

For BACKSIDE/CHANGED_LIGHT the reference state is PRESENT and the inventory's exact
subject/view cell must be VISIBLE. For EMPTY_BACKGROUND it is REMOVED and the cell
must be OUT_OF_VIEW. Baseline subject presence, physical removal/visibility and a
backside label remain declarations; agreement does not prove scene correspondence.
The profile supports props, not structural-shell reference tests. CHANGED_LIGHT
also requires different condition labels. Labels do not prove novel illumination,
three unseen lights, near-light coverage, fixed camera/materials or no refitting.

The native record has exactly `schema_version=1`, `world_epoch`, `pose_revision`,
`recipe_sha256`, `capture`, `inventory`, `visibility`, `kind`, `pairs`,
`manual_ancestor`. Pair rows have exactly `pair_id`, `subject_track_id`, `baseline`,
`reference`, `baseline_condition`, `reference_condition`, `reference_state`,
`baseline_lighting`, `reference_lighting`. Views use the existing full view format;
lighting fields are exact refs or null. All refs bind selected original graph nodes.

## Lighting quantities and dependencies

Optional lighting products are actually decoded. Each has exactly
`schema_version=1`, `world_epoch`, `pose_revision`, `recipe_sha256`, `view`,
`illuminance`, `color_temperature`, `manual_ancestor`. Quantities are null or
`{"value": number, "unit": string}`. Illuminance uses exactly `lux` and finite
nonnegative values; color temperature uses exactly `kelvin` and finite positive
values. At least one quantity must be present. Missing quantities stay absent;
no inferred conversion/default/covariance is created. Context, recipe, declared
ancestry and exact selected view must agree. Dependencies are exactly that view's
image and pose product. One measurement product cannot represent another view.

These are typed observations of declared quantities. They contain no calibrated
photometer identity, location/time synchronization, light direction/spectrum or
HDR environment and cannot establish those properties. A missing lighting product
is allowed and retained by `has_missing_lighting`; partial quantities in a supplied
record remain distinguishable from an absent record. Measurement authenticity and
calibration remain open even when all fields are present.

The reference product depends exactly on capture, inventory, visibility and all
deduplicated selected baseline/reference image/pose/lighting refs. Its full declared
closure passes the capture's [evaluation separation check](evaluation_inventory.md#schema-dependencies-and-role-separation),
catching known content/recipe/action aliases into selected inference closures.
The original narrow inference handle is unchanged. Undeclared influence, legacy
opaque callers and future worker integration remain outside that check's proof.

## Bounds, ownership and recovery

Bounds before selection copies: 1–32 pairs, 256 KiB reference record, at most 64
unique lighting records of 64 KiB each and 1 MiB aggregate copied lighting bytes.
Existing capture/receipt/DAG bounds still apply. Strict JSON rejects duplicate
keys, raw NUL, depth above 16, unknown fields/enums, invalid integers, nonfinite
numbers, malformed hashes, wrong units and stale refs/context. Pinned whitespace
is retained. The private result owns normalized selections, exact reference bytes
and parsed lighting values; copies survive caller edits/source removal. It has no
conversion to inference input and grants no execution or AP success authority.

`publish_controlled_reference` rechecks original receipt/context and atomically
publishes `controlled-<product_id>-<revision>` through the existing immutable
registry with source/output overlap protection. Staged cancellation exposes no
committed record; matching operation/base/bytes replay recovers. Fresh reads require
an independently selected current commit, exact committed reference hash and the
original admitted inventory/capture/receipt, then repeat native admission. Lighting
bytes remain in the original owned receipt; publication does not copy them into a
new lighting registry. No new crash protocol, worker, GUI/CLI or scene update is added.

[Corpus schema 5](evaluation_corpus.md#typed-controlled-references-in-schema-5)
requires one matching private handle per present kind, preserving physical,
correspondence, calibration and missing-measurement qualification gaps.
