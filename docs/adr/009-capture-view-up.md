# 009 — Capture orientation as an explicit viewing reference

Date: 2026-10-13, Asia/Hong_Kong. Status: accepted for the user-requested tilt fix.

The original video's fitted scene uses an arbitrary monocular world frame. Its
recorded capture-up vector differs from canonical +Y by 10.845°. Removing roll
relative to +Y made the source reset visibly tilted. No IMU or measured floor
alignment is available; rotating fitted assets or declaring measured gravity
would require additional pose/SH/dependency work and evidence.

Keep canonical world coordinates, map poses, Gaussians, SH and gravity provenance
unchanged. Source inspection metadata v2 stores the recorded optical camera-up
vector after the existing backend conversion. **Source view** / `--source-view`
explicitly selects that viewing reference. Free navigation, Fit and bookmarks
retain it; Space/Ctrl follow this chosen viewing reference in that mode. The UI
labels it as capture orientation with gravity unmeasured. This is a presentation
option, not a new world frame or camera tracking.

Default loading without a source reset retains +Y world-up and locked roll,
preserving the specification's default controls. Legacy v1 metadata also retains
that behavior. Reject invalid unit/orthogonality/profile metadata. Revisit when
independent gravity or user-approved floor alignment is implemented; such an
alignment must update poses and all dependent appearance/geometry/collision
transactionally rather than reusing this viewing hint as measurement.
