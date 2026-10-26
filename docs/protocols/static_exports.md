# Generated constant-material GLB and export manifests v1

[`static_export.hpp`](../../include/world_explorer/application/static_export.hpp)
adds a separate `WE_GENERATED_CONSTANT_GLB_V1` producer and source-bound package
admission. Existing captured-unlit GLB and generated-static payload encodings are
unchanged. A0/AP0 remain **IN_PROGRESS**. This exports admitted generated static
data; it supplies no reconstruction, intrinsic material recovery, relighting,
real asset-production, engine-import or AP/Q/N qualification.

## Source and supported conversion

The source supplies private `AdmittedAssetInstances` and original receipt handles,
exact per-definition references/provenance selections, expected epoch/pose revision
and independent finite [0,1] position/normal error limits. Instances are freshly
re-admitted against their definitions/receipt; their canonical identity must match.
Each definition's actual mesh, constant material and two provenance sidecars pass
existing static-payload admission. This does not accept opaque geometry/material
metadata in place of actual bytes.

The profile supports 1–16 definitions and 1–64 instances. Every definition has one
material/variant and ≤512 vertices/faces under the existing static profile. All
instances must be visible, with no room/support/lineage relations; units remain
arbitrary, without LOD or collider. Unsupported fields fail rather than disappear.
Expanded native geometry/material/completion products and textured PBR need a
separate converter and asset-definition adoption; they are not reinterpreted here.

The GLB contains one indexed mesh and one core metallic-roughness material per
definition. Nodes reference those shared resources; visually identical distinct
definitions are not silently merged. Pivot transforms are baked into local
vertices. Nodes retain world-from-asset TRS, explicitly converting wxyz to glTF
xyzw quaternions. Local positions/translations are bounded to ±1,000,000, positive uniform scale
to [1e-4,1e4] and quaternion squared-length error to 1e-12. Pivot admission still
requires a rigid transform. No negative-scale or mirrored-node fallback exists.

POSITION/NORMAL/TEXCOORD_0 use float32 with stride 32; uint32 indices occupy a
separate view. Positions/UVs must not collapse faces, and winding must agree with
normalized exported normals. Accessor bounds use actual converted positions.
Binary data, GLB headers and aligned chunks are little endian. No URI, image,
texture, animation, camera or extension is introduced. Output GLB is ≤1 MiB.

Constant base color/emission factors are linear; metallic/roughness retain their
source values. Alpha is opaque and surfaces are double-sided. Core emissive
factors require emission ≤1; occlusion must equal 1, permitting its omission.
Other occlusion, HDR emission and texture channels reject as unsupported. These
choices follow the [glTF 2.0 specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials)
for factors, normals, TRS, buffer views and accessors. They do not establish that
generated constants are physically correct materials.

## Authoritative package and losses

`prepare_static_export` returns a deterministic proposal; `inspect_static_export`
independently rebuilds it from original private inputs and compares every file.
The closed canonical-byte profile rejects altered GLB, JSON formatting, unknown
files, unsafe/repeated paths, missing sidecars and mismatched source/audit pins.
It is not a general third-party glTF importer. File-vector order is irrelevant.

| File | Role |
|---|---|
| `scene.glb` | Ordinary self-contained geometry/material/instance data |
| `manifest.json` | Version/profile, exact source scene revision, definition set, resource/node mapping, file bytes/hashes, losses, audit outcomes and unqualified scope |
| `catalog.json` | Canonical admitted definition snapshots; exact definition set, without inventing a mutable catalog-head revision |
| `scene.json` | Canonical admitted instance snapshot and original receipt binding |
| `provenance/ID-REV.json` | Exact independently selected generated/edited region bytes |
| `rights/ASSET.json` | Original rights reference and UNREVIEWED_BINDING_ONLY; opaque rights text omitted |
| `audit-validator.json`, `audit-importer.json` | Explicit source-bound audit selections/outcomes |
| `reports/{validator,importer}.json` | Selected raw reports, when supplied |

Manifest asset/revision/instance IDs use decimal strings; native catalog/scene
sidecars retain their existing integer-preserving JSON contract. Custom GLB
extras are not the authoritative semantic identity copy. Per-asset raw converted
geometry/index hashes and core-material JSON hashes are independent of resource
offsets and instance count, enabling individual/assembly comparison. Source
geometry/material product hashes remain distinct from these conversion hashes.

The manifest declares FLOAT32_GEOMETRY, NORMALS_NORMALIZED,
PIVOT_BAKED_INTO_VERTICES and NOMINAL_METRE_MAPPING. It records maximum native
world-position error over every vertex/instance and local normal error, plus
the independent limits. This calculation uses original double TRS, without
claiming an engine's transform precision. UVs also convert to float32; this profile
checks face validity but has no separate UV-error/texture-quality certificate.
One arbitrary source unit maps to one nominal glTF metre, without measured scale.
Captured radiance, images, opaque rights text and arbitrary authoring documents
are not added. Explicitly selected tool reports remain uninspected raw report
bytes; no report-text sanitization is claimed. Walking remains disabled.

## Tool observations and limits

Both audit selections are mandatory; explicit NOT_RUN is permitted and records
null tool/report/counts. A reported outcome must pin target GLB SHA, report SHA,
tool name/version and executable/package SHA independently. Report bytes must
match these selections. REPORTED_PASS requires zero errors; REPORTED_FAIL requires
nonzero errors. Warnings remain explicit. No outcome authenticates execution.

Validator observations accept KHRONOS_GLTF_VALIDATOR and match the raw report's
`validatorVersion` and `issues.numErrors/numWarnings`. Import observations accept
GODOT with a normalized `GODOT_IMPORT_OBSERVATION` report: schema version, payload
hash, errors/warnings, mesh/node counts and node-to-mesh indices. Reported import
success must account for all canonical nodes and resource sharing. This schema
is a declared observation, not a Godot runner or a transform/visual parity test.
All packages retain `qualification=UNQUALIFIED_EXPORT` and tool
`execution_authenticity=UNVERIFIED`; these records cannot clear AP7, rights or A1.
The [Khronos validator API](https://github.com/KhronosGroup/glTF-Validator/blob/main/node/README.md)
provides separate real conformance observations; ordinary Godot evidence remains
required for the intended engine import track.

## Publication and recovery

At most 56 files/8 MiB are owned, with per-file maxima: GLB 1 MiB; manifest,
catalog and scene JSON 256 KiB; provenance/report 64 KiB; audit 16 KiB; rights
bindings 2 KiB. Only admitted canonical files can be written. Handles own all
bytes and source identities, independently of caller/source lifetime.

`publish_export_manifest` rechecks original receipt/context and uses the existing
immutable store, protecting its source tree. Record keys hash manifest identity
and relative file path. Cancellation preserves the last committed package; exact
operation retries resume idempotently. Reload requires the selected current
commit, fresh independent source admission and every committed file/hash.

`write_static_export` materializes a new local directory outside the protected
source tree, with data files first and `manifest.json` last. Existing destinations
reject unchanged. Interruption can leave an incomplete new directory; consume
only a complete terminal manifest with matching files. This writer does not add
crash/disk-full/concurrent-mutation qualification or resume an ordinary directory.
