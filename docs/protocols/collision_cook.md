# Canonical synthetic collision triangles

The `collision` library cooks source-bound, immutable triangle input from an
admitted [synthetic collision support](collision_support.md) and its exact scene
snapshot. V1 selects only explicit floor triangles. V2 additionally selects
explicit, admitted occupied-cell wall faces. It does not infer walls from
occupied cells or treat unknown cells as geometry. The support ledger continues
to block occupied, unknown, out-of-grid and unsupported-floor capsule envelopes.
A clear result is diagnostic only;
walking remains disabled. Real depth-space `FRONT_DIAGNOSTIC` labels are not
clearance, and real scenes cannot enter this cook.

`cook_synthetic_collision` requires matching scene ID, epoch, revision, complete
source hash, synthetic world-frame alignment and the admitted support document.
It rechecks every selected chunk's generation, pose revision, content hash and
triangle index before transforming its vertices into world coordinates. It
rejects nonfinite or degenerate triangles and cancellation. All construction is
private until a complete immutable owner is returned; failures leave the source,
support and any prior owner unchanged. `validate_source` and `probe` reject stale
scene/epoch/revision/content, including pose corrections, with resynchronization
required. The owner retains the support ledger for conservative diagnostic
probes; no shape or scene transaction is published.

The portable `WE_COLLISION_TRIANGLES_V1` byte stream is deterministic and has
no padding or host-ABI dependency. It contains, in order:

1. The ASCII magic above, without a terminator.
2. Six little-endian unsigned 64-bit integers: format version `1`, weld flag
   `0`, double-sided flag `0`, scene ID, epoch and scene revision.
3. The 64-byte lowercase ASCII source SHA-256 and 64-byte lowercase ASCII
   support-document SHA-256.
4. A little-endian unsigned 64-bit triangle count, bounded by the support
   admission limit of 256.
5. For each selected floor triangle, ordered by chunk ID then triangle index: four
   little-endian unsigned 64-bit values (chunk ID, generation, pose revision,
   triangle index), followed by three XYZ world vertices as nine little-endian
   IEEE-754 binary64 values. Signed zero is normalized to positive zero.

The owner exposes SHA-256 of these exact bytes. The stream binds cook parameters,
source content and support identity. A future Jolt cooked cache must separately
bind its Jolt version, CPU/ABI and these canonical bytes; this stream itself is
not a Jolt cache. V2 uses magic `WE_COLLISION_TRIANGLES_V2`, format version `2`
and the same header. It then writes the floor count and floor entries in V1
order, followed by a 64-bit wall count and wall entries in chunk/index order.
Each entry keeps the same four dependency integers and nine binary64 world
coordinates; the document hash binds cell/face selection. V1's stream is
unchanged. The [Jolt 5.3.0 adapter](../research/jolt_dependency.md)
builds an in-memory static mesh shape from all selected triangles and rejects
any triangle discarded by the backend. Its project-owned `JoltStaticCollision`
owner exposes a source-validated diagnostic segment ray cast and keeps Jolt
types private to `collision`. Construction and queries reject corrected or
cancelled contexts without replacing a prior owner. No Jolt shape is installed
in the source scene or viewer. The canonical input performs no vertex welding
or inferred face generation. The separate [synthetic kinematic controller](kinematic_capsule.md)
uses this shape and the support ledger for bounded flat-floor diagnostics.
There is no real metric floor or measured-walking permission.

[V1 validation](../validation/2026-10-21-collision-cook.md) covers canonical replay,
source correction, conservative barriers and native diagnostic viewing.
[V2 validation](../validation/2026-10-21-collision-wall.md) adds complete wall-face
selection, all four winding directions, Jolt rays and exact V1 byte preservation.
M3 remains in progress; Q5/N2 and A1's separate independent-scale requirement remain open.
