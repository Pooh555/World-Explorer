# Observed-depth space diagnostics

`scene_store/depth_space.hpp` provides a separate diagnostic profile for real or
synthetic axial depth. It does not produce `CollisionSupport`, occupied cells,
walkable floors, metric alignment or a measured clearance certificate. The
[synthetic declaration profile](collision_support.md) remains synthetic-only.

`DepthSpaceView::create` validates all source planes and a finite positive world
guard and world-from-camera similarity. It accepts at most 4,194,304 pixels per
view, builds a bounded range pyramid, and supports cancellation during creation.
Views own their summaries; caller depth is unchanged. The application streams
one admitted view at a time. Camera coordinates retain optical +X right/+Y down/
+Z forward and integer pixel centers; depth remains axial camera Z.

For each closed world-axis-aligned cell, all eight transformed corners must have
positive camera Z and finite projections. Projective corner extrema enclose the
whole cell. V1 expands each projected bound by `1e-7` pixels, then uses outward
floor/ceil, including both adjacent pixels at integer centers. Any footprint
outside the image yields `OUTSIDE_VIEW`. Let `s` be the smallest power of two at
least as large as the rectangle's longest inclusive dimension. The queried
footprint is the union of aligned `s × s` blocks touching that rectangle, clipped
at the image boundary. This conservative overcoverage permits at most four mip
queries; it never skips interior pixels to obtain a label.

Every included pixel must be valid, have positive recipe confidence weight and
at least two supporting observations. Otherwise the relation is `UNKNOWN_DEPTH`.
With available sigma, the range includes depth ±2 sigma. Missing sigma remains
**UNAVAILABLE**: nominal depths plus the explicit guard are only a diagnostic
comparison, never a confidence interval. Confidence weights are not probability.
The world guard is divided by camera-to-world scale before comparison:

- `FRONT_DIAGNOSTIC`: the farthest cell corner plus guard is strictly before
  every included lower depth bound.
- `BEHIND_SURFACE`: the nearest corner minus guard is strictly beyond every
  included upper depth bound. This indicates occlusion, not occupied matter.
- `SURFACE_BAND`: neither strict relation holds; includes mixed or crossing depths.

Per-cell evidence preserves counts in relation order: outside, unknown, front,
surface band, behind. Its display summary is unknown with no supported relation,
front-only with front evidence and no surface/behind evidence, surface-or-behind
with no front evidence, or conflict when both occur. Missing observations remain
recorded even alongside another view's diagnostic relation. None of these display
states clears unknown geometry for navigation. Depth sampling, missing source
uncertainty and unqualified reconstruction prevent physical-space certification.

## Application and local viewing

```sh
build/dev/we_process inspect-depth-space REQUEST.json SHA256 NEW_DIRECTORY
build/dev/we_viewer --scene NEW_DIRECTORY/diagnostic.wexplore
```

The shared application command consumes the existing strict, pinned single-cache
or complete-sequence [captured-color request](mesh_color.md). It reuses complete
cache/pose/recipe/depth/RGB admission and decodes each checked RGB image, without
running reconstruction. The fixed `OBSERVED_DEPTH_SPACE_DIAGNOSTIC_V1` recipe
covers every source chunk bound in original pose coordinates, with longest-axis
extent divided by 32 and other dimensions rounded up (minimum one, maximum 32).
Cells use X-fastest order, then Y, then Z. No components, views or unknown cells
are removed. The guard is the pinned source fusion voxel size, in arbitrary pose
units. World-aligned input is unsupported by V1; no scale/up inference occurs.

Outputs are an immutable view-major byte ledger `relations.u8` (codes 0–4 in
relation order), an ordinary native diagnostic bundle, and a terminal
`manifest.json` published last. The report pins the request, full source receipt,
source scene hash, recipe/grid, every view's counts and uncertainty availability,
ledger size/hash and diagnostic manifest hash. `categories_unknown_front_blocked_conflict`
uses "blocked" only as shorthand for the surface-or-behind display category,
never physical occupancy. The bundle preserves all source mesh chunks and adds
one splat per cell: blue unknown, green front-only, orange surface/behind, red
conflict. Its provenance and viewer status explicitly label the diagnostic,
arbitrary scale and unavailable walking. It is not tagged as synthetic capture.

The destination must be new and outside immutable source caches. Cancellation,
stale pins, bad dependencies or failed publication leave sources unchanged and
produce no successful terminal report; an interrupted directory may contain
immutable partial products. Retry uses a new destination. Independent bundle
reload precedes terminal publication. Existing mesh, bake and GLB encodings and
collision admission contracts are unchanged.

## Typed evidence admission

`load_depth_space_evidence(DepthSpaceLoad)` in `scene_store/depth_space.hpp`
returns a shared immutable `DepthSpaceEvidence`. Inputs are an already admitted
raw source snapshot, directory, independently supplied terminal SHA-256 and the
original production-request SHA-256. The fixed V1 source profile is one valid,
unmodified observed mesh chunk in original arbitrary-scale pose coordinates.
Aligned, cleaned/baked and foreign snapshots require a separately defined
profile; admission does not reinterpret their frames.

The reader checks the exact report bytes (maximum 1 MiB), strict field sets,
duplicate keys, raw NUL, nesting, numeric types, digest syntax and diagnostic-only
policy. The complete source receipt must equal the receipt pinned in the source
mesh. Pose epoch/revision/hash, complete ordered unique frame identities, recipe,
uncertainty availability and exact full-source grid must agree. Ledger size is
exactly views × cells, at most `120 × 32³` bytes. All codes, per-view counts,
per-cell counts and aggregate categories are reconstructed and checked. A
supported relation never erases another view's unknown or conflicting relation.

The reader then reconstructs the expected native diagnostic from the source and
ledger. It loads the actual bundle under the exact manifest pin and source-derived
chunk/splat/mesh/frame/payload limits, and compares complete scene content hashes.
This checks original geometry, every cell's position/color/opacity, provenance,
epoch and revision. Rehashed cell permutations preserving totals still fail when
the displayed cell evidence differs. Report and ledger are read from fixed file
names; directories must be canonical and unlinked, and asset files must be regular
and nonsymlink. No paths from the receipt are followed during reload.

This is **consistency admission under a trusted external terminal pin**. It does
not recompute depth classifications, authenticate an arbitrary producer or turn
nominal depth into physical clearance. The original producer/cache admission and
independent classifier evidence retain their separate scopes. No new collision
or walking eligibility is granted.

The admitted object owns its grid, typed relations, frame IDs, sigma-availability
flags, per-cell counts and checked native snapshot. Before reusing evidence with
a current source, call `validate_source`: a changed scene ID/content requires
resync, a changed epoch returns `WRONG_EPOCH`, and a changed revision returns
`WRONG_BASE`. Pose or chunk corrections invalidate the original content binding.
Failed/cancelled admission publishes nothing and preserves existing owners.
Synthetic test sources remain explicitly synthetic in the diagnostic provenance.

The shared application `open_depth_space` first loads the source bundle using its
independent manifest pin. GUI and CLI use this same read-only command:

```sh
build/dev/we_process check-depth-space SOURCE SOURCE_SHA DIRECTORY REPORT_SHA REQUEST_SHA
build/dev/we_viewer --depth-space DIRECTORY --depth-space-report-sha REPORT_SHA \
  --depth-space-source SOURCE --depth-space-source-sha SOURCE_SHA \
  --depth-space-request-sha REQUEST_SHA
```

The viewer runs admission on its owned cancellable job thread and submits the
checked snapshot through the ordinary initial/replacement update path. A pending
handoff cannot be overwritten; failed opens retain the current visible scene.
Opening `diagnostic.wexplore` as a generic scene remains a display operation and
does not perform terminal/ledger admission.
