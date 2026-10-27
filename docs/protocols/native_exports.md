# Native textured PBR glTF export v1

[`native_export.hpp`](../../include/world_explorer/application/native_export.hpp)
adds `WE_NATIVE_TEXTURED_GLTF_V1` over the private [native asset handle](native_assets.md).
It exports actual admitted geometry and channels, without reinterpreting them as
generated-static v1 or captured-unlit GLB. A0/AP0 remain **IN_PROGRESS**. This is
bounded format conversion, not producer execution, physical relighting, runtime
adoption, ordinary Godot import or AP qualification.

## Source and conversion

Independent inputs select the native asset handle, original receipt, current
epoch/pose and seven finite [0,1] error limits. Receipt/context and canonical
instances are rechecked before conversion. The profile permits 1–2 definitions,
at most four materials total and 64 visible instances. Existing native adoption
already excludes metric units, LOD/colliders and room/support/lineage adoption.
Hidden nodes, captured-appearance conversion and per-instance material overrides
fail explicitly: every copy must retain the selected original instance's material
selection. The converter uses the original exact face-group assignments, without
inventing an alternate-material mapping. Source selection IDs remain in metadata.

Each definition becomes one glTF mesh with an indexed primitive per material
group. Vertices are shared across primitives; a manifest map retains every
primitive's original face indices when grouping changes triangle order. Pivoted
positions, rotated/normalized normals and tangents, unchanged tangent.w and UV0
become little-endian float32, interleaved at stride 48. Indices are uint32.
Actual converted bounds populate accessors. Converted triangle/UV collapse,
nonfinite values, winding disagreement and tangent orthogonality loss reject.
Full UV/tangent arrays are mandatory. Positive uniform TRS is retained; wxyz is
converted explicitly to xyzw. Coordinate/translation magnitudes are limited to
1e6, scale to [1e-4,1e4] and quaternion squared-length error to 1e-12.

The following choices follow the normative [glTF 2.0 material/texture conventions](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials):
base color and emission use sRGB-encoded RGB; packed occlusion/roughness/metallic
use linear R/G/B respectively. Normal maps use linear signed XYZ encoded into
RGB, with positive tangent Z and normalization after decoding. UV origin remains
upper left; neither UVs nor normal Y are flipped. Sampler modes retain native
NEAREST/LINEAR and CLAMP_TO_EDGE/REPEAT, without mipmaps. Alpha is opaque; materials
are double-sided. Core emission factors remain bounded; HDR emission is refused.

Four RGBA8 PNGs per material contain base color, ORM, normal and emission, with
unused alpha 255. Source float channels are rounded to nearest 8-bit values;
color uses the piecewise sRGB transfer function. The existing internal PNG codec
is reused unchanged. No texture resizing, mip generation, compression extension,
geometry simplification, material refit or fit-light export occurs.

Every definition records measured conversion errors: maximum vertex world-position
Euclidean distance using original double TRS; local normal/tangent Euclidean
distance including normalization; UV component error; decoded linear-color and
numeric component errors; and Euclidean normal-map error after normalized decode.
Each must meet its independently selected limit. This does not certify engine
float-transform precision, filtering behavior or perceptual/physical quality.
Occlusion remains a separate unqualified source channel; movement requires
occlusion revalidation. One arbitrary unit maps to one nominal glTF metre,
without measured scale or walking authority. Native grids remain at most 256²;
this profile does not satisfy AP5's 2K texture target.

## Shared resources and authoritative sidecars

| Path | Content |
|---|---|
| `assets/ID/asset.gltf` | Local identity node and one canonical definition mesh/material set |
| `assets/ID/geometry.bin` | Pivoted vertex attributes and material-group indices |
| `assets/ID/material-VARIANT-{base,orm,normal,emission}.png` | Explicit role-specific channel encoding |
| `environment.gltf` | Scene nodes referencing the same per-definition buffers/PNGs |
| `catalog.json`, `scene.json` | Exact canonical admitted definitions and instance snapshot |
| `provenance/PRODUCT-REV.json` | Exact selected face, material-root and channel provenance bytes |
| `rights/ID.json` | Original rights binding, UNREVIEWED_BINDING_ONLY; opaque text omitted |
| `manifest.json` | Original receipt/context/scene identity, asset/material/channel/face/node maps, conversion losses/limits/errors and full file hashes |

Individual and assembled glTF documents reference the **same physical** per-asset
buffers and images. Assembly only remaps document-local indices and contained
relative URIs. It creates one mesh per semantic definition, with all copies
sharing that mesh; identical bytes never merge semantic IDs. Catalog revisions
remain exact definition snapshots, without a mutable global catalog-head claim.
The manifest, rather than importer-dependent extras, owns semantic identity and
provenance. Every file except the manifest appears in its hash/size inventory;
the admitted handle's manifest SHA identifies the complete package.

No source images, original media, opaque rights/prompt/library text or arbitrary
authoring documents are copied. Strict admitted region records contain source
identity hashes, not image pixels. Synthetic sources are labeled in every glTF
document and the manifest. All outputs retain `UNQUALIFIED_EXPORT` and explicit
validator/importer `NOT_RUN`: later external observations do not rewrite the
package or authenticate a tool run. Optional GLB compilation is not implemented
by this profile; existing GLB profiles are unchanged.

## Admission, publication and interruption

`prepare_native_export` proposes files. `inspect_native_export` first bounds the
untrusted selection, independently rebuilds the package and compares the exact
closed path/byte set. File-vector ordering is irrelevant; duplicate, unsafe,
missing, altered or additional paths fail. This is not a general third-party
glTF/PNG importer. At most 64 files/8 MiB are accepted, at most 1 MiB per file;
copied provenance is additionally bounded to 256 KiB. Budget failures expose no
partial admitted handle and preserve caller input.

`publish_native_export` rechecks receipt/context, protects the original selected
source tree and uses existing [immutable-record transactions](asset_metadata_io.md).
Keys hash manifest identity plus relative path. Cancellation retains the previous
manifest and any owned intent/blobs; exact retries recover/replay. Reload requires
fresh independent inputs, selected current store commit and every package file.
Publication copies only the admitted package, not its source receipt.

`write_native_export` materializes a new directory outside the input tree. Existing
destinations reject. Data files precede terminal `manifest.json`; interruption can
leave an incomplete new directory, which must not be consumed. There is no
ordinary-directory resume or new arbitrary power-loss/disk-full/concurrent-writer
qualification. Native PBR rendering must still use the existing immutable scene
update path; real A1 viewing and Godot evaluation remain required.

The separate [native PBR scene compiler](native_pbr_scene.md) now consumes admitted
textured exports for bounded immutable runtime resources and diagnostic native
viewing. That integration does not qualify producers, real A1 output or AP gates.

An explicitly selected [native structure binding](native_structure.md) now adds
manifest schema 2 / `WE_NATIVE_STRUCTURED_GLTF_V1` with exact graph/binding sidecars.
Core mesh/material resources retain this conversion profile; legacy schema-1
exports remain byte compatible. Native PBR loading preserves the structured
source pins without adding physical-support or real AP qualification.
