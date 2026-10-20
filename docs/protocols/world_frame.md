# Explicit world-frame alignment

`scene_store/world_frame.hpp` admits a bounded local declaration against an
immutable arbitrary-scale scene. This implements frame admission and application;
it does not independently verify a physical measurement, geometry quality or
walkable support. A legacy `METRIC_MEASURED` flag carries no measurement proof.

## Measurement document

`WORLD_FRAME_ALIGNMENT_V1` is exact SHA-256-pinned JSON, at most 8,192 bytes and
eight nesting levels. Duplicate/unknown fields, raw NUL, decoded NUL in records,
nonfinite/nonnumeric scalars and fractional/negative IDs are rejected. IDs and
indices are unsigned JSON integers, including values beyond binary64 precision.

| Field | Contract |
|---|---|
| `schema` | `WORLD_FRAME_ALIGNMENT_V1` |
| `source_sha256`, `source_epoch`, `source_revision` | Exact `scene_content_hash` and source identities; wrong context requires resync |
| `evidence_kind` | `INDEPENDENT_MEASUREMENT`, or `SYNTHETIC_DIAGNOSTIC` exclusively for scenes labeled `source_kind=synthetic_diagnostic` |
| `distance` | `a`, `b` anchors; positive finite `metres`; `0 ≤ uncertainty_metres < metres`; `method`; nonempty `record` up to 1,024 bytes |
| `up` | Nonzero finite `direction` in source world coordinates; `0 ≤ uncertainty_degrees < 90`; `method`; nonempty bounded `record` |
| `origin` | Mesh anchor mapped to the aligned origin |

An anchor is `{ "chunk_id": ID, "triangle": INDEX, "barycentric": [A,B,C] }`.
It references a current mesh, with nonnegative finite weights summing to one
within 1e-12. Resolve its vertex interpolation through the original chunk
transform. Distance endpoints must differ. All meshes must be current; source
content pins also include pose revisions, generation, appearance and provenance.

Independent distance method is `MEASURED_DISTANCE`; up method is `IMU` or
`USER_CONFIRMED_FLOOR`. Records identify the points, measurement method/source
and up reference. They are operator assertions, not authenticated sensor records.
Synthetic records use method `SYNTHETIC` for both. Monocular priors and a floor
inferred from reconstruction cannot substitute for these inputs. Uncertainties
are retained declarations, not confidence certificates or relaxed Q5 thresholds.

Scale is measured distance divided by source endpoint distance. The shortest
rotation maps declared up to world +Y; yaw remains arbitrary. Translation maps
the origin anchor to zero. Derived transforms/bounds must remain finite. Cached
transform fields are recomputed and checked on scene admission; editing the
cache independently is invalid.

## Publication, corrections and consumers

An alignment transaction has no chunk operations and advances both world epoch
and scene revision by exactly one. It retains immutable chunk objects, original
source epoch/revision and document bytes. Retried operation IDs are idempotent;
changed content under the same ID is rejected. Failed admission leaves the store
unchanged. Old revision replay requires a full snapshot after alignment.

The aligned snapshot is immutable. Chunk changes, even a revision-only operation,
require resync. A pose correction uses the existing full-snapshot reset with a
newer world epoch and corrected source assets, then a new source-bound declaration.
It cannot keep stale proof, bake dependencies or collision context. Bookmarks in
the previous epoch fail restoration. No collision producer is implemented:
walking is blocked for missing proof, synthetic proof, or unavailable supported
collision even after independent declaration admission.

A separate scene transform composes with each original chunk transform. Bounds,
mesh positions/normals, Gaussian positions/scales/orientations and local SH view
directions use that composition; covariance follows s² R Σ Rᵀ. Alignment identity
participates in renderer fingerprints and resident reuse checks. Source-view
position/orientation and spectator speed follow the frame, with existing speed
limits retained. Captured lighting/color and texture ownership are unchanged.
Synthetic alignment retains `ARBITRARY`; independent declaration admission sets
`METRIC_MEASURED` with an explicit geometry-unverified label. Neither enables walk.

Schema-5 native bundles add `payload_schema_version` (1–4), `world_frame` and
`world_frame_sha256`. The separately pinned document precedes atomic manifest
publication. Chunk/index/provenance encoding is unchanged; legacy unaligned
bundle bytes and scene hashes stay unchanged. Loading reconstructs the source
identity and re-admits its document. Older schemas reject alignment fields.
Current GLB export and owned postprocessing profiles reject aligned snapshots;
there is no metric GLB profile yet.

## Shared command and local checks

`align_world_scene(WorldAlignmentRequest, stop_token)` implements the CLI path:

```sh
build/dev/we_process align-world SOURCE.wexplore MEASUREMENT.json SHA256 NEW_BUNDLE
build/dev/we_process validate NEW_BUNDLE
build/dev/we_viewer --scene NEW_BUNDLE
```

The destination must be new and outside the source bundle. Checked admission and
cancellation precede reservation; publication independently reloads the result.
Failure may retain an incomplete newly reserved output; no source is overwritten.
The CLI honors cancellation before publication; bundle publication itself uses
its existing atomic manifest boundary.

[Local validation](../validation/2026-10-20-world-frame.md) contains the executable
synthetic fixture, native texture/splat comparisons and unchanged full real mesh
regression. The user has no independent sample_1 distance/up reference, so no
real metric alignment was produced. Q5/N2 and real measured walking remain open.
