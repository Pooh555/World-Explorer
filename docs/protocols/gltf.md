# Captured-unlit GLB interchange

M3 exports complete baked mesh snapshots as self-contained glTF 2.0/GLB and imports
this producer's restricted `WE_CAPTURED_GLB_V1` profile into native bundles.
This is captured appearance, not intrinsic albedo, PBR recovery, measured geometry
or asset-track qualification. General glTF editing/import and engine packaging
remain separate. [Actual local checks](../validation/2026-10-19-mesh-glb.md).

```sh
build/dev/we_process export-glb SOURCE.wexplore NEW_DIRECTORY
# Read NEW_DIRECTORY/manifest.json for the explicit scene.glb SHA-256.
build/dev/we_process import-glb NEW_DIRECTORY/scene.glb GLB_SHA256 NEW_NATIVE.wexplore
build/dev/we_viewer --scene NEW_NATIVE.wexplore
```

The application commands are shared APIs; ordinary Open scene consumes the new
native bundle. GUI GLB job controls are not implemented. Export accepts original
[native baked appearance](mesh_color.md#bounded-native-baked-appearance) only.
Stale geometry, splats/hybrid scenes, untextured chunks, measured-scale scenes,
empty scenes and re-export of lossy imported previews reject without trimming.
Retain the original native bake for subsequent exports.

## Portable representation and declared losses

The file requires [KHR_materials_unlit](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_unlit/README.md).
`baked_appearance` uses an embedded nonpremultiplied RGBA8 PNG, `LINEAR`
minification/magnification, `CLAMP_TO_EDGE`, no mipmaps, double-sided triangles and
`alphaMode: MASK` / `alphaCutoff: 1.0`. No lighting/material inference is performed;
metallic/roughness defaults are unused by the required unlit extension.
These semantics follow the [glTF specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html).

Linear captured RGB is converted to nearest sRGB8 codes; alpha remains exactly
0/255. Unknown texels retain zero RGB. Each image dimension is padded upward to a
power of two with transparent zero texels. This makes normalized half-integer UVs
exact float32 binary fractions, without rescaling any source texel. Texture origin
is top left, with no vertical flip. All source triangles remain in original order;
standard render vertices are expanded at UV seams. Source vertices, including
unused ones, original normals and shared index topology are retained separately
for exact native reconstruction. Unlit render attributes omit normals.

Standard glTF bilinear filtering differs from the original triangular lattice
filter. The native portable preview requires **every positive-weight bilinear
corner** to be valid; otherwise it discards the fragment. This conservatively
interprets cutoff one. It can hide supported surface edges and alter colors;
floating implementations may also differ at mask boundaries. It adds no inferred
color or geometric hole filling. Coverage/color conversion losses must be measured
separately from original native baking; shared lattice samples alone do not prove
continuous export fidelity. The viewer labels the portable preview and its losses.

Coordinates stay right-handed, +Y up. Nodes preserve uniform positive scale,
translation and rotation, explicitly converting wxyz to glTF xyzw. The declared
mapping is one arbitrary world unit to one **nominal glTF metre**, uncalibrated;
it establishes no measured scale. Imported scenes stay `ARBITRARY`, with no
collision or walking eligibility. No source inspection camera is exported.

## Bounded file and source contract

GLB is little-endian, version 2, exactly one aligned JSON chunk followed by one
aligned BIN chunk. The profile caps the file at 256 MiB, JSON at 16 MiB, nesting at
24, nodes at 4,096, aggregate original vertices at 4M and native texture texels at
8M. Existing per-mesh 100k triangle / 18-view limits remain. Individual padded
PNGs are at most 4096×4096. Excess fails instead of dropping data.

Every chunk uses five contiguous, four-byte-aligned BIN views:

| View | Contents |
|---|---|
| Render | Nonindexed triangle corners, float32 xyz and normalized uv, stride 20; POSITION bounds and two checked accessors |
| Original vertices | Six float32 per source vertex: xyz and original normal |
| Original indices | Three uint32 per original source triangle |
| Image | Embedded PNG with exact declared byte length |
| Ownership | One uint64 owner plus uint32 ordered-source candidate mask per native texel |

Node extras preserve bounds, original chunk ID/generation/pose revision, source
frame IDs, source/cleanup pins, exact authoring documents and original bake metadata.
Large identities use decimal strings, preserving all uint64 bits through JSON
consumers. Root extras pin the source scene and identity and declare the profile,
unit mapping, synthetic provenance and content inventory. Source snapshot admission
checks dependencies and exact text hashes; it does not replay fusion/cleanup or
prove original photometric accuracy.

The export adds captured textures and retains authoring documents verbatim. It
adds no original video/frames, render cameras or location fields, and omits the
arbitrary scene-provenance map. Inventory fields are `source_images_added: false`,
`location_fields_added: false`, `authoring_documents_included: true` and
`authoring_metadata_review: UNINSPECTED_RETAINED_DOCUMENTS`. Retained documents may
contain local paths or metadata; this profile makes **no sanitization claim**.
All content stays local unless the user separately shares it.

The project-owned PNG codec writes the [PNG RGBA8 format](https://www.w3.org/TR/2003/REC-PNG-20031110/)
with row filter zero, a zlib stream of stored deflate blocks, CRC-32 and Adler-32.
The importer accepts exactly this chunk/filter/compression profile, not arbitrary
PNG recompression. Independent standard decoders can read the exported PNGs.

Import rejects unknown fields/features, duplicate keys, raw NUL, external URIs,
wrong counts/strides, overlapping/gapped views, trailing/nonzero padding, invalid
transforms, malformed PNGs, nonbinary validity and inconsistent ownership/source
pins. Actual render positions and UVs must match retained geometry exactly.
No external resource is opened. Alpha/RGB ownership and metadata receive ordinary
scene admission before publication. A supplied file SHA-256 is required by CLI
import; checksums are integrity evidence, not authorship or quality certification.

## Native compatibility and publication

The current GLB profile accepts legacy single-cache appearances only. Native
sequence appearances use a wider candidate plane and bundle schema 4; export
returns unsupported before encoding rather than truncating their evidence.

Imported appearances have metadata schema 2 / `CAPTURED_UNLIT_ATLAS_PNG_V1`,
`bilinear_valid_only`, `score_squared_convex_linear_rgb_then_srgb8` and an original
`parent_chunk_sha256`. RGB must match an sRGB8-decoded float within one ULP for
library rounding. The native source geometry hash stays unchanged. The derived
material uses content-hash prefix 4; original native baked prefix 3 and legacy
raw/splat hashes stay unchanged. Scene hashes differ because appearance and
provenance differ.

[Bundle schema 3](scene.md#baked-appearance-bundle-schema-3) keeps its binary layout;
its reader derives the portable-parent field from exact authoring JSON and validates
the complete metadata family. Earlier readers reject the unsupported family.
The owned parent string counts toward decoded budgets. The renderer privately
encodes the bilinear flag in its atlas-height word; public contracts expose no GPU
objects. The original native triangular path remains available and unchanged.

Export publishes `scene.glb` first, then an exclusive terminal `manifest.json`
with exact bytes/SHA-256 and source/content inventory. Destinations must be new
and outside the immutable source bundle. Import validates the entire owned GLB
before writing a new native bundle through the existing atomic writer. Both
commands check cancellation before publication; publication failure/cancellation
can leave an incomplete new directory. Consume exports only with a complete
manifest. Old files/readers remain unchanged. Production job checkpoint/resume,
kill/crash qualification and general engine import remain open.
