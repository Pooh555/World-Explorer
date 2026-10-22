# Selected synthetic step geometry and clearance

`SYNTHETIC_COLLISION_SUPPORT_V4` admits one source-bound, positive X step for
synthetic diagnostics. It extends V2's required fields with `step` and permits an
empty `walls` array only. `step` contains `boundary_x` (an interior X grid line),
`height` (finite, greater than zero and at most 0.30 synthetic units), and a
`riser` source reference. The riser reference has the same chunk generation,
pose revision and content hash pins as a selected floor, plus exactly two
triangle indices. V1–V3 documents and canonical encodings are unchanged.
Existing V4 documents remain geometry-only: their queries return
`UNSUPPORTED_FLOOR`. Clearance requires the explicit optional step field
`clearance_profile: "CLIPPED_ABOVE_SELECTED_FLOOR_V1"`; other values reject
admission. This profile keeps world-aligned grid indices, but each cell label
applies only to the part of that cell at or above its selected floor. The part
below the floor is never navigable; a cell fully below it has an empty navigable
part. This clipped synthetic evidence is declared by the new profile rather
than inferred from source triangles. `FREE` never grants access below a
selected floor or across the riser. The support-document hash binds the profile.

The lower floor is `origin.y` to the negative X side of the boundary; the upper
floor is `origin.y + height` to its positive X side. Each selected floor
triangle must lie exactly on one of those levels, remain on its own side of the
boundary, use upward winding and have vertices on the existing X/Z lattice.
Whole-tile support retains the existing one-triangle containment rule. At least
one row must have selected support on both tiles adjacent to the riser. Other
tiles may be unsupported; a selected triangle count alone never fills a floor.

The riser must consist of two unique source triangles covering the complete
rectangle at the boundary, from the lower to upper level and across the full Z
grid span. Its four corners, negative X winding, shared diagonal and source
dependencies are checked exactly. It is neither generated from occupancy nor
inferred from a depth front label. Empty, malformed, changed or foreign source
geometry fails admission. The canonical V4 stream records the sorted floor,
wall and riser references separately and includes the support-document hash.

V4 with that profile checks a conservative world-aligned box around each
complete vertical capsule sweep. Every touched X/Z tile must have selected source floor support;
the capsule bottom cannot pass below that tile's floor height. Every touched
Y cell must be explicitly `FREE`; occupied, unknown and out-of-grid space
block. This is deliberately conservative at diagonal paths and triangle seams.
Ordinary sweeps require both endpoints grounded at the same admitted height,
so they cannot cut through the riser. Static clearance is now queryable on a
selected landing, but unsupported tiles remain blocked.

`probe_step_transition` is a V4-only diagnostic certificate for one crossing.
Both endpoints must be grounded on opposite landings and farther than capsule
radius plus 0.01 from the riser. It checks five continuous swept envelopes:
approach to the riser gap, vertical lift on the low side, horizontal crossing
at upper-floor height, descent on the low side for the reverse direction, and
departure on the destination landing. Zero-length segments are checked too.
Each envelope uses the same floor, occupied and unknown checks. This path is
an existence certificate for a bounded synthetic crossing; it is not a
per-tick trajectory or proof that Jolt proposed the path. The controller
explicitly rejects V4 creation until it can realize and check a continuous
transition without skipping time, and shared CLI/native sessions therefore
remain disabled for steps. Mixed-slope transitions and native step replay are
still pending. The original `--step` fixture retains its bytes and geometry-only
behavior; `--step-clearance` writes an explicitly opted-in support document
and inspection request against the same source bundle. Neither diagnostic is
a walking trace. No measured collision or N2 result follows from these
synthetic checks.

`SyntheticCapsuleController::inspect_step_motion` performs a separate bounded
speculative replay of 1–600 exact 120 Hz Jolt updates. It requires a V4 shape,
the explicit clearance profile, a clear supported spawn and the existing
capsule dimension/source checks. Input velocity is horizontal, finite and at
most 10 synthetic units/s. It returns project-owned raw feet, per-tick
horizontal travel, Jolt's supported flag and both straight/transition probe
results. It uses the same private world, character settings and update routine
as the controller. It returns no live controller and commits no application
pose or clock. Rejected proposals continue only inside this speculative trace;
Jolt's supported flag never overrides the ledger. Cancellation/errors return
no partial vector and retain caller data.

The query copy of each raw pose normalizes only the existing 0.002 flat contact
rounding band; the returned raw pose is untouched. Both probes still reject
rounded-cap stair-corner contacts and short crossings outside their contracts.
The common update now sets Jolt's minimum forward stair distance to zero:
its former 0.02 minimum could exceed a tick's requested horizontal travel.
On the current 0.30-bounded fixture the resulting slow 0.3-unit/s ascent stalls
at the riser, while 3-unit/s proposals climb through corner poses that the
project probes reject. Neither outcome enables V4 sessions.

`we_collision_fixture NEW_DIRECTORY --step-proposals` retains the source and
support bytes from `--step-clearance` and adds `step-proposals.json`
(`SYNTHETIC_STEP_PROPOSALS_V1`) plus `proposals.wexplore`. The trace pins source,
support and canonical input and records raw 3-unit/s ascent/descent and
0.3-unit/s stalled ascent. Its walking/measured flags are false and
`proposal_only` is true. The native bundle adds small markers above raw feet
to a separate synthetic source copy; they never enter the collision cook.
Use `we_viewer --scene .../proposals.wexplore --source-view` to inspect it.
