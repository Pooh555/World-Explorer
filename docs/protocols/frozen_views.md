# Frozen inference view preparation

The worker's explicit `frozen_views` option prepares an immutable **pre-pose**
decode stage from existing RGB8 PNGs. It uses the ordinary worker stage cache,
checksums, cancellation and publication path. The native frame source and batch
pose adapter consume that stage. This is a producer/consumer connection, not an
asset observation API or an exception to [capture](capture_selections.md),
[receipt](asset_receipts.md) or [evaluation](evaluation_protocol.md) admission.
A0/AP0 remain IN_PROGRESS; real asset/corpus/recipe qualification remains open.

## Explicit request and selection

Add this object to a normal worker schema-1 request with `stop_after="decode"`:

```json
"frozen_views": {
  "manifest": "/absolute/local/selection.json",
  "sha256": "<independently selected lowercase SHA-256>",
  "image_directory": "/absolute/local/native-png"
}
```

Timestamp sampling is not performed. Quality selection and direct pose/appearance
execution in this request are refused. `sample_fps` does not choose these frames.
Inputs must be canonical local paths disjoint from the output. No file is
spatially resized, recolored, omitted from the declared selection or downloaded.
Only INFERENCE payloads are copied; excluded views remain in the caller's source
folder. The complete selection is retained as coordinator metadata.

The strict manifest has exactly `schema_version=1`, `source_sha256`,
`source_frame_count`, `width`, `height`, `time_base`, `role_blocks` and `frames`.
Blocks contain `first`, `end` (exclusive) and `role`, partitioning the entire
original source-index range in order. Roles are INFERENCE, HELD_OUT and
EVALUATION_ONLY. Every frame has exactly `source_index`, `pts`, `role`, `file`,
`sha256`, `bytes`; its role must match its containing block. Original indices and
PTS strictly increase. Basename PNG references are portable; names and content
hashes must be unique across the complete selection. There must be at least
three inference views and one excluded view. This binds declared roles, not true
independence, physical conditions or undisclosed earlier tuning.

Limits: 64 KiB manifest, 4–64 selected views, 1–128 blocks, 1–36,000 source frames,
16 MiB/PNG, RGB8, one fixed grid up to 8,192 per dimension/16 Mi pixels, original
positive int32 time base, nonnegative int64 PTS and at most 1,200 seconds. Duplicate
keys, NUL, unknown fields, invalid types/paths/digests and aliased image content
are refused. All selected image hashes/sizes/headers are checked; copying uses
1 MiB chunks, cancellation and a second size/hash check. PNG decoding/CRC errors
can still fail during native replay. Source video and manifest are rechecked
before completion. No input is modified. Frame count, free space and configured
worker stage disk budgets are checked before copying; a declaration is not a
measured process-tree memory bound.

## Native consumption and role pin

The completed decode profile is `frozen_inference_views_v1`; it owns only
`result.json`, the exact `selection.json` and numbered inference PNGs. Native
admission checks the manifest hash against the completion inventory, strict
manifest structure/roles/blocks, exact ordered inference metadata/grid/hash/size
bindings and closed stage file inventory. Existing per-image checks, immutable
RGB leases and seek/reset semantics remain. A corrupt or additional excluded
payload is refused; caller files are preserved.

`DecodedFrameCatalog::m_frozen_selection_sha256` records the selection identity.
`BatchPoseConfig::m_frozen_selection_sha256` must independently match it before
an estimator is created. Missing/wrong pins refuse before output preparation;
a pin on an ordinary legacy stage also refuses. The CLI exposes that existing
application API as:

```sh
we_process solve-poses DECODE_STAGE VIDEO NEW_JOB --pose-python EXECUTABLE --selection-sha256 HASH
```

The batch adapter consumes one RGB lease at a time into its existing bounded
PPM journal. The isolated pose worker receives only those inference frames.
Ordinary reconstruction-request/GUI support for selecting this new preparation
option remains unimplemented. Native replay needs no solver/model environment:
`we_process replay-frames DECODE_STAGE VIDEO`.

A source hash binds caller-selected image declarations to the named video; this
producer does not independently re-decode it to prove pixel/PTS correspondence.
The [real validation](../validation/2026-10-29-frozen-view-inputs.md) instead uses
the independently checked source/PNG correspondence from the preceding intake.
The current 4K set still exceeds owned asset-capture/receipt and full evaluation
mask budgets. Preparation and pose-input verification do not clear those gates,
invent metric scale, enable walking or qualify A1. Filesystem checks assume
cooperating local writers, without hostile-mutation or power-loss guarantees.
