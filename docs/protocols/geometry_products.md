# Native-source geometry products v1

[`geometry_product.hpp`](../../include/world_explorer/application/geometry_product.hpp)
adds owned geometry/correspondence admission to `application`, where native scene,
capture and asset-receipt contracts meet. A0/AP0 remain **IN_PROGRESS**. This is an
additive bounded profile; the existing generated-static payload/edit profile and
its encodings remain unchanged. No solver, asset-definition adoption, native render
transaction, measured units, collision or AP/Q/N approval is added.

## Independent source and inference binding

`GeometryProductRequest` supplies optional independent selection, private capture
and receipt handles, a native `SceneSnapshot` and expected epoch/pose revision.
Missing prerequisites return INVALID_ARGUMENT before interpreting geometry. A
complete request validates the original receipt/context and selected product's
inference dependency closure, including known evaluation content/recipe/action
aliases. This does not authenticate an undeclared historical influence.

The selection pins root/source products, native scene ID/epoch/revision/content
hash, chunk ID/generation/content hash, 1–16 full inference views and an independent
finite maximum correspondence deviation in arbitrary source units (0–1,000,000).
This bound is a declared policy, not a Q5/AP threshold or measured uncertainty.
The native source requires arbitrary units, no world alignment, explicit
`source_kind=synthetic_diagnostic` or `partial_observed_geometry`, and untextured
mesh-only chunks. The selected chunk must be geometry-valid at the expected pose
revision; its distinct frame IDs must exactly equal the selected inference frames.
Every selected view must equal an admitted inference view, including native pose,
calibration and image identities. Native observed metadata, when present, must
bind the same epoch/pose revision and selected pose-asset hash.

The source-binding product depends exactly on deduplicated selected image/pose
products; the root depends on those products plus the source binding. Both pass
the existing graph/context checks. The root additionally binds the original
capture product in coordinator metadata. The full capture manifest is not a graph
input: it contains evaluation-only metadata. No new worker input is exposed.

## Geometry and support checks

The high/source mesh comes from actual selected native chunk arrays. Runtime
positions/normals, triangles, material-group IDs and optional UV/tangent arrays
come from owned product bytes. Their coordinate frame is the native source chunk's
right-handed +Y-up local frame; the handle retains its exact world transform.
Runtime world points must remain finite. There is no pivot rebase or implicit scale
conversion. Material group IDs are declarations, without material payload admission.

Positions and unit normals are finite. Faces have valid distinct indices, nonzero
finite area and winding compatible with all corner normals. Duplicate faces and
unused vertices fail. Edges have at most two oppositely oriented incident faces;
each vertex fan must be connected. OPEN_SURFACE_ALLOWED permits declared boundary
edges; CLOSED_MANIFOLD requires paired edges. Disconnected components with separate
vertices are allowed. These are topology checks, not self-intersection or complete
room/shell validation.

UVs and tangents are either both absent (explicit null) or complete per vertex.
UVs are finite in [0,1] with nondegenerate face UVs. Tangents have unit xyz,
orthogonality to normals and w exactly ±1; their orientation/handedness must agree
with finite UV position derivatives. Chart overlap, seams/stretch, padding,
texel/bake coverage, LOD and collider quality remain unverified.

Every runtime face has exactly one support row with an evidence class,
optional three source-triangle/barycentric anchors, distinct selected support
views and nullable uncertainty. Anchor weights are finite [0,1] and sum exactly
to 1 in the stored representation. Actual source coordinates are interpolated;
computed corner distances must be finite and within the independent deviation
bound. Nullable uncertainty remains unknown; a supplied finite nonnegative value
≤1,000,000 is an uncalibrated declaration, never an error-bound guarantee.

- OBSERVED requires native observed metadata, nonempty inference support, exact
  runtime/source anchor equality and all three anchors within one source triangle.
  Thus the whole nondegenerate output triangle lies in that convex source triangle;
  matching isolated corners on unrelated source faces cannot fill a hole as observed.
- INFERRED requires anchors/support but may cross source triangles or displace
  within the selected bound. Its faces never enter observed counts.
- UNKNOWN/GENERATED require null anchors/uncertainty and empty source support.
- MANUALLY_EDITED requires graph-derived manual ancestry; optional anchors/support
  still undergo the same checks. Every product retains transitive manual ancestry
  regardless of face labels.

Support/provenance and native source declarations are not physical observation
proof. This path does not re-admit original depth/voxel receipts or verify that a
declared image actually supports the face. Physical correspondence, visibility,
uncertainty calibration and original reconstruction quality remain open. Coverage
counts are face counts, not area or measured Q5/N2 coverage.

## Exact schemas and bounds

The root has exactly `schema_version=1`, `kind=GEOMETRY_PRODUCT`, `world_epoch`,
`pose_revision`, `recipe_sha256`, `capture`, `source`, `views`,
`frame=SOURCE_CHUNK_LOCAL_RIGHT_HANDED_Y_UP`, `surface`, `max_deviation_units`,
`mesh`, `support`, `manual_ancestor`. Mesh keys are `positions,normals,uv,tangents,
triangles,groups`; support rows are `class,anchors,views,uncertainty_units`.
Anchors have `triangle,weights`; support views have `capture_id,frame_id`.
Refs/full views keep the existing capture selection shapes.

The source binding has exactly `schema_version=1`, `kind=NATIVE_GEOMETRY_SOURCE`,
`world_epoch`, `pose_revision`, `recipe_sha256`, `native`, `manual_ancestor`.
Native keys are `scene_id,world_epoch,scene_revision,scene_sha256,chunk_id,generation,
chunk_sha256`. Headers/context/recipe/manual flags must match the actual graph;
the native record must exactly match the independent selection and scene bytes.

Root ≤4 MiB (the immutable registry's per-record cap), source binding ≤16 KiB;
runtime ≤8,192 vertices/faces, ≤16 material groups and ≤65,536 support-view entries
(≤16/face). Native source ≤64 chunks/32 MiB payload/262,144 vertices/524,288 faces/
4,096 source frames per chunk; observed metadata totals ≤4 MiB. Provenance has
≤32 entries with keys/values ≤4,096 bytes. Arrays/text are bounded before native
validation/hashing or owned copies. Individual maxima need not fit simultaneously.
Sources whose complete frame set exceeds this profile's 16 inference views are
unsupported; frames are never dropped to make admission succeed.
Strict JSON rejects duplicate keys, raw NUL, nesting over 16, unknown fields/enums,
invalid hashes/types and nonfinite arithmetic. Cancellation is checked throughout
admission; native hashing/scene validation has bounded work without a stop-token API.

## Ownership, publication and reload

The private immutable handle owns runtime/high mesh arrays, support summaries,
world transform, normalized selections and both original record byte buffers.
High-mesh getters retain geometry arrays only, without duplicating opaque native
observed metadata. Source classification, manual ancestry and topological closure
remain explicit. Copies survive caller mutation/removal and cannot convert into a
scene snapshot or inference input.
Callers keep the supplied native snapshot stable during admission; concurrent
mutation through another producer alias is outside this contract.

Publishing rechecks the original receipt/context and writes `geometry-<id>-<rev>`
plus `geometry-source-<id>-<rev>` atomically through the existing immutable registry.
Receipt-tree overlap is protected. Callers must keep external native bundle paths
disjoint from output: the in-memory source supplies no filesystem root to protect.
Staged cancellation commits neither record; exact operation/base/byte retry
recovers. Fresh reads require the independently selected current commit and both
hashes, then repeat admission against the original capture/receipt/native source.
No separate crash mechanism or global asset authority is introduced. Integration
with full asset definitions, material products and producers remains open.

The separate [native asset-adoption path](native_assets.md) now binds these
private handles to checked revisioned definitions/instances. It preserves this
profile and adds no producer, renderer/export or real AP qualification.
