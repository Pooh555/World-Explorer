# Application-owned synthetic capsule session

`SyntheticCapsuleSession` owns the [synthetic controller](kinematic_capsule.md)
behind an application interface shared by viewer and CLI. Its public types are
project-owned scene, camera, input and clearance values; Jolt stays private to
`collision`. The application may privately depend on `collision`. The existing
inspection and session commands share bounded request parsing, native bundle
loading and support admission. Neither command changes the source scene.

`open(request, sha256, stop)` accepts exactly:

```json
{
  "kind": "SYNTHETIC_CAPSULE_SESSION_V1",
  "source_bundle": "source.wexplore",
  "source_manifest_sha256": "<64-character SHA-256>",
  "support": "support.json",
  "support_sha256": "<64-character SHA-256>",
  "capsule": {
    "feet": [2.5, 0, 1.3],
    "radius": 0.25,
    "height": 1.7,
    "eye_height": 1.6,
    "speed": 3,
    "yaw_radians": 1.5707963267948966
  }
}
```

The caller pins the complete request bytes. Input paths resolve relative to the
request; source manifest and support pins are checked before cooking. Requests
are bounded to 1 MiB/eight nesting levels, with duplicate keys, raw NUL, extra
fields, nonnumeric scalars and nonfinite values rejected. Controller dimensions
and supported spawn constraints still apply. Eye height must be positive and
below capsule height; speed is 0.01–10 synthetic units/s; yaw is within ±π.
These explicit diagnostic values never establish real scale or gravity.
A failed open leaves an existing session and every input byte intact.

`advance(current, FlyInput, nanoseconds, stop)` validates the supplied current
snapshot on every active call. It stages camera look and controller movement,
then commits both together. Input forward/right are finite values within ±1;
mouse deltas are bounded to ±1,000,000. Vertical keys and wheel have no capsule
effect. Forward/right motion follows yaw on the +Y-up horizontal plane and is
normalized diagonally. Shift multiplies the requested speed by four, capped at
10. Eye position is accepted feet plus the explicit eye height; pitch cannot
move the capsule vertically. The controller retains its exact 120 Hz/eight-tick
clock and conservative space checks.

Uncaptured mouse, focus loss or text/dialog input pauses motion and the clock,
retaining only the existing fractional tick, without later catch-up. Invalid
frame input preserves pose, look and clock. Cancellation/explicit stop transitions
`ACTIVE_SYNTHETIC` to terminal `STOPPED`. Source/epoch/pose corrections and other
controller errors transition to terminal `INVALIDATED`; even the old source
cannot reactivate that owner. Those failures preserve the last accepted pose.
A fresh pinned session is required to continue. `stop()` never clears invalidation.

Start the viewer with `--capsule-request FILE --capsule-request-sha SHA256`.
It accepts one startup source and performs admission before creating its native
window. The source uses the ordinary scene update/upload path. The toolbar,
status and controls explicitly say synthetic; the measured walk control remains
unavailable. Capture the viewport for input, Escape to pause and **Stop capsule**
to return to Fly. Fit, source reset and bookmark restoration also stop the session.
An accepted replacement scene is checked immediately and invalidates a mismatched
session before further movement. Upload/owned-job work pauses capsule input.
Optional bounded `--input-trace` rows add status, feet, ticks, dropped ticks,
clearance and frame nanoseconds for local diagnostics; this is not timing qualification.

`we_process capsule-session REQUEST SHA256 FORWARD RIGHT FRAME_NS FRAMES`
runs 1–600 deterministic frames through the same session command and prints
`SYNTHETIC_CAPSULE_REPLAY_V1`: request/source pins, per-frame feet/ticks/drops/
clearance and false measured-collision/walking flags. It writes no assets.
`we_collision_fixture NEW_DIRECTORY --capsule` now also emits `session-wall.json`,
`session-unknown.json` and `session-unsupported.json`, bound to the original inputs.
[V3 inclined support](collision_ramps.md) now admits a single source-proven
plane through this same workflow. Step transitions, measured reconstruction,
Q5/N2 and platform qualification remain unverified.
