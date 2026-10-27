# Native static structural collider admission

[`native_collider.hpp`](../../include/world_explorer/application/native_collider.hpp)
admits a distinct receipt-owned collider product against an independently selected
private base geometry, context and cook policy. This A0 slice produces actual
portable triangle input and owned metadata, without a backend shape, support
ledger, navigation authority, convex prop decomposition or AP qualification.

## Root and exact surface binding

Version 1 / `NATIVE_STATIC_TRIANGLE_COLLIDER` contains exactly `schema_version`,
`kind`, `world_epoch`, `pose_revision`, `recipe_sha256`, `base`, `purpose`, `body`,
`units`, `frame`, `cook`, `vertex_sources`, `faces`, `manual_ancestor`. The fixed
labels are `SYNTHETIC_GAMEPLAY`, `STATIC_STRUCTURAL`, `ARBITRARY` and
`BASE_GEOMETRY_LOCAL`. The root depends exactly on the selected base product,
with the same receipt/epoch/pose and inherited manual ancestry. Base identity
already pins its native source, capture, views and original correspondence.

`vertex_sources` is a permutation of every original runtime vertex; positions
copy exactly. Each output face supplies `source_face` and three `vertices` in
the remapped vertex array. Source faces form a complete permutation and each
triangle preserves its source's cyclic orientation. This bijection retains all
components, holes, thin/open surfaces and unknown regions; no subset, bridge,
welding, displacement, thickness or inferred floor is generated. Normals, UVs,
render materials and physical coefficients are not cooked.

The independently selected policy has maximum vertex/triangle counts and weld/
double-sided flags. Its counts must be 3–8,192 vertices /1–8,192 faces; both flags
must be false. The root's `cook` has exactly `format`, `weld_vertices`,
`double_sided`, `max_vertices`, `max_triangles`, matching that selection. Root
bytes are limited to 1 MiB. Strict parsing rejects duplicate/unknown fields, NUL,
excessive depth/counts and invalid indices before use. Actual base geometry still
passes its independent admission; a collider root cannot override its topology
or source classification. Long loops check cancellation; failures publish nothing.

## Portable cook input and ownership

`WE_NATIVE_COLLIDER_TRIANGLES_V1` is an ASCII magic without a terminator, followed
by these little-endian fields:

1. Eleven uint64 values: format version 1, epoch, pose revision, weld flag 0,
   double-sided flag 0, selected maximum vertices/faces, collider product ID and
   revision, base geometry product ID and revision.
2. Three 64-byte lowercase ASCII SHA-256 values: original collider root, base
   product and original receipt.
3. Uint64 actual vertex/face counts, then each local position as XYZ binary64.
   Signed zero normalizes to positive zero.
4. For every output face: uint64 original base face ID, then three uint64 indices.

The private owner retains both admitted base arrays/correspondence and the
derived mesh, root bytes, canonical bytes and their SHA-256. The stream is at
most 1 MiB. It is **cook input**, not a Jolt cache, simulation result, measured
surface or clearance proof. A later backend cache must separately bind backend
version/ABI and these bytes and validate its own losses.

`publish_native_collider` atomically writes `collider-<id>-<revision>` and
`collider-cook-<id>-<revision>` through the existing immutable record store. It
rechecks receipt/context and protects the original receipt tree; callers keep
external native bundle paths disjoint because the base is an in-memory snapshot.
Cancellation retains owned staging without a partial committed pair; exact
operation retries recover/replay. Reload requires the independently selected
current commit, repeats admission, regenerates the cook, then compares both
records exactly. Rehashed foreign cook bytes refuse without altering the store.

## Asset adoption and downstream boundary

`NativeAssetSelection.m_collider` must match the definition's distinct collider
reference, the exact LOD0 geometry and original receipt. Only STRUCTURAL assets
with `SYNTHETIC_GAMEPLAY` bindings are admitted. Collider face provenance covers
every output face and follows its source-face permutation: original evidence
class/views/coverage, null confidence and the base geometry product as parent.
Actual view/calibration/image/pose bindings remain checked. Surface evidence and
gameplay purpose are separate; copied observed faces never grant measured walking.

The collider reference/provenance enters canonical definition identity and every
instance reference. No extra collider source entry is added to `m_sources`;
the collider root already binds its policy and sole base dependency. Existing
asset/scene publication and selected metadata reload retain these exact references.
Original native export profiles refuse colliders without explicit opt-in; the
separate [collider companion profile](collider_exports.md) now carries original
inputs, provenance and checked placements. No collider is installed in a viewer,
Jolt owner, measured collision session or engine. Real production/scale/support,
convex props, backend cooking, engine mapping and AP8 budgets remain open.
