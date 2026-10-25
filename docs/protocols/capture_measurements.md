# Typed capture measurement evidence v1

[`capture_measurement.hpp`](../../include/world_explorer/application/capture_measurement.hpp)
adds bounded receipt-backed metadata in `application`. It reuses the existing
[world-frame reader](world_frame.md) for native mesh anchors, distance/uncertainty,
up/origin and derived transform checks. A0/AP0 remain **IN_PROGRESS**. Admission
checks declarations against selected bytes; it does not authenticate a physical
measurement, observed correspondence, sensor provenance or legal rights.

## Independent selection and byte checks

`inspect_capture_measurement` requires an independently selected measurement ref,
inference `ObjectViewSelection`, source scene ID/epoch/revision/content hash and
alignment-document ref, plus original owned capture/receipt, caller-selected native
scene and expected world/map context. The measurement ref must equal the capture's
selected measurement; view/calibration/pose/image must equal an admitted inference
view. Its full source hash must equal that capture source. Evaluation views cannot
be used here. No capture-manifest hash is stored in its own upstream measurement
record; the private handle binds the admitted capture product after inspection.

The strict record has exactly `schema_version=1`, `world_epoch`, `pose_revision`,
`recipe_sha256`, `source_sha256`, `view`, `scene`, `alignment`, `manual_ancestor`.
`view` uses the capture-selection format. `scene` contains `scene_id`, `world_epoch`,
`revision`, `sha256`; `alignment` uses exact product ID/revision/content hash.
Measurement dependencies must equal image, pose asset and alignment ref; the
alignment node has no product parents. Recipe/context and declared manual ancestry
must equal the native graph. The existing capture dependency firewall additionally
refuses known evaluation content, recipe/action aliases and indirect ancestry.

The record is at most 16 KiB, with exact keys, strict integer/digest/boolean types,
no duplicate keys/raw NUL, and nesting at most 16. The separately pinned alignment
blob retains its original bytes, is at most 8,192 bytes, and passes both strict
metadata parsing and `WORLD_FRAME_ALIGNMENT_V1` admission. The latter retains its
eight-level limit, decoded-NUL/text bounds, valid barycentric anchors, positive
distance, `0 <= uncertainty < distance`, independent/synthetic methods, up reference
and finite derived transform. This path changes none of those numerical rules.

## Native source and ownership

The source is an unaligned ARBITRARY scene at the expected world epoch. It is
validated before hashing: at most 64 chunks, 32 MiB payload, 262,144 vertices and
524,288 faces, no splats or textures, and at most 4,096 source frame refs/chunk.
Every chunk must contain current mesh geometry at the expected map pose revision
and a nonempty source-frame table wholly within the chosen inference capture and
pose asset. Declared observed meshes must also bind that exact pose hash/context.
Synthetic meshes still require explicit `synthetic_diagnostic` scene provenance;
the reused reader only accepts synthetic measurement methods for that source.

Actual vertex/index/normal/transform bytes and source metadata participate in the
independently selected scene hash. Changing geometry or context requires new
selection/admission. Existing native scene admission validates observed metadata
checksums; it does not re-decode an original fusion binary or establish that the
declared mesh actually corresponds to measured surfaces. That qualification stays
open, as does undeclared historical evaluation influence.

The private handle owns the selected refs, capture product/receipt/context,
validated alignment bytes/transform, distance/uncertainty and manual ancestry.
It retains no caller scene aliases. No scene/map transform is applied, no scale
status is promoted, and no walking, Q5/N2 or AP pass is enabled. Copies survive
caller/source removal. Manual ancestry is the graph declaration, not an audit
that an operator actually measured something; setup/action accounting remains
the separate manual-action contract.

## Publication and fresh committed reads

`publish_capture_measurement` checks original receipt/context and publishes
`measurement-<product_id>-<revision>` plus `...-frame` atomically through the
existing immutable registry. The two records retain exact measurement/alignment
bytes. Receipt source/output overlap is refused. The native source is in memory;
no original bundle path is discovered or mutated. Callers retain responsibility
for selecting an output disjoint from any separately held scene bundle.

Cancellation after either staged blob exposes no committed records. Exact replay
recovers without replacing input bytes. `read_capture_measurement` requires an
independently selected current registry commit, exact committed hashes for both
keys, and repeats original capture/receipt/native-scene admission. Pending bytes,
wrong/old commits and changed source geometry cannot yield a fresh typed handle.
Other immutable registry records may coexist; this is revisioned metadata, not
the corpus's single-seal directory. An existing owned handle remains historical
evidence if its caller's mutable source later changes.

## Corpus v2

[Corpus schema 2](evaluation_corpus.md) adds explicit `typed_scale=true`.
For every present scale ref, it requires a private independently admitted handle
for that exact measurement, capture product and original receipt. Supplied opaque
refs cannot silently downgrade to schema-1 checking. A missing scale ref remains
an explicit missing prerequisite; a typed handle without its selected ref fails.
Typed admission replaces only the opaque structural-scale blocker, retaining
`MEASUREMENT_AUTHENTICITY_UNVERIFIED`, `GEOMETRY_CORRESPONDENCE_UNVERIFIED` and,
where applicable, `SYNTHETIC_SCALE_DIAGNOSTIC`. Existing rights and real-capture
blockers remain. No declaration qualifies a real pilot or automatic yield.
