# Captured-color projection audit

M3's read-only audit checks calibrated color candidates before texture production.
It does not publish a scene/material, usable texture, UV-unwrapped export, PBR
albedo, continuous surface coverage or walking readiness. The native viewer still
shows the existing [normal diagnostic](scene.md#observed-mesh-runtime).

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

At most 18 views, 48M total source pixels and 1.8M samples (200k triangles) are
accepted; excess rejects without trimming. Existing fusion/scene/depth byte/count
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
