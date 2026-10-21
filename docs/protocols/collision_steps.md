# Selected synthetic step geometry (admission slice)

`SYNTHETIC_COLLISION_SUPPORT_V4` admits one source-bound, positive X step for
synthetic diagnostics. It extends V2's required fields with `step` and permits an
empty `walls` array only. `step` contains `boundary_x` (an interior X grid line),
`height` (finite, greater than zero and at most 0.30 synthetic units), and a
`riser` source reference. The riser reference has the same chunk generation,
pose revision and content hash pins as a selected floor, plus exactly two
triangle indices. V1–V3 documents and canonical encodings are unchanged.
The explicit V2 world-aligned cell labels remain diagnostic; they do not prove
clearance above the upper landing or below any floor.

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

This slice admits geometry only. The V4 clearance probe returns
`UNSUPPORTED_FLOOR` even for a static spawn, so shared CLI/native capsule
sessions cannot walk or step on it. A future transition profile must prove the
actual lift, horizontal crossing, descent, full capsule envelope, corners and
unknown blocking before the controller accepts V4. The `--step` fixture saves
a source bundle, document and inspection request for the selected geometry;
its diagnostic is not a walking trace. All values are synthetic, and no
measured collision or N2 result follows from admission.
