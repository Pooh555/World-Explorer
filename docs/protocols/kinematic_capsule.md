# Synthetic kinematic capsule

`SyntheticCapsuleController` in `collision` consumes an immutable
[Jolt static shape](collision_cook.md) and its source-bound synthetic support
ledger. All Jolt world, body, shape, character and allocator types remain private.
This single-threaded diagnostic API does not enable measured walk mode, declare
real scale, or admit observation-backed collision. Dimensions are explicit
synthetic inputs: radius 0.05–1, height at least twice the radius plus 0.01 and
at most 3, in the synthetic aligned coordinate system. Creation requires an
entirely clear, supported capsule spawn on the ledger's declared surface.

`advance(current, horizontal_velocity, elapsed, stop)` validates the current
scene/epoch/revision/content and selected pose pins on every call, even with
zero elapsed time. A corrected source requires rebuilding from admitted current
support; the old controller cannot advance against it. Inputs must be finite,
horizontal, at most 10 units/s in magnitude, with elapsed nanoseconds in
[0, 60 seconds]. Invalid input, stale context, allocation/backend failure or
cancellation returns an error and preserves position, tick count and fractional
clock. The immutable shape and source scene remain usable under their original
context.

The clock accumulates integer nanoseconds multiplied by 120. Each 1,000,000,000
accumulated units schedules one fixed 1/120-second Jolt update. At most eight
ticks are scheduled per call. Excess whole ticks are reported as dropped;
the fractional remainder is retained and excess ticks never become future debt.
The result's step count and cumulative simulated ticks include held ticks when
space blocks motion. An early block holds position for the remaining scheduled
ticks rather than accumulating a later movement burst.

A private static-only Jolt world owns one body using exactly the selected
canonical triangles. `CharacterVirtual` proposes swept capsule movement and
sliding, with +Y up, one-sided source triangles, 45° maximum slope and 0.30
step-up/stick-down settings. Jolt supplies its own 0.005 padding; it is not
applied twice. Contact collection is bounded by Jolt's 256-hit limit and overflow
fails the whole call. No rigid-body simulation, worker pool or dynamic obstacles
are introduced.

Only the final proposed straight movement segment becomes project state, after
the existing conservative swept-envelope probe accepts it. Occupied, unknown,
out-of-grid and unsupported-floor envelopes block it even when no triangle was
selected there. A blocked call returns its clearance reason and retains the
last accepted tick position. V1/V2 remain flat:
proposed vertical displacement beyond 0.002 units is unsupported, while smaller
floor-contact rounding is normalized to the admitted plane.
[V3 inclined support](collision_ramps.md) supplies tangent vertical velocity,
proves plane-relative clearance and normalizes bounded contact rounding to the
capsule/plane contact height. General floor transitions, stairs, real
reconstruction and N2 qualification remain pending.

`we_collision_fixture NEW_DIRECTORY --capsule` is a development fixture tool.
It writes the existing V2 source/support/request, reloads their pinned bytes,
then cooks and simulates three one-second paths: wall sliding, unknown-column
blocking and unsupported-floor blocking. `capsule-trace.json` records
`SYNTHETIC_CAPSULE_TRACE_V1`, source/support/canonical hashes, explicit dimensions,
10 ms frame duration, each route's velocity and 100 feet/tick/clearance samples.
Clearance numbers are 0 clear diagnostic, 1 unknown, 2 occupied, 3 unsupported
floor. Both measured-collision and walking flags remain false. `paths.wexplore`
adds tiny synthetic mesh markers and an inspection camera to a separate,
unaligned diagnostic scene; those markers never enter the collision cook.
Use `we_viewer --scene .../paths.wexplore --source-view` to inspect the paths
beside the source floor and wall. Default and `--walls` fixture bytes are retained.

The [application session](capsule_session.md) owns interactive lifetime, pinned
loading, shared camera input, terminal stop/invalidation and CLI replay.
