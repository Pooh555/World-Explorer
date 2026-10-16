# Checked native dense inputs

`application/dense_inputs.hpp` prepares retained completed poses and undistorted
images without a solve, fit, pixel resample or depth calculation. It supplies
`depth_asset_metadata` independently of a depth file. This implements M3 input
preparation; it does not qualify an MVS backend, geometry, scale or walking.

## Inputs and checks

The caller selects a completed pose job/source SHA-256, optional explicit decode
stage, canonical `.weposes` path/hash, and depth-recipe path/hash. Existing
completed-output import checks the decode and pose inventories, source images,
calibrations and poses. Re-encoding the canonical map from those inputs must match
the selected canonical file exactly. Corrected or invalidated poses require a
fresh preparation rather than reuse of the original dense model.

The committed pose stage supplies `training/sparse/cameras.bin`, `images.bin` and
`training/images/NAME.png`. The adapter admits one SIMPLE_PINHOLE/PINHOLE camera,
checks camera/image identities, distinct names/IDs, finite poses and track
coordinates, and compares every transformed pose with the canonical pose. Native
COLMAP world coordinates are rotated by Rx(pi); optical axes stay right/down/
forward. Equivalent quaternion signs compare through transformed basis points.
Image names are bounded basenames; committed inventories reject symlinks and
escaping paths. Exact PNG hashes and RGB8, noninterlaced IHDR dimensions must
match the undistorted camera. This header check does not decode PNG payloads;
the consuming worker must decode and validate its pixels before using them.

V1 uses the **native undistorted grid**, with **no crop or resize**. Canonical
intrinsics subtract 0.5 from COLMAP's half-pixel principal point. A later resized
profile must bind actual resized image bytes and use per-axis
`fx' = sx*fx`, `cx' = sx*(cx+0.5)-0.5` (and similarly for Y). Scaling `cx` alone
is incorrect. No resized output is implemented by this profile.

Camera dimensions retain the existing training-camera limits: minimum 11,
maximum 8192 per dimension and 4096² pixels. `images.bin` is capped at 256 MiB,
120 images and two million track entries per image, bounded by remaining bytes
before iteration. PNGs are capped at 64 MiB each. Recipes/manifests are capped at
1 MiB. Reads and record iteration check cancellation. These are input bounds,
not full process/device peak-memory guarantees.

## Manifest and recipe identity

The deterministic JSON manifest pins source, canonical poses, pose/decode
completion, camera/model and original/calibrated image hashes. It records epoch,
revision, typed frame/calibration IDs, native grid/intrinsics, world-from-camera
poses and original distorted calibration. Units remain arbitrary pose-camera
units; gravity, geometry and measured scale are not inferred.

The fixed `colmap_native_dense_input_v1` preprocessing recipe has a separate
SHA-256 over compact sorted-key JSON. The selected **full depth-producing recipe**
is pinned by its exact file bytes. It must declare `schema_version: 1`,
`input_profile: "colmap_native_dense_input_v1"` and
`depth_producer: "MULTIVIEW_STEREO"`. Preparation checks identity and input profile;
the eventual worker owns algorithm/configuration validation and qualification.
A declaration or successful preparation does not prove that a recipe is runnable.
Its exact hash becomes the expected depth metadata's `recipe_sha256`, so changing
solver settings requires new dependent depth. Prepared metadata expects axial
camera-Z and MVS provenance, whose tag alone is not quality evidence.

`write_dense_inputs` publishes one closed/flushed immutable manifest exclusively,
outside selected inputs. Existing entries are refused. It borrows retained image
paths and does not copy or change their bytes. `load_dense_inputs` hashes the
manifest, independently repeats preparation, and requires byte-exact agreement;
changed metadata returns `RESYNC_REQUIRED`. Consumers must still hash the actual
bytes they decode: a checked path is not an immutable pixel lease. No scene,
appearance or collision revision is published.

A native grid may exceed depth interchange's **default 4096 dimension cap**.
For example, sample_1 is 4104×2208. A consumer needs an explicit supported limit
(e.g. `depth_asset_limits.max_dimension = 8192`, retaining the other bounds), or a
separately implemented/checked resize profile. Never silently resize or raise
limits to disguise a resource failure.

## CLI

```sh
build/dev/we_process prepare-dense POSE_JOB SOURCE_SHA POSES.weposes POSE_SHA RECIPE.json RECIPE_SHA OUTPUT.json
# Optional: append --decode-stage IMMUTABLE_DECODE_STAGE
```

Success prints the manifest SHA-256; invalid input exits 2 and observed
cancellation exits 130. Destination parent must exist. All generated files belong
in ignored `artifacts/`. [Local evidence](../validation/2026-10-17-dense-inputs.md)
separates real retained-image correspondence, synthetic admission tests and the
still-unqualified MVS/depth/fusion path.
