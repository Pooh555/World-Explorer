# Object observations v1

[`object_observation.hpp`](../../include/world_explorer/map_store/object_observation.hpp)
implements bounded read-only admission of object-observation and visibility-mask
bytes from an [owned receipt](asset_receipts.md). `map_store` owns this contract
and gains one explicit public dependency on `asset_catalog` for project-owned
receipt/reference types. The boundary gate permits that edge; no engine/framework
headers or algorithm implementation enter the interface. A0/AP0 remain
**IN_PROGRESS**. These are declarations, not segmentation/tracking results or a
completed capture/evaluation freeze.

## Independent selection and actual pose bytes

`inspect_object_observation` requires an original admitted receipt, independently
selected observation product/semantic ID/revision, capture/frame/calibration,
per-frame pose revision, image/pose-asset/mask product refs, image grid, evidence
refs and current epoch/map revision. Semantic `ObjectObservationId` and
`ObjectTrackId` are distinct core types; neither is an asset ID, class label or
array index. No selection is learned from the document being inspected.

Every selected reference must exactly match its graph node. Observation, image,
pose and mask roles use different product IDs. Evidence IDs are unique and cannot
alias those roles. The observation node's direct parents are exactly image,
pose, mask and independently selected evidence; the mask depends exactly on image
and pose. Graph context and declared transitive manual ancestry are revalidated.
Hash binding does not authenticate the producer or a human action.

The selected pose-asset bytes pass the existing [native decoder](maps.md),
including its calibration, finite transform, convention and map consistency
checks. The decoded snapshot must match the expected epoch/map revision. The
selected frame must exist, remain noninvalidated and have TRACKING state, exact
capture/calibration/image hash and exact selected per-frame pose revision.
An unchanged frame pose may precede the snapshot revision. Calibration IDs remain
immutable within the selected pose snapshot; the pose-asset revision/hash binds
both calibration and pose bytes. The handle retains actual selected calibration,
pose and composed world-from-camera values without changing their units or
coordinates. Arbitrary scale remains arbitrary.

Images are hash-bound encoded bytes already owned by the receipt; this inspector
does not decode pixels, measure calibration accuracy or certify source/pose
correspondence. The caller must select current coherent snapshots. No MapStore
transaction occurs, and this is not a synchronization token with a mutable map.

## Strict observation and mask records

Both JSON documents reject raw NUL, duplicate keys, depth above 16, foreign/missing
fields, wrong types, negative/floating integers and nonfinite numbers. The source
product hash identifies exact bytes; arbitrary JSON whitespace/order is permitted.
No additional canonical encoder, persistent semantic revision registry or writer
is introduced here.

| Record/field | Implemented meaning |
|---|---|
| Observation root | `schema_version=1`, selected `observation_id`/`revision`, `world_epoch`/`pose_revision`, exact node `recipe_sha256`, `view`, `mask`, `grid`, `classes`, `associations`, `evidence`, `manual_ancestor` |
| `view` in both records | Exact `capture_id`, `frame_id`, `calibration_id`, `frame_pose_revision`, `image` and `pose_asset` refs |
| `grid` in both records | Positive width/height and four finite `image_to_mask` coefficients `[sx,sy,tx,ty]`; positive scales |
| Class hypothesis | Unique 1–64 byte ASCII token (`A–Z`, `a–z`, digits, `_`, `.`, `-`), optional confidence |
| Track association | Unique positive track ID, HYPOTHESIS/ACCEPTED/REJECTED and optional confidence; at most one accepted track |
| Evidence decision | Exact independently selected product ref, boolean `accepted`, 1–64 byte uppercase/digit/underscore reason token; every selected ref appears once |
| Mask root | `schema_version=1`, expected epoch/map revision, exact `view`/`grid`, `runs`, and graph-bound `manual_ancestor` |
| Mask run | Nonnegative `first`, positive `count`, VISIBLE/OCCLUDED/UNKNOWN/BACKGROUND; contiguous complete row-major partition, no overlap or gaps |

Mask coordinates refer to the **calibrated image** with integer pixel centers:
`mask_pixel = scale * image_pixel + offset`. Its inverse pixel support must be
finite and stay within calibrated support `[-0.5, extent-0.5]` on both axes.
For example half-size full-image resizing uses scale `0.5`, offset `-0.25`.
The calibration's own original-image crop/resize provenance remains separate;
applying it again would be incorrect. Cropped-out pixels have no declarations.

UNKNOWN is not background; OCCLUDED is not directly visible support. All-unknown
or fully occluded records are allowed and do not infer a full object extent.
Coverage counts are pixel counts, never geometry area. Masks, classes, associations
and accept/reject decisions are unverified declarations; confidence is null or a
finite value in `[0,1]` and cannot establish evidence or verified identity.
Accepting a track does not merge asset identities. Rejected evidence remains a
dependency and retains its declared manual ancestry. Manual flags must equal
node ancestry; trusted action journaling remains separate work.

## Bounds, lifetime and scope

Limits precede typed vector growth: observation 256 KiB; mask 4 MiB, 16,384 runs,
16,777,216 pixels and dimensions at most 32,768; 16 class hypotheses, 16 track
associations, 48 evidence decisions. Pose assets use a separate 16 MiB ceiling,
4,096 frames and 64 calibrations/submaps. Existing receipt aggregate bounds apply.
JSON DOM allocation is also bounded by input bytes and depth; masks stay compressed.

Success creates a privately constructed immutable shared handle. It owns selected
metadata, compressed runs, four coverage counts, calibration/pose/world transform
and original receipt SHA-256. Caller edits, source deletion and later map changes
cannot mutate it; callers must re-admit against the current context before use.
Failure/cancellation returns no partial handle and consumes no input. There are
no writes, worker launches, downloads, inference or runtime scene updates.
[Validation](../validation/2026-10-24-object-observations.md) separates synthetic
checks and real retained pose/image binding with explicitly synthetic masks from
real object-observation quality or AP gate qualification.
