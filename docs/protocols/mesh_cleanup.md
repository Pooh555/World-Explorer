# Certified observed mesh cleanup

This is a bounded M3 development simplifier for admitted partial fusion meshes,
not reconstruction repair or measured geometry. It preserves the original assets,
all components and their unknown gaps. No component deletion, hole filling,
smoothing, position welding, metric alignment, collision or walking is enabled.

## Fixed policy and whole-surface certificate

`simplify_fusion_mesh` proposes independent local patches in ascending source
vertex order. The maximum displacement is **voxel_size / 4**, in the parent's
arbitrary camera/world units. Patch diameter is limited to two voxels. Every
affected star vertex must have a connected cyclic manifold link, edge incidences
of two and valence at most 64. Boundary/nonmanifold vertices and their neighboring
stars are protected. All eight directional floor-cell stencil corners at every
star vertex must be observed with positive weight, at least two observations and
`support_sum >= 2 * observation_count`. These checks retain the
[diagnostic stencil's limitations](fusion.md#read-only-component-and-support-analysis);
they are not independent geometric truth or confidence probabilities.

Two primitives share these limits:

- A degree-three interior fan replaces three consistently oriented faces by their
  outer triangle, which must not already exist. A proposed convex point
  `q = a + u*(b-a) + v*(c-a)` has all barycentric weights at least 1/64.
- A valence-4–64 interior star collapses its center to an adjacent endpoint within
  the same displacement limit. The vertices' common link must consist exactly
  of the two opposite edge vertices. Surviving faces must remain nondegenerate,
  preserve orientation and introduce no duplicate faces. Each collapsed face's
  image edge must occur in a surviving output triangle.

Accepted patches share no vertices, so displacement never accumulates. For a fan,
mapping its center to `q` maps its three faces onto a partition of the outer
triangle. For an edge collapse, each source triangle maps onto an output triangle
or an explicitly retained image edge. Barycentric interpolation bounds every
point's displacement by the center's displacement; every output point also has
a source preimage with this bound. Untouched triangles have zero displacement.
The maximum patch bound therefore bounds **both whole-surface distances**.
This is a conservative upper bound, not an exact Hausdorff measurement or an
error to measured ground truth. It covers removed faces, not just retained vertices.

Binary64 interval operations round outward with `nextafter`, including the
distance bound and positive face-normal dot products. IEEE nearest rounding is
required; another active rounding mode rejects. Numerical projection only proposes
fan weights; the stored coefficients and interval certificate are revalidated.
The policy does not certify global self-intersection freedom. Retained positions
and diagnostic normals copy exactly from the parent; unused vertices are compacted
in ascending source-index order. No captured color or material is inferred.

## Immutable derived product and admission

```sh
build/dev/we_process cleanup-fusion INPUT.wefusion ASSET_SHA MANIFEST_SHA RECEIPT_SHA RECIPE_SHA POSES.weposes POSE_SHA NEW_DIRECTORY
```

The application admits the exact parent bytes against current separately pinned
poses, generates and replays the certificate, and computes complete before/after
component/area/support/coverage reports before publication. The destination must
be new, with an existing parent; existing entries, including symlinks, reject.
It publishes these files without overwriting caller data:

| File | Meaning |
|---|---|
| `mesh.wecleanup` | Independently SHA-pinned UTF-8 certificate referencing the parent SHA; no duplicated mesh/voxel payload |
| `report.json` | Full before/after component reports, original grid coverage/unknown metadata, generation decision ledger and both distance upper bounds |
| `scene-request.json` | Original eight-field fusion request plus `cleanup` and `cleanup_sha256`; parent/poses remain local references |
| `manifest.json` | Atomically published last, with file sizes/hashes, parent and resulting scene identity |

Certificate envelope version 1 / kind `OBSERVED_AFFINE_CLEANUP_V2` contains exactly
`schema_version`, `kind`, `parent_asset_sha256`, `voxel_size`, `error_bound`,
`maximum_distance_upper_bound`, `input_vertices`, `input_triangles`,
`output_vertices`, `output_triangles`, `mesh_sha256`, `patches`. Each patch has
`vertex`, `target`, `triangles`, `ring`, `barycentric_uv`, `distance_upper_bound`.
`target = 4294967295` selects the three-face fan; otherwise it is the collapse
endpoint. Source face lists and ring membership/order must match native validation.
Version-1 fan-only certificates remain readable. Duplicate keys, raw NUL, nested
JSON beyond depth five, wrong types/counts/order, unsupported policy, altered
parent/geometry and inadequate distance bounds reject, even after rehashing.

The geometry SHA-256 hashes little-endian uint64 vertex/triangle counts, followed
by each retained vertex's six float32 position/normal values, then uint32 triangle
indices. Source-index remapping is deterministic. Native replay checks topology,
support, orientation, interval bounds and output hashes before scene publication.
Limits are **500k input vertices / 1M triangles / 8,192 patches / 4 MiB certificate /
64 MiB report**; ordinary fusion/scene limits still apply. Over-budget input rejects
without silently coarsening. Long loops check cancellation; edge sorting and final
hashing are bounded indivisible work. Allocation failure returns an explicit error.

Failure preserves caller files and may retain partial diagnostics. A late
publish/directory-flush error requires reopening the manifest to determine its
publication state; an error does not authorize deleting the output directory.
This native command does not implement production-worker resume or kill/reap.

The existing shared native loader/viewer accepts the derived request:

```sh
build/dev/we_process inspect-fusion-scene NEW_DIRECTORY/scene-request.json REQUEST_SHA
build/dev/we_viewer --fusion-request NEW_DIRECTORY/scene-request.json --fusion-request-sha REQUEST_SHA
```

Parent pose/coverage metadata and exact cleanup text/hash enter immutable scene
chunks. The same update/admission/upload/invalidation path renders the proxy, with
an explicit simplified-surface label and walking disabled. Original requests still
open the raw geometry. Bundle schema 1 still refuses mesh snapshots; portable mesh
bundles and baked-color export remain separate work. [Validation](../validation/2026-10-19-mesh-cleanup.md)
records the frozen first-pass no-op, real reductions, exact certificate oracle and
actual native/profile limits without certifying Q5/N2 or asset gates.
