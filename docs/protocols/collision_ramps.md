# Source-bound inclined synthetic support

`SYNTHETIC_COLLISION_SUPPORT_V3` extends the [V1/V2 support contract](collision_support.md)
with one inclined plane. It is a synthetic development profile, not admission of
observed-depth or real measured collision. Every floor and wall still selects
explicit source triangles with source/epoch/revision/frame/chunk/pose/hash pins.
V1/V2 encodings, flat queries and canonical bytes retain their original meanings.

V3 has the V2 fields plus mandatory `plane: {"rise_x": gx, "rise_z": gz}`.
Both values must be finite numeric data with magnitude at most 4. `walls` may
be empty; other bounds, exact field sets and budgets remain unchanged. The
plane is `H(x,z) = origin.y + gx*(x-origin.x) + gz*(z-origin.z)` in aligned world
coordinates. Every selected floor vertex must satisfy `y == H(x,z)` exactly,
and X/Z must round-trip exactly to the bounded integer grid lattice. Upward
geometric winding and whole-tile containment in one selected triangle are still
required. No tolerance closes a gap or joins a triangle seam. Different planes,
steps and general height fields are outside V3.

A V3 cell `(i,j,k)` is the explicit plane-relative prism obtained from
`x = origin.x + u*cell_size`, `z = origin.z + w*cell_size`,
`y = H(x,z) + v*cell_size`, for `u in [i,i+1]`, `v in [j,j+1]`,
`w in [k,k+1]`. Thus `.` declares that whole prism free, `#` occupied and `?`
unknown. Below the plane is never declared free; outside the grid remains
unknown. These labels are manually supplied synthetic evidence, not inferred
from a mesh or transformed from real depth-space diagnostics.

A selected wall face still covers one occupied cell's complete NEG/POS X/Z
face with two unique outward triangles. Its four expected corners now use the
plane height at each X/Z corner plus the cell's relative lower/upper Y. Coverage,
shared diagonal and exact position checks retain the V2 proof. Occupied labels
alone never generate geometry. Plane gradients with `gx*gx+gz*gz > 1` retain
selected collision geometry but grant no supported tiles or capsule spawn:
this is the strict 45° maximum, with no angle tolerance.

The clearance envelope encloses a true vertical capsule swept along the entire
straight feet segment. Let `G = sqrt(1+gx*gx+gz*gz)`, radius `r`, height `h` and
`q = feet.y-H(feet.x,feet.z)`. Under the prism coordinate transform, capsule Y
extrema are `q-r*(G-1)` and `q+h+r*(G-1)`. Segment extrema occur at its endpoints;
X/Z remain the ordinary swept capsule bounds. The probe checks the complete
outward-rounded grid-space box, including every touched cell and floor tile.
Its bottom must be at least zero, and each endpoint's bottom at most 0.01
synthetic units above the plane. This near-contact band is not a real clearance
or accuracy claim. It prevents arbitrary airborne queries from acquiring floor
support. The transformed box remains deliberately conservative at corners.

The private Jolt controller supplies the tangent vertical velocity
`gx*vx+gz*vz`, while application input remains horizontal. V3 uses a Jolt
downward support sweep after movement instead of free-fall integration: this
keeps idle feet still and settles wall-blocked tangent input onto the plane.
Contact-budget overflow from either query fails the entire call. V1/V2 retain
their existing gravity behavior. It checks each proposed result against V3 support. Only contact
rounding within the existing 0.002 bound may be normalized to
`H(x,z)+(r+padding)*(G-1)+1e-6`, using Jolt's existing 0.005 padding once.
This keeps the capsule's lower cap above the plane and accounts for the
lowest-point/contact-point difference on slopes. Larger vertical discrepancies
block movement. Source validation, cancellation, transactional state/clock,
120 Hz/eight-tick limits and terminal application-session invalidation are retained.
The configured 0.30 step limit has no step-transition qualification yet.

Canonical V3 uses `WE_COLLISION_TRIANGLES_V3` and format version 3, followed by
the same header and floor-count/records/wall-count/records layout as V2, including
an explicit zero wall count when empty. The support-document hash binds the
plane and prism labels. Jolt receives only the selected world-space triangles.

`we_collision_fixture NEW_DIRECTORY --ramp`, `--ramp-45` or `--ramp-steep`
produces a source plane with X rise 0.5, 1 or `1+1/1024`, explicit occupied face
and unknown column, inspection request and shared wall/unknown/unsupported session
requests. Use the ordinary `capsule-session` CLI and `--capsule-request` viewer
entry points. The inspection view places diagnostic samples in the tilted
prisms; these samples are not volume boundaries. All output remains visibly
synthetic, and measured walking stays disabled.
