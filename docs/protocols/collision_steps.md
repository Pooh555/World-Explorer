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
integration below follows its checked legs with retained phase and a bounded
clock. Mixed-slope transitions remain unsupported. The original `--step` fixture retains its bytes and geometry-only
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

`probe_step_motion` is an additional V4 straight-sweep proof; the existing
ordinary and five-segment queries keep their original meanings. It requires
the same explicit clearance profile and the controller's bounded dimensions
(radius 0.05–1, height at least `2*radius+0.01` and at most 3). It validates the
complete source context and does not grant a clock, velocity or controller
capability.

Let `L`/`H` be the admitted lower/upper floor, `b` the riser X and `r` the
capsule radius. The selected step solid is the union of `y <= L` and
`x >= b, y <= H`. The lowest capsule-core center is `(x, feet.y+r, z)`;
higher core centers cannot be closer to either downward solid. Thus the entire
straight sweep must have `feet.y >= L` and
`max(b-x,0)^2 + max(feet.y+r-H,0)^2 >= r^2` for every segment parameter.
The implementation bounds this expression with outward-rounded interval
operations and bisects unresolved parameter intervals. Maximum depth is 16,
with 512 visited intervals; an unresolved proof returns `UNSUPPORTED_FLOOR`.
Exact upper-floor contact is handled directly when both feet heights are at
least `H`. No penetration tolerance or Jolt padding is added to this geometry
test. Exact tangencies that interval rounding cannot certify remain blocked.

Each endpoint must also be within the existing synthetic 0.01 contact band:
near the lower floor, or above the upper edge with core-to-solid distance at
most `r+0.01`. This does not certify continuous grounding or stair timing.
Every touched projected tile still needs selected whole-tile floor support.
Grid bounds are rounded outward. Each candidate cell is clipped at that tile's
floor; an empty clipped cell contributes no traversable space. Unknown and
occupied cells block unless the distance from an enclosing swept-core box to
the clipped cell proves the radius cannot touch it. These cell checks remain
conservative at diagonal paths and seams. Source changes, invalid dimensions
and cancellation return errors without modifying inputs.

`CanonicalCollisionMesh::probe_motion` exposes that proof through the same
immutable source owner. `we_collision_fixture NEW_DIRECTORY --step-motion`
adds `step-motion.json` (`SYNTHETIC_STEP_MOTION_V1`) and `motion.wexplore`.
The trace retains the raw proposal and original probe fields, adds motion
clearance/cell counts, and includes an additional 10-unit/s ascent. It uses
the same explicit 0.002 floor-contact normalization for query copies only.
Markers now distinguish the new motion-probe result; the raw rows remain
speculative, with false walking/measured flags. Previous fixture modes and
their serialized outputs are retained. The raw proposal trace remains independent of the certified controller path
below; rejected proposal chords retain their original probe results.

## Owned transition cursor

`certify_step_transition` returns the existing probe result and, only when all
five complete segments are clear, their six ordered waypoints. Blocked results
carry no path; invalid, stale or cancelled inputs return errors. The old
`probe_step_transition` delegates to this same proof and returns only its probe.
`CanonicalCollisionMesh::certify_transition` preserves the immutable cook's
source checks. No document format, selected geometry or clearance rule changes.

`SyntheticStepTransition` in `collision/kinematic_capsule.hpp` owns that cook
and a private copy of the certified path. Creation requires controller-bounded
capsule dimensions and a complete certificate; unsupported, occupied, unknown,
short-crossing and legacy geometry-only paths cannot create a cursor. Copying
an admitted cursor produces an independent checkpoint of its phase and offset.
Every `advance_tick` validates the current source, including while paused or
finished. Cancellation, invalid speed and source changes preserve all cursor
state. Retained certificates cannot authorize a different source revision.

Each explicit call represents one 1/120-second path tick. Signed finite path
speed is limited to 10 synthetic units/s. Positive speed follows the path;
negative speed retraces it; zero holds the current phase. The budget bounds
**total three-dimensional travel**, including lift/drop, rather than only
horizontal displacement. A tick traverses at most one nonzero leg. Unused
budget at a corner is discarded; zero-length legs require no displacement.
The returned `CapsuleSweep` is the actual single-leg subsegment, never a chord
joining two different legs. Internal points remain inside their certified
segment envelope; exact waypoint coordinates are restored at leg boundaries.
An endpoint holds on continued input and can be retraced with opposite input.

This cursor permits intermediate airborne poses only through the complete
landing-to-landing certificate. Those poses need not pass the ordinary motion
probe's endpoint-contact rule. The cursor itself does not reclassify rejected
raw Jolt proposals, prove continuous grounding, run Jolt or schedule elapsed time. The controller owns
the elapsed-time integration below.

## Controller integration

`select_step_transition` and `CanonicalCollisionMesh::select_transition` propose
an opposite landing when one tick's horizontal intent reaches the radius-plus-
0.01 riser margin from a grounded point outside that margin. Selection validates
the source and inputs but grants no clearance. The proposed destination is the
opposite margin at its admitted floor height, with the starting Z coordinate.
No route starts at an unsupported rounded-cap pose or uses raw Jolt corner
proposals to infer a landing. The existing complete-path certificate must pass
before an owned cursor starts. Occupied, unknown or unsupported certificates
hold the last accepted point and consume scheduled time with their clearance
reason, preserving the existing eight-tick cap and dropped-time behavior.

V4 controller creation now uses the ordinary supported-spawn check; geometry-
only legacy V4 and short unsupported spawns remain blocked. Away from a
transition, Jolt proposes ordinary same-landing movement and the original probe
checks it. During a crossing the cursor uses signed `velocity.x` along the
selected path and holds Z; pure lateral or zero input holds phase. Reverse X
retraces the same certified legs. Each tick bounds full 3D travel and cannot
join different legs with a shortcut chord. Jolt position/contact state is
synchronized to each accepted point and contact-budget overflow fails the call.
These transition sweeps are owned by the geometric certificate, not inferred
from Jolt's support flag. Stick-down is not applied to a retained lift/drop.

A speculative copy retains the cursor, its initial point and direction across
calls. Pose, ticks, fractional clock and cursor commit together. Failure or
cancellation preserves all four; source changes fail even with zero elapsed
time. Finishing or retracing to the original landing releases the certificate.
Same-landing height normalization uses the last accepted tick, allowing movement
after landing within one multi-tick call. A rendered frame may contain several
certified ticks; its endpoint-to-endpoint chord is not a new sweep certificate.
Single-plane V3 remains separate. Mixed inclined/step source geometry and
arbitrary surface transitions remain unsupported.

`we_collision_fixture NEW_DIRECTORY --step-session` retains the explicit
step source/support/request and adds pinned `session-up.json`, `session-down.json`,
`session-slow.json`, `session-short.json` and `session-corner.json`. The corner
request uses a separate occupied upper-side cell ledger; the short spawn is
intentionally unsupported. Use the ordinary `capsule-session` CLI and
`--capsule-request` viewer paths. All outputs remain synthetic with false
measured-collision/walking flags. These APIs do not admit real observed collision
or advance Q5/N2.
