# Depth interchange and pose admission

`scene_store/depth_asset.hpp` owns immutable depth interchange. The application
API in `application/depth_assets.hpp` loads it against independently checked
canonical poses and an expected dense-input domain. No backend, CUDA, SDL or
Vulkan types cross this boundary. [ADR 017](../adr/017-depth-interchange.md)
records the design; this contract does not implement dense reconstruction,
normal estimation, fusion, collision, metric scale or a compressed scene asset.

## Meaning and admission

Depth is axial optical camera **+Z**, with **+X right / +Y down**, undistorted
pinhole calibration and integer pixel centers. Values are in the bound canonical
pose's arbitrary camera units; its world-from-camera similarity supplies world
placement/scale. They are not ray ranges or metres. V1 rejects every other
convention/scale value, including unknown wire values before enum narrowing.

Every asset binds its world epoch, map/pose revision, frame/calibration IDs,
canonical pose-file SHA-256, original observation-image SHA-256, calibrated
image SHA-256 and recipe SHA-256. The calibrated hash identifies the actual dense
input image; it is distinct from the source observation. The expected grid,
intrinsics, producer and recipe must come from checked dense-input preparation,
independently of the asset being admitted. The [native dense-input preparation](dense_inputs.md) now supplies that domain
from retained inputs; copying the asset's own metadata into the expectation proves
no calibration. Its preprocessing and full selected depth-recipe hashes are
separate; only the latter becomes the expected depth recipe dependency.

`admit_depth_asset` checks all metadata against this expectation. Wrong epoch
returns `WRONG_EPOCH`, wrong revision `WRONG_BASE`, and a different pose-file hash
`RESYNC_REQUIRED`; other mismatches reject with `INVALID_DATA` or explicit
unsupported/resource errors. `load_pose_bound_depth` additionally reads the pinned
canonical pose asset and verifies its epoch/revision, observation/calibration/image
identity and tracked pose. Missing, invalidated, INITIALIZING or LOST frames
require resynchronization. TRACKING and FINISHED are admissible states. Neither
function publishes a scene revision or changes existing appearance/collision.

The row-major planes contain:

- Positive finite axial depth at valid pixels; validity is a separate **0/1** mask.
- Finite confidence in **[0,1]**, a recipe-defined relative weight, never an
  inferred probability or uncertainty. Valid pixels may have zero weight.
- Actual support counts, at least one for valid pixels; fusion still needs its
  own cross-view consistency/coverage policy.
- Optional finite nonnegative standard deviation in the same camera units.
  Absence means **unavailable**; confidence is not converted into uncertainty.

Invalid pixels have canonical finite **+0** depth, confidence, support and optional
uncertainty. This normalization does not infer validity from zero. Missing data
stays unknown; even an all-invalid asset is not a recovered floor or walkable map.
Producer tags distinguish `SYNTHETIC_DIAGNOSTIC` from `MULTIVIEW_STEREO`; a tag alone
is not proof of reconstruction quality.

## V1 wire representation

One `.wedepth` interchange file represents one image grid. It has no native C++
struct padding. Scalars/planes use little endian, IEEE binary32/binary64 floats.
The fixed header is **356 bytes**:

| Byte offset | Encoding / meaning |
|---|---|
| 0 | Eight ASCII bytes `WEXDEP01` |
| 8, 12, 16, 20 | uint32 version=1, convention=1 (axial), producer=1 synthetic / 2 MVS, scale=0 (arbitrary) |
| 24, 28, 32 | uint32 width, height, uncertainty-plane presence=0/1 |
| 36, 44, 52, 60 | uint64 epoch, pose revision, frame, calibration |
| 68, 76, 84, 92 | binary64 fx, fy, cx, cy for this depth grid |
| 100, 164, 228, 292 | Four 64-byte lowercase ASCII SHA-256 digests: poses, original observation image, calibrated image, recipe |

Planes immediately follow: `float32 depth[N]`, `uint8 validity[N]`,
`float32 confidence[N]`, `uint32 support[N]`, then optional `float32 sigma[N]`.
`N=width*height`; exact payload is **13N** or **17N** bytes. Trailing bytes,
truncation and unknown flags fail. The externally pinned SHA-256 covers the
entire encoded file; the same owned bytes are hashed and parsed. Zstd-compressed
storage/indexing remains future scene-compilation work; V1 has no compression
sniffing or implicit decoder fallback.

Independent limits bound dimensions, pixels, encoded bytes, decoded plane bytes
and support count before allocation. Defaults: **4096** per dimension,
**16,777,216** pixels, **256 MiB** each for encoded and plane bytes, **1,000,000**
support. The optional uncertainty plane can hit the byte budget before the pixel
budget. Limits may be reduced independently; hard ceilings are 16,384 per
dimension / 268,435,456 pixels / 256 MiB per byte budget. These are per-asset
bounds, not an assertion about complete process/device peak memory.

Encoding, checksumming, validation and file I/O poll cancellation in bounded
blocks. Caller data is borrowed const; failed admission preserves it. Shared
`core/asset_files.hpp` primitives reject final-component read symlinks, stage
closed/flushed files, publish exclusively and clean owned staging paths. Existing
entries are never replaced by immutable asset publication. If the directory flush
fails after publication, the file can already exist; inspect its digest before
retrying. Atomic manifest replacement remains a separate owning-store operation.

## Diagnostic command

```sh
build/dev/we_process inspect-depth INPUT.wedepth SHA256 [--max-dimension N]
```

This shared application command validates format/checksum and prints metadata,
valid/unknown counts and uncertainty availability. The optional dimension limit is explicit and bounded to 1–16,384; other byte/pixel
bounds stay unchanged. It labels synthetic input and
states that pose-context/calibration/geometry quality remain unverified. It does
not run a decoder, solver, optimizer or GPU, or enable walking. Bad input exits 2;
observed cancellation exits 130. [Initial evidence](../validation/2026-10-17-depth-assets.md)
separates synthetic interoperability from real retained-pose read regressions.
