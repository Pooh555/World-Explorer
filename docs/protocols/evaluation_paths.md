# Frozen temporal blocks and inspection paths v1

[`evaluation_paths.hpp`](../../include/world_explorer/map_store/evaluation_paths.hpp)
adds coordinator-only evidence in `map_store`. A0/AP0 remain **IN_PROGRESS**.
The contract binds declarations to complete admitted capture selections and exact
native camera bytes. It does not qualify a real corpus, physical pose accuracy,
novelty, historical fitting isolation or a freeze before tuning.

## Independent selection and temporal partition

`inspect_evaluation_paths` requires independent temporal/inspection product refs,
ordered `EvaluationTemporalBlock` rows and ordered `EvaluationInspectionEntry`
rows, plus the original admitted capture/receipt and expected world/map context.
The private result owns the selection and exact pinned record bytes. It retains
capture product/receipt/context/manual ancestry; it cannot convert to inference
inputs. Caller edits/source removal do not change an admitted copy.

Each block has a unique positive ID, capture ID, INFERENCE/HELD_OUT/EVALUATION_ONLY
role and nonempty ordered frame IDs. All selected frames, including evaluation-only
frames, appear exactly once. Blocks preserve caller order; frames within a source
strictly increase in original PTS and monotonic capture nanoseconds. A source must
retain one exact time base; v1 refuses changes rather than converting or rounding
rational timestamps. Signed PTS, including negative values, retain their exact
int64 representation. Adjacent blocks of the same role for the same capture must
be merged, even if another source's block appears between them. Thus each block
is contiguous at the **selected-frame resolution**. Unselected source frames and
physical duration/novelty are not inferred from a sparse selection.

At least one capture must contain both INFERENCE and HELD_OUT blocks, supplying a
training-source temporal holdout. Other inference captures need not each have one.
This structural requirement alone does not certify the temporal sampling policy,
prove that a split was nonrandom, or establish sufficient evaluation coverage.

## Independent inspection and pose declarations

The inspection path must contain exactly all selected HELD_OUT frames whose full
source hash is absent from every inference source. It is nonempty, uses a single
capture ID and has strictly increasing capture times. Same-source aliases under
another capture ID cannot supply an independent path. Training temporal holdouts
and EVALUATION_ONLY frames do not enter it. Repeated, omitted, foreign or reordered
entries fail. Additional independent sources with HELD_OUT frames require separate
capture selections under this single-path profile.

Each path entry independently selects UNASSESSED, UNCERTAIN or DECLARED_RELIABLE
confidence. This is a declaration, not covariance, localization evidence or measured
accuracy. Any value except DECLARED_RELIABLE is retained in the handle's uncertainty
indicator. Native pose-asset references continue to bind any actual covariance;
no covariance or physical confidence is manufactured from the declaration.

Every temporal/path camera row binds the exact admitted full view (capture/frame,
calibration, pose revision, image and pose product refs), original PTS/time base,
capture nanoseconds and decoded world-from-camera quaternion wxyz, translation and
similarity scale. Values must equal the admitted native values; no tolerance,
relocalization, pose correction, exposure fitting or metric-scale promotion occurs.
The existing capture reader still refuses evaluation/unselected observations in
inference pose snapshots. Both new record roots additionally pass the capture's
[evaluation separation check](evaluation_inventory.md#schema-dependencies-and-role-separation),
which catches known content/recipe/action aliases into selected inference closures.
Worker adoption and undeclared historical influence remain outside this proof.

## Schemas, bounds and owned recovery

Both records have exactly `schema_version=1`, `world_epoch`, `pose_revision`,
`recipe_sha256`, `capture`, `manual_ancestor` and one body: `blocks` for temporal,
`path` for inspection. Block rows have `block_id`, `capture_id`, `source_sha256`,
`role`, `frames` (camera rows). The path body has `temporal` (exact ref), `entries`;
entry rows have `camera`, `confidence`. Camera rows have `view` (existing full view
format), `pts`, `time_base` (two integers), `capture_time_ns`, `world_from_camera`
(`rotation`, `translation`, `scale`). Unknown fields, enums, duplicate keys, raw NUL,
depth above 16, malformed hashes, noninteger/overflowing timestamps and nonfinite
numbers fail. Exact whitespace remains pinned.

Bounds precede selection copies: 256 KiB per record, 1–64 blocks, 1–64 frames per
block with at most 64 frames in aggregate, and 1–64 path entries. Existing graph,
source/frame/receipt limits still apply. Temporal depends exactly on the capture
manifest; inspection depends exactly on temporal plus deduplicated path image/pose
products. Graph recipe/manual ancestry and independent refs must agree.

`publish_evaluation_paths` rechecks the original receipt/context and atomically
publishes `temporal-<product_id>-<revision>` and `inspection-<product_id>-<revision>`
through the existing immutable registry. It protects the original receipt tree
from overlap. Cancellation after either staged blob exposes no committed records;
matching operation/base/bytes replay recovers. Fresh `read_evaluation_paths` requires
an independently selected current commit and both committed hashes, then repeats
admission against the original owned capture/receipt/selected tables. Stale pins
require resync. No new crash, worker, map/scene transaction or UI flow is implied.

[Corpus schema 4](evaluation_corpus.md#typed-temporal-and-inspection-evidence-in-schema-4)
requires the private handle for present paired temporal/inspection refs and keeps
physical sampling, independence, poses and pre-tuning provenance unverified.
