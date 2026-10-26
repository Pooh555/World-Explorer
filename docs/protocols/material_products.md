# Bounded material products v1

[`material_product.hpp`](../../include/world_explorer/application/material_product.hpp)
adds owned channel/texel-provenance admission to `application`. The independently
selected private geometry, capture and receipt handles must share the original
receipt, epoch and pose revision. Geometry retains its original capture product;
another capture manifest cannot substitute merely because some views match.
The selected material variant must occur in the actual admitted mesh groups,
with complete UV/tangent arrays. The material root's inference dependency closure
passes the existing capture firewall before payload interpretation.

This additive CPU profile preserves generated-static/constant-PBR v1 encodings.
It supplies no worker, actual inverse fitting, asset-definition attachment,
renderer transaction, portable export or qualification token. A0/AP0 remain
**IN_PROGRESS**. Synthetic source and declared manual ancestry remain explicit.

## Root and channel bindings

The strict JSON root has exactly `schema_version=1`, `kind=MATERIAL_PRODUCT`,
`world_epoch`, `pose_revision`, `recipe_sha256`, `geometry`, `capture`,
`variant_id`, `lighting`, `brdf`, `grid`, `uv_set`, `uv_origin`, `sampler`,
`normal`, `relighting_scope`, `channels`, `fit_inputs`, `manual_ancestor`.
Product references use the existing exact ID/revision/content-SHA object.
Grid dimensions must match independently selected width/height, each 1–256.
This format budget does not satisfy the AP5 2K texture target.

| Lighting | Channels | BRDF / scope | Fit inputs |
|---|---|---|---|
| CAPTURED_APPEARANCE | CAPTURED_RGB only | CAPTURED_UNLIT / CAPTURED_ONLY | None |
| ESTIMATED_PBR | BASE_COLOR, METALLIC, ROUGHNESS, NORMAL, OCCLUSION, EMISSION | METALLIC_ROUGHNESS_GGX / UNQUALIFIED | LIGHT and EXPOSURE required; WHITE_BALANCE optional |
| GENERATED_PBR | Same six PBR channels | METALLIC_ROUGHNESS_GGX / UNQUALIFIED | Optional |

All six PBR roles are required in this profile, including explicit occlusion and
emission. `uv_set=0`, `uv_origin=TOP_LEFT`. Sampler keys are `filter` (NEAREST or
LINEAR), `wrap` (CLAMP_TO_EDGE or REPEAT), `mip=NONE`; selections must match.
Captured normal is null. PBR normal keys are
`basis=GEOMETRY_TANGENT_W_SIGN`, `y=POSITIVE_BITANGENT`: bitangent is tangent.w
times cross(normal, tangent.xyz). Geometry admission checks tangent handedness.

`channels` is an exact role-keyed object. Each row has `product`, `provenance`,
`color_space`. Captured RGB/base color/emission declare LINEAR_SRGB primaries;
other roles declare LINEAR_DATA. The native interchange stores linear floats;
plan §11.4's sRGB-encoded color export requires a separate explicit converter.
It does not claim PNG/JPEG or glTF texture conformance. Captured radiance is never
converted to albedo by admission. Mip generation, seams, UV overlap/stretch,
padding, bake visibility and physical response remain unverified.

The root depends exactly on geometry plus every selected channel, provenance and
fit product. Channels depend exactly on geometry plus all selected fits. Capture
is coordinator metadata, not a graph input: its full manifest contains evaluation
metadata. No evaluator input is exposed to fitting.

## WEMATX1 bytes

Each channel is a fixed 64-byte header followed by exact row-major top-left
float32 samples, all little endian. No padding/trailing bytes are accepted.

| Offset | Field |
|---|---|
| 0 | Eight magic bytes: ASCII WEMATX1 then NUL |
| 8 / 12 | u32 version 1 / role ordinal |
| 16 / 20 / 24 / 28 | u32 width / height / components / reserved zero |
| 32 / 40 | u64 product ID / revision |
| 48 / 56 | u64 world epoch / pose revision |
| 64 | width × height × components float32 values |

Role ordinals are CAPTURED_RGB=0, BASE_COLOR=1, METALLIC=2, ROUGHNESS=3,
NORMAL=4, OCCLUSION=5, EMISSION=6. RGB/normal/emission have three components;
metallic/roughness/occlusion have one. All values must be finite: base color and
scalar channels [0,1], captured RGB/emission [0,64], signed normal components
[-1,1]. Every tangent normal's squared length differs from 1 by at most 1e-4.
The radiance bound is an admission budget, not a calibrated physical-unit claim.
All header identity/context/grid fields must match the independent selection.

## Complete texel provenance

Each selected sidecar passes native [region admission](asset_region_provenance.md)
with target=channel, domain=MATERIAL_TEXELS and exactly width×height elements.
Complete contiguous partitions retain separate class, nullable confidence,
parents, source support and manual ancestry. Each of at most 16 independently
selected sources must be used and bind an actual selected geometry inference
view, calibration ID, image product and native pose-asset SHA. Calibration and
image-grid SHA declarations remain hash-bound metadata; this command does not
recompute intrinsic/grid checksums or establish physical correspondence.
Sidecars depend only on their target channel; each span's parents must be direct
channel dependencies. Existing region/manual lineage checks apply unchanged.

Captured mode forbids inferred/generated/manually edited texels. Estimated mode
forbids generated/manually edited texels. Generated mode permits explicit mixed
partitions, including observed/inferred/unknown regions; it cannot hide those
regions under a pure estimated label. Transitive manual ancestry remains even
where automated descendants use another evidence class. Declared actions and
confidence do not establish human authenticity or legal rights.

## Explicit fit records

Each independently selected fit has a unique role and 1–16 distinct full views,
all drawn from admitted geometry inference views. Strict JSON keys are
`schema_version=1`, `kind=MATERIAL_FIT_INPUT`, `world_epoch`, `pose_revision`,
`recipe_sha256`, `geometry`, `capture`, `role`, `views`, `parameters`,
`manual_ancestor`. Exact dependencies are geometry plus those image/pose products.
Root `fit_inputs` rows contain exactly `role`, `product` and match the selections.

| Role | Exact parameters | Bounds |
|---|---|---|
| LIGHT | model=CONSTANT_ENVIRONMENT, linear_rgb=[r,g,b] | Each [0,64] |
| EXPOSURE | model=SCALAR_LINEAR_GAIN, gain | (0,64] |
| WHITE_BALANCE | model=DIAGONAL_LINEAR_WHITE_BALANCE, linear_rgb=[r,g,b] | Each (0,64] |

These simple finite declarations do not run fitting, calibrate illumination or
prove that source shadows are absent from base color. Fixed-response production
and real changed-light AP6 evaluation remain required separately. No evaluation
threshold, qualified-lighting claim or arbitrary fit model is accepted.

## Ownership, budgets and recovery

Root/fit JSON is bounded to 64 KiB each; channels to 786,496 bytes each;
provenance to 256 KiB each before native region inspection; owned publication
to 8 MiB. Strict JSON rejects duplicate/unknown keys, excessive depth and NUL.
Handles own decoded channel/provenance/fit data and original record bytes.

Publication uses the existing immutable store, protecting the receipt source
tree and validating receipt/context again. One batch contains root, every raw
channel, every provenance sidecar and every fit (at most 16 records). Keys are
`material-ID-REV`, `material-channel-ID-REV`, `material-region-ID-REV`,
`material-fit-ID-REV`. Cancellation preserves the last committed manifest;
matching retries resume the same intent. Wrong base/context or conflicting
operation requires failure/resync. Reload freshly admits independent inputs,
requires the selected current commit and checks every closure key/content hash.
A committed root without channels, provenance or fits is insufficient.
