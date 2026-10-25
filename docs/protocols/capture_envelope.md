# Frozen capture envelope v1

[`capture_envelope.hpp`](../../include/world_explorer/map_store/capture_envelope.hpp)
adds coordinator-only capture evidence in `map_store`. A0/AP0 remain **IN_PROGRESS**.
The record binds complete selected sources/views and their actual native camera
metadata. It does not establish full-video coverage, static conditions, capture
quality, consent, physical calibration or metric scale.

## Independent selection and completeness

`inspect_capture_envelope` requires an independently selected product, source/view
rows, original admitted capture/receipt and expected world/map context. Every
selected capture source appears exactly once, binding its capture ID, original full
source SHA-256 and rights-product ref. Every selected view appears exactly once,
binding its complete view and INFERENCE/HELD_OUT/EVALUATION_ONLY role. Missing,
duplicate, invented, stale or role-swapped rows fail. Source/view order normalizes
by capture and capture/frame identity; it does not define temporal blocks.

Each independent source row also declares a nullable uint64 `source_frame_count`,
condition label, scene motion UNASSESSED/STATIC/DYNAMIC and exposure control
UNASSESSED/LOCKED/AUTOMATIC. Labels use 1–64 ASCII lowercase letters, digits,
underscores or hyphens. A known source count must be at least the number of selected
views from that source. It is not used to allocate frames and is not inferred from
PTS or frame IDs. Null means unknown. Even an exact selected count does not prove
that the source decoder selected every original video frame.

Conditions are source-level declarations. They do not contain measured per-frame
exposure values, demonstrate locked settings, verify static geometry/illumination,
prove orbit/revisit coverage or qualify image quality. Unknown counts/conditions,
dynamic sources and automatic exposure remain explicit private-handle diagnostics;
they do not become inferred static/locked values or an execution approval.

## Native camera binding and separation

Each serialized view contains the exact full view, role, original signed int64 PTS,
positive native time base, capture nanoseconds, decoded world-from-camera wxyz
rotation/translation/similarity scale and full native calibration. Calibration binds
ID, integer width/height, PINHOLE/OPENCV model, four intrinsics (fx/fy/cx/cy), four
Brown-Conrady distortion coefficients and four canonical image-transform values.
All values must match the already decoded selected native frame. Numeric camera
values compare at native double precision; pinned source bytes are not normalized.
No relocalization, camera adjustment, calibration conversion or scale promotion occurs.

The private `src/map_store/capture_metadata.hpp` helper now owns shared full-view,
signed timestamp and world-camera checks for this reader and the existing temporal/
inspection reader. Their camera record fields and acceptance rules are preserved.
Calibration equality is an additional envelope check. Neither reader uses a full
pose snapshot as a new inference input.

The envelope product depends **exactly on the capture manifest**. Source/view/rights
refs are already bound transitively there. The new root passes the capture's
[evaluation separation check](evaluation_inventory.md#schema-dependencies-and-role-separation),
which stops at the capture/source-view boundaries so shared setup metadata does not
become evaluation labels. Known envelope content/recipe/action aliases into selected
inference closures fail. Narrow inference bytes remain unchanged by envelope-only
edits. Undeclared historical influence and future worker adoption remain unverified.

## Schema, ownership and recovery

The record has exactly `schema_version=1`, `world_epoch`, `pose_revision`,
`recipe_sha256`, `capture`, `sources`, `views`, `manual_ancestor`. Source rows have
`capture_id`, `source_sha256`, `rights`, `source_frame_count`, `condition`,
`scene_motion`, `exposure_control`. View rows have `camera`, `calibration`, `role`.
Camera has `view`, `pts`, `time_base`, `capture_time_ns`, `world_from_camera` using
the existing temporal-path format. Calibration has `calibration_id`, `width`,
`height`, `model`, `intrinsics`, `distortion`, `image_transform`.

Bounds before selection copies: 256 KiB, 1–32 sources, 1–64 views, existing capture/
receipt/DAG limits. Strict JSON refuses duplicate keys, raw NUL, depth over 16,
unknown fields/enums, invalid integer/range/finite values and malformed hashes.
Header context/recipe/capture and declared ancestry bind actual original graph
nodes. The private result owns normalized selections and exact record bytes;
copies survive source removal/caller mutation and cannot convert to inference inputs.

`publish_capture_envelope` rechecks original receipt/context and atomically publishes
`envelope-<product_id>-<revision>` through the immutable registry. Source/output
overlap is refused. Staged cancellation exposes no committed record; exact
operation/base/byte replay recovers. Fresh reads require an independently selected
current commit, exact committed record hash and original admitted capture/receipt,
then repeat native admission. Stale pins require resync. No new crash mechanism,
worker, GUI/CLI flow or scene transaction is added.

[Corpus schema 6](evaluation_corpus.md#typed-capture-envelope-in-schema-6) requires
matching private envelope evidence while retaining physical coverage, conditions,
consent, image quality and declaration-specific blockers.
