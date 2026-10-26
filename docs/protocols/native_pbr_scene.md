# Native PBR draft scenes

`compile_native_pbr_scene` consumes the private
[admitted native textured export](native_exports.md), rechecks receipt/context and
compiles immutable `SceneSnapshot` resources. This bounded A0 integration supports
local diagnostic display; it does not execute asset producers or qualify A1,
material recovery, physical relighting, rights or ordinary Godot import.

Each definition becomes one mesh chunk with shared geometry, material images and
stable instance IDs. The first source instance defines the chunk frame; other
positive uniform transforms are relative to it. Bounds cover all instances.
Source definition/material references, export manifest/hash, individual glTF and
canonical instance document remain pinned in each chunk. Validation reconstructs
converted geometry and PNG hashes, complete face groups, sampler choices and
original world placement. Scene epochs/poses, synthetic/manual ancestry and
arbitrary scale remain explicit. Invalidated geometry retains its bytes but is
excluded from GPU packing. Source hashes are integrity bindings, not authenticated
production or rights evidence.

The profile is exclusive with splats, observed/captured meshes, captured atlases
and world-frame wrappers. It supports 1–2 definitions, at most four materials and
64 instances total; each mesh has at most 8,192 vertices/faces and each map is at
most 256². Encoded PBR bytes and packed GPU bytes each have a 16 MiB limit. Mixed
profiles, missing UV/tangents, degenerate geometry/UVs, inconsistent tangent signs,
nonpositive normal-map Z, unsupported source selections and altered bindings fail
admission. Quantized normal Z must be at least 128. Existing generated-static and
captured exporters/bakers do not silently reinterpret PBR resources.

## Bundle and update compatibility

Bundle schema **6** uses `WEXCHK05`, `WEXMSH01` and a length-prefixed `WEXPBR01`
tail containing bounded source documents, UV/tangent/material assignments, RGBA8
maps and instance transforms. The manifest labels `static_native_pbr`,
`unqualified_pbr` and `srgb_textures_linear_brdf`, with collision unavailable.
Chunk/index/provenance assets are immutable and the manifest is published last.
Schemas 1–5 retain their encodings and legacy content-hash branches. New PBR
chunks use a distinct hash discriminator and include the canonical tail.

Initial loads, replacements, cancellation, resync, invalidation and deletion use
ordinary `SceneStore` / renderer update contracts. Vulkan staging, timeline
acknowledgements and delayed resource retirement retain the existing owner. One
vertex/index set and image set per definition is shared across indexed instance
draws; unique geometry counters do not multiply by instance count. Shader-storage
reads are synchronized for both vertex and fragment consumers. PBR display uses a
separate pipeline with the existing compatible color/depth targets.

## Preview rendering and ordinary viewer

`we_viewer --scene PATH` detects schema-6 PBR scenes and selects the native PBR
pass. The project panel labels the draft and provides preview direction,
intensity and exposure controls; synthetic fixtures retain the diagnostic banner.
This spectator view does not enable measured walking. The application API takes
an explicit `PbrLighting`; invalid/nonfinite/out-of-range lights fail before GPU
work. A zero direction from the UI keeps the previous valid value.

Base color/emission are decoded from sRGB **before** texture interpolation; ORM
is linear occlusion/roughness/metallic in R/G/B. Tangent normals retain signed XYZ,
tangent handedness and UV orientation; normalization follows interpolation.
Nearest/linear and clamp/repeat are explicit, without mipmaps. Opaque double-sided
surfaces use counterclockwise fronts and flip the mapped normal on back faces.
The preview evaluates dielectric F0 0.04, metallic mixing, Lambert diffuse,
GGX distribution, correlated Smith visibility and Schlick Fresnel under a white
directional key. Squared roughness has a numerical floor of 0.0001. A constant
ambient approximation is modulated by occlusion; emission is added separately.
Exposure, Reinhard mapping and sRGB encoding produce the displayed image.

This is a diagnostic light model: no fitted illumination, environment integration,
shadowing or physical novel-light accuracy is established. Movement still requires
source AO/support/collision revalidation. Captured radiance is never converted
into albedo. A shader toggle or plausible synthetic image cannot satisfy AP6.
The implementation follows [glTF material conventions](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials)
and the [Vulkan shader interface requirements](https://docs.vulkan.org/spec/latest/chapters/interfaces.html).
