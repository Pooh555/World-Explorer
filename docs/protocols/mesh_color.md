# Captured-color projection audit

M3 provides a read-only sampled audit and a separate bounded captured-color atlas
baker. Both share the pinned input admission below. Native baked viewing preserves
unknowns; neither path certifies continuous surface coverage, PBR albedo, measured
geometry or walking readiness. Restricted captured-unlit interchange is separate
from the diagnostic audit.

```sh
build/dev/we_process audit-mesh-colors AUDIT.json AUDIT_SHA256 NEW_DIRECTORY
```

The externally pinned, at-most-1-MiB request has exactly five fields:

```json
{
  "schema_version": 1,
  "fusion_request": "RAW_OR_CLEANED_SCENE_REQUEST.json",
  "fusion_request_sha256": "SCENE_REQUEST_SHA256",
  "cache": "DEPTH_RGB_CACHE/manifest.json",
  "cache_sha256": "ADMITTED_RECEIPT_SHA256"
}
```

Paths resolve relative to the request. Duplicate keys, raw/path NUL, nesting
deeper than eight, incorrect types/pins and missing/unknown fields reject.
The shared application command first replays the fusion/optional cleanup admission
against current pinned poses. The cache must byte-match that source's original
visibility receipt; replacing its checksum in the request cannot authorize a new
cache. Each depth is admitted against the receipt's exact calibrated grid/recipe
and the canonical pose/observation dependencies. RGB files retain their encoded
checksums and sizes. Cache image/asset paths accept one filename under `images/`
or `assets/`, rejecting linked files/directories. Outputs must be new and outside
the immutable cache.

Request schema **2** uses the same five fields and explicitly selects a
`POSE_ADMITTED_DEPTH_SEQUENCE_V1` receipt. Schema 1 retains the single-cache
contract and cannot silently admit a sequence. The union retains up to 120
ordered frames; every pinned child is a `POSE_ADMITTED_DEPTH_VISIBILITY_V1`
cache with its original 18-view / 48M-pixel limits. Batches must form a complete
ordered partition with exact frame rows, pose/recipe/input-manifest identities
and valid/unknown totals. Each child's frame metadata is checked against its
hashed original dense manifest, including intrinsics and camera transforms.
Every encoded file is size/hash checked before projection, including poses,
both recipes, the dense manifest, depth and RGB; children retain their 768-MiB
encoded aggregate cap. Canonical file paths reject symlinked parents or files.
Views are then decoded one at a time from the selected child, rechecking depth,
RGB and pose pins and native depth admission. Actual depth validity and sigma
availability must match the receipt. No union of decoded source planes is held.

`decode_calibrated_rgb` runs installed FFmpeg with argument arrays, one frame,
no rotation/resize, bounded allocation/output/diagnostics and explicit cancellation
through the existing terminate/reap owner. A P6 header checks actual width/height
before returning RGB8; equal pixel counts with different shapes reject. Input
checksums are checked before and after decoding. There is no model/decoder download.

## Fixed sampled policy

Every original triangle is retained, in source order. Six interior barycentric
samples use `(u,v)` = `(.125,.125)`, `(.375,.125)`, `(.625,.125)`, `(.125,.375)`,
`(.375,.375)`, `(.125,.625)` for `a*(1-u-v)+b*u+c*v`. Three additional samples
use edge midpoints `(a+b)/2`, `(b+c)/2`, `(c+a)/2`. Normals are the world-space
face cross product; degenerate/nonfinite samples reject without dropping faces.

The owned `ColorProjectionView` validates its depth planes, RGB size, camera
transform and voxel size once. Application admission binds source identities;
the numerical projector alone does not authenticate an external RGB image/pose.
Each candidate then requires:

- Finite world point/normal and positive optical camera z, using the inverse
  world-from-camera similarity, optical +X right/+Y down/+Z forward and integer
  pixel centers.
- A complete in-image 2×2 footprint, even at an integer pixel where some
  interpolation weights are zero. All four depths must be valid, with positive
  recipe-defined confidence and support count at least two.
- Absolute face/view incidence cosine at least 0.1. Faces are treated two-sided;
  this does not establish that a reconstructed backside is observed.
- Every footprint corner within `2*world_voxel_size/camera_world_scale + .01*z`
  of the sample's camera-space axial depth. Foreground/background mismatch and
  unknowns reject; depth is never interpolated through a missing corner.

This frozen diagnostic envelope is not a measured occlusion guarantee. Sigma
availability is preserved in the parents and explicitly **unused**, not replaced
by zero uncertainty. Supported RGB8 values are interpreted as sRGB, converted
with its piecewise transfer function, then bilinearly interpolated in linear RGB.
There is no exposure harmonization, source blending, dilation or hole filling.

Candidate score is `abs_incidence/(1+maximum_corner_depth_residual/tolerance)`.
The highest score owns a sample; exact ties select the lower uint64 frame ID.
The candidate bitmask, selected owner/color/score and per-channel candidate spread
remain auditable. Rejection ledgers use exclusive priority: invalid sample,
behind camera, outside image, unknown depth, unsupported depth, grazing, depth
mismatch, otherwise accepted. Every view's counts sum to all nine samples/face.

Schema 1 accepts at most 18 views / 48M source pixels; schema 2 accepts at most
120 views with those same per-child limits. Both retain 1.8M samples (200k
triangles); excess rejects without trimming. Existing fusion/scene/depth byte/count
limits also apply. Depth/RGB are streamed one view at a time. Cancellation is
checked during admission and every 4,096 projection samples.

## Diagnostic publication and interpretation

Immutable files publish before terminal `manifest.json`:

| File | Contract |
|---|---|
| `samples.bin` | Little-endian `WECLR001`; three uint64 counts: triangles, views, samples; then 44-byte records in face/sample order: uint64 owner, uint32 view bitmask, 3×float32 linear RGB, float64 score, 3×float32 candidate RGB spread |
| `atlas.ppm` | P6 RGB8 contact sheet, one 4×4 tile/face, `ceil(sqrt(triangles))` columns; only the six interior locations are drawn. Unknown is magenta, unsampled padding dark gray. It is not a usable texture |
| `report.json` | Source/scene/pose/cache/cleanup pins, frozen policy, exclusive view ledgers, all sample/face denominators and selected-owner/view-count histograms; candidate color spread and shared-edge midpoint seam measurements |
| `manifest.json` | Request/scene pins and exact sidecar byte sizes/SHA-256; terminal diagnostic completion only |

Schema-2 audits publish `CAPTURE_COLOR_PROJECTION_AUDIT_V2` report/manifest
objects and `WECLR002` samples. The 32-byte header is unchanged; each **56-byte**
record replaces the uint32 mask with **four little-endian uint32 words** in
ascending view order (view `i` is word `i/32`, bit `i%32`). Remaining fields keep
their order/types. Bits 120–127, and bits beyond the actual frame inventory,
remain zero. Histograms include every count from zero through the sequence view
count. Global score selection and lower-uint64-ID tie breaking span all children;
no per-child winner substitutes for the union. The legacy `WECLR001` 44-byte
layout and 19-entry histogram remain unchanged. Unsupported consumers must reject
the new version rather than truncate its masks.

An unknown sample has owner/mask/color/score/spread all zero. Candidate masks use
receipt view order. Edge reports retain boundary/nonmanifold/two-face counts;
seam comparisons require known samples on both adjacent faces and retain unknown
comparisons. Mean/maximum seam values are null if no comparisons exist.

Six samples per triangle do not measure observed room area, certify unsampled
texels/interiors, or cover unknown space outside the mesh. Reported seam jumps
are measurements, not a seam-quality pass. Original geometry, source receipts,
cleanup certificates and coverage/support/component denominators remain unchanged.
Rejection preserves caller files; cancellation during decoding reaps its child.
Late publication failure/cancellation can leave an uncommitted new directory;
consume nothing without its terminal manifest. Production job resume, kill/crash
fault injection, texture export and native textured rendering remain separate.
See [full retained-source checks](../validation/2026-10-19-color-projection.md).
[Sequence checks](../validation/2026-10-20-color-sequence.md) cover complete
63-view projection, preserved legacy bytes, independent candidate/owner/coverage
oracles, active decoder cancellation and full sanitizer parity.

## Bounded native baked appearance

```sh
build/dev/we_process bake-mesh-colors AUDIT.json AUDIT_SHA256 NEW_SCENE.wexplore
build/dev/we_process inspect-baked NEW_SCENE.wexplore
build/dev/we_viewer --scene NEW_SCENE.wexplore
```

The baker replays the same pinned input admission and streams one calibrated view
at a time. It accepts at most 100,000 source triangles, 18 ordered source views
and 48M source pixels, rejecting excess rather than trimming geometry. Every
triangle receives an independent 8×8 atlas tile; columns are `ceil(sqrt(T))`, rows
`ceil(T/columns)`. Its 36 surface lattice points have integer `x+y<=7` and
barycentrics `((7-x-y)/7,x/7,y/7)`. Original positions and interpolated original
vertex normals supply the projector above. Shared source edges therefore use the
same point/normal field. Geometry, topology, support and cleanup certificates are
preserved exactly. The atlas is deliberately coarse; this is a bounded fallback.
The baker explicitly rejects schema-2 sequence inputs before accumulation or
publication: its persisted appearance still has uint32 masks and an 18-view cap.
Sequence diagnostic success does not qualify sequence baking or textured export.

For each independently projected lattice point, accepted source colors are
averaged in linear sRGB with squared candidate scores as positive weights.
There is no exposure correction, outlier removal, depth-policy retuning or
inferred color. Dominant score/lower-ID tie ownership and the full candidate
bitmask remain stored. A valid RGBA texel has alpha exactly one; unknowns and tile
padding have all-zero RGBA, zero owner and zero candidates. Known texel counts
refer to the partial source mesh, never room coverage. No dilation or hole filling
is performed. Source incidence/depth uncertainty limitations still apply.

Each corner stores pixel-coordinate UVs at `(tile_x+.5,tile_y+.5)`, `(+7,0)` and
`(0,+7)`. The Vulkan fragment shader performs triangular linear interpolation on
this lattice. Every positive-weight corner must be valid; otherwise the fragment
is discarded, preserving unknown background/depth. Filtering never reads a
neighboring tile or blends through an unknown. Tile origin is a flat per-triangle
varying; every atlas read also checks dimensions, including helper invocations
outside small triangles. Linear RGB is converted to display sRGB before the
shared UNORM presentation path. Atlas RGB is captured outgoing
appearance, displayed unlit, **not relightable albedo**. The native viewer selects
Baked captured color for mesh-only baked bundles; Observed surfaces retains the
normal diagnostic. Walking stays disabled.

`BakedMeshAppearance` owns linear float32 RGBA, uint64 owner IDs, uint32 candidate
masks, corner UVs, ordered frames, an untextured chunk hash and exact hashed authoring
JSON. Validation enforces the canonical layout, finite ranges, binary validity,
mask/owner membership, zero padding, strict metadata semantics and current source
hash. Current observed pose context must match every input view. Cancelled partial
accumulation cannot be finalized or retried; finalization cancellation publishes
nothing. Application admission and projection finish before atomic bundle
publication begins; the writer itself finishes or reports failure.

[Schema 3](scene.md#baked-appearance-bundle-schema-3) persists the immutable result.
Scene limits cap aggregate texture texels at 8M and account for RGBA, masks,
owners, UVs and metadata in payload budgets. GPU upload includes atlas RGBA and
expanded per-corner vertices in the existing staged residency budget and rejects
storage-buffer range excess. Replacement becomes visible only after upload
completion; geometry invalidation hides its baked appearance. Source frames,
parents and input caches remain external authoring dependencies.
[Restricted unlit GLB export/import](gltf.md) is available with declared
quantization/conservative filtering losses. Production jobs/resume, general
interchange and material-quality qualification remain open.
See [actual checks](../validation/2026-10-19-mesh-textures.md).
