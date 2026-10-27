# Diagnostic native static shapes

The [neutral input owner](../../include/world_explorer/scene_store/native_collider_input.hpp)
and [collision adapter](../../include/world_explorer/collision/native_static_shape.hpp)
consume the [existing portable collider input](native_colliders.md). The application
bridge prepares that input from a privately admitted native collider. Collision
depends on scene storage; application-owned types and Jolt headers stay outside
its public interface. This is static synthetic gameplay inspection in arbitrary
units, with no body, simulation, support ledger, walking token or engine package.

## Input and source selection

`decode_native_collider_input` requires independently selected epoch, pose,
collider/base product IDs, revisions and root hashes, receipt SHA and canonical
input SHA. It checks the unchanged V1 stream, exact length/header/checksum, the
1 MiB /8,192 vertex and face limits, finite bounded positions, canonical positive
zero, valid indices, used vertices, a complete source-face permutation and
nondegenerate, nonduplicate triangles. Unsupported flags refuse. The immutable
owner copies bytes and arrays; failures/cancellation preserve the caller's input.

Those pins provide integrity. A standalone decoded stream does not establish
receipt membership, base correspondence, provenance, rights or source truth;
`prepare_native_collider_input` preserves the existing application admission
authority. Build and every query compare the selected complete binding with the
owner, returning `RESYNC_REQUIRED` on any difference. The caller must supply the
current selection rather than replay the old binding after a source correction.

## Backend construction and loss

`build_native_static_collision` uses the existing registered Jolt runtime and
verifies binary/header version configuration. The selected policy independently
bounds maximum Euclidean local vertex displacement and world ray-endpoint
conversion error, defaulting to `1e-4` each. Both must be finite in [0, 1]. There
is no automatic relaxation. Local positions remain bounded by ±1,000,000 units.

The adapter retains indexed input and per-triangle source IDs, refuses sanitizer
count loss, then checks the actual shape's triangle count and enumerates its
decoded compressed triangles. A bounded conservative search establishes an
oriented bijection to the original triangles within the selected displacement.
It checks cancellation between candidates/batches; ambiguous near-coincident
geometry may refuse. Worst-case matching is quadratic, bounded by 8,192 faces.
The retained diagnostic triangle table follows input face order and cyclic
corner order, with original base face IDs. It is a geometric matching table;
ray hit IDs separately use Jolt's retained per-triangle user data.

The maximum matched vertex displacement bounds the affine triangle surfaces by
the same amount. It does not certify clearance, topology after quantization,
metric accuracy, contact stability, convex decomposition or AP8 performance.
Independent queries below exercise hole retention and original face mapping.

The report records backend version/configuration/version ID, an adapter source
hash, decoded geometry SHA, selected policy and observed local displacement.
`m_build_sha256` hashes the exact canonical input SHA, backend/configuration,
adapter source hash, decoded geometry SHA and both independent policy values.
CMake refreshes the adapter hash when its listed source/header inputs change.
The pinned development archive remains owned by `cmake/dependencies.cmake`;
the report does not assert that another dependency profile used that archive.
These identities describe diagnostic construction, without serializing or
certifying a portable Jolt cache or binary/toolchain reproducibility.

Hash encoding uses little-endian uint64 words and binary64 numbers (signed zero
normalized); text is a uint64 byte length plus UTF-8 bytes. Geometry identity is
the text tag `WE_NATIVE_JOLT_DECODED_TRIANGLES_V1`, triangle count, then each
input-ordered match's source-face word and nine XYZ numbers. Build identity is
the text tag `WE_NATIVE_JOLT_DIAGNOSTIC_BUILD_V1`, then four texts (canonical
input SHA, backend string, adapter source SHA, geometry SHA), followed by the
local-displacement and world-endpoint policy numbers, in that order.

## Queries and placement ownership

One immutable local shape serves multiple explicitly supplied instance frames.
Every `ray_cast` receives a fresh `world_from_base`; it caches no placement and
admits no scene-instance identity or revision. The application remains responsible
for selecting a current frame and composing its compensated asset pivot. No
instance is installed into the viewer or measured controller through this API.

The frame must be finite, positive uniform scale in [1e-6, 1e6], with a unit
quaternion squared-norm residual ≤1e-12. The segment must be finite and nonzero.
The adapter inverts the frame, converts endpoints to local float32 and checks
the actual COM-relative origin/direction's roundtrip endpoints against the
independent **world-unit** bound. Collapsed, out-of-range or lossy queries refuse.
Jolt's explicit `IgnoreBackFaces` collector honors the portable single-sided
policy; its shortest ray API would test both sides.

Results contain optional fraction/original base face/point, observed endpoint
error and local surface error multiplied by this instance scale. The point
interpolates the original world segment at the backend fraction. These error
reports are separate diagnostics, without an overall hit-distance or clearance
certificate. Stale input, cancellation and backend failure preserve the existing
owner. Existing M3 support-ledger/controller admission and all Q5/N2 walking
barriers remain unchanged.
