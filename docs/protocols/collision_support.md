# Synthetic collision-support admission

`scene_store/collision_support.hpp` admits immutable diagnostic space support and
checks conservative capsule envelopes against it. This is a synthetic-only M3
software profile. There is no observed-space producer, measured collision,
character controller or walking permission. A metric flag, a drawn mesh and an
allocated TSDF voxel cannot establish free space.

## Pinned support document

`SYNTHETIC_COLLISION_SUPPORT_V1` is SHA-256-pinned JSON, at most 1 MiB with eight
nesting levels. Exact fields, unsigned integer IDs/indices and finite numeric
scalars are required. Duplicate keys, raw NUL, unknown fields, invalid states and
unsupported profiles fail admission. Source scenes must validate with a synthetic
[world-frame alignment](world_frame.md); real or flag-only scenes are refused.

| Field | Meaning |
|---|---|
| `schema` | `SYNTHETIC_COLLISION_SUPPORT_V1` |
| `scene_sha256`, `epoch`, `revision` | Exact aligned source content and identities |
| `world_frame_sha256` | Exact admitted alignment declaration |
| `grid` | `origin` vector, `cell_size`, three `dimensions`, complete `states` string |
| `floors` | Explicit source chunks and selected floor triangles |

Dimensions are 1–64 per axis (at most 262,144 cells); cell size is 1e-6–1,000 in
synthetic scene units; each origin coordinate has magnitude at most 1e6. These
are diagnostic format/precision limits, not changed product acceptance targets.
X varies fastest, then Y, then Z. Every cell is explicit: `.` free, `#` occupied,
`?` unknown. No omitted cells or inferred free values are allowed; outside the
grid remains unknown.

Each floor record contains `chunk_id`, `generation`, `pose_revision`,
`chunk_sha256` and `triangles`. Up to 256 records and 256 total unique triangle
references are admitted. Chunks must have current geometry and exact dependency
pins. Selected triangles must have upward geometric winding, lie exactly on the
horizontal plane `origin.y`, and have vertices on the bounded integer X/Z grid
lattice. Source-to-grid conversion must round-trip exactly; no epsilon snaps a
gap closed. Slopes, steps and non-lattice geometry are outside this profile.

A whole X/Z floor tile is supported only when one selected triangle contains
all four corners. Integer orientation tests make this coverage conservative.
Tiles crossing a triangle seam can remain unsupported even when several adjacent
triangles jointly cover them. This deliberate false block does not fill a hole.

## Clearance and invalidation

`probe_collision_support` accepts start/end feet, positive radius and height at
least twice the radius. Feet must stay exactly on the supported flat plane.
It checks the complete continuous axis-aligned box enclosing the swept vertical
capsule, with outward-rounded bounds and adjacent cells at contact boundaries.
It does not sample only endpoints. All touched cells and floor tiles must pass;
leaving the grid, unknown space, occupied space or unsupported floor blocks the
probe. A diagonal envelope may block even if the exact capsule misses an obstacle.
This is a bounded clearance check, not collision response or Jolt simulation.

Results are `CLEAR_DIAGNOSTIC`, `UNKNOWN`, `OCCUPIED` or `UNSUPPORTED_FLOOR`, with
checked-cell counts. `CLEAR_DIAGNOSTIC` never authorizes walking. Admission and
probing honor cancellation; a failure preserves the immutable admitted support.
Scene ID/epoch, revision and complete source hash are checked again on probing.
Pose/geometry/provenance corrections cannot silently reuse old support: stop and
resynchronize. Existing scene reset and renderer update contracts remain intact.

## Shared inspection and ordinary viewing

```sh
build/dev/we_collision_fixture artifacts/NEW_SYNTHETIC_INPUT
build/dev/we_process inspect-collision artifacts/NEW_SYNTHETIC_INPUT/request.json REQUEST_SHA artifacts/NEW_INSPECTION
build/dev/we_viewer --scene artifacts/NEW_INSPECTION/diagnostic.wexplore --max-splats 1000000
```

The development fixture executable produces an analytic aligned source, explicit
space states and four probes. It never reads capture data. Its output is new and
clearly labeled synthetic.

The shared application command consumes a SHA-pinned, ≤1 MiB request with kind
`COLLISION_SUPPORT_INSPECTION_V1`, `source_bundle`, `source_manifest_sha256`,
`support`, `support_sha256` and `probes` (at most 256). Relative paths resolve
against the request directory. Each probe has `start_feet`, `end_feet`, `radius`
and `height`. The optional external pin in `load_bundle` verifies the exact
manifest bytes decoded, rather than relying on a separate pre-load hash.

Admission, all queries and cancellation precede exclusive output reservation.
The destination must be new and outside the immutable source. The command writes
an ordinary synthetic splat bundle, independently reloads it, then atomically
publishes `manifest.json` containing pins/counts/results and explicit
`walking_enabled=false`, `measured_collision=false`. Failure after reservation
can retain a partial new output, with no completed report. Original inputs are
unchanged.

The viewer displays green free samples, red occupied samples, blue unknown
samples and amber unsupported floor tiles, with a synthetic legend and walking
disabled. These Gaussian samples visualize cell labels, not solid collision
boundaries. The diagnostic is an independent arbitrary-scale scene; it is never
installed as collision for the source reconstruction. Legacy bundle encoding,
scene hashes and captured GLB profiles remain unchanged.

[Validation](../validation/2026-10-20-collision-support.md) records CPU/native
checks and retained real-scene refusal. No real measured alignment/support exists
for sample_1. Q5/N2 and the remaining M3 product gates are unchanged.
