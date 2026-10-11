# Decoded frame source

`open_decoded_frame_source` returns the project-owned `frame_source` interface.
Its current profile replays PNG images from a committed `timestamp_sampling_v1`
decode stage; it does not rerun the source-video decode or reconstruction.
An explicit source SHA-256 supplies the capture identity. The CLI hashes the
selected local video. The legacy stage records only a 16-digit source prefix in
external frame/calibration IDs; this adapter checks that prefix and the caller's
declared full identity, rather than claiming the stage independently records it.

The stage basename/key/status/schema, completion inventory, metadata size/hash,
selected image references, original timestamps, indices, dimensions and pixel
budgets are validated before admission. Each used image is hashed before and
after decoding; the completion manifest must remain unchanged. Image paths must
remain within the stage without symlinks. Only referenced observation assets are
consumed; unrelated backend logs are not certified by this adapter.

Current limits: 120 selected frames, 36000 source frames, 1200 seconds, 1 MiB
manifests, 64 MiB encoded images and configurable RGB bytes up to 64 MiB. PNG
dimensions are checked before launching FFmpeg. One decoder runs per read with
one thread, no automatic rotation, and a 64 MiB single-allocation limit. These
limits do not certify aggregate process-tree or caller-retained lease memory.
The current replay profile requires uncropped/unresized images and the worker's
single `lens-0` calibration identity with consistent dimensions. Unsupported
calibration/profile variants require an adapter extension.

Frames contain immutable RGB8 leases, width/height/row stride, integer capture
nanoseconds and original PTS/time base. RGB comes from decoded captured PNGs;
it is not relightable albedo. Capture timestamps must strictly increase. Negative,
overflowing or inconsistent timestamps are rejected. Depth and IMU are absent.
Intrinsics/distortion are not invented by this source; the calibration ID must
be resolved through the forthcoming native pose/map adapter.

Opaque IDs are the first 16 hexadecimal digits of SHA-256 of UTF-8
`world_explorer.observation.v1/{full_source_sha256}/{kind}/{external_id}`, parsed
as uint64. Kinds are `capture`, `frame`, `calibration`; capture has an empty
external ID. Zero IDs and within-stage collisions are rejected. Identity is
independent of stage location and persists across reopen/seek.

The first read emits `source_reset` at epoch zero. Seek selects the first frame
at or after the target's integer nanoseconds, increments the reset epoch and
emits a reset before frames/EOF. Seeking past the last frame yields reset/EOF;
EOF can be read repeatedly. Cancellation, failed reads and failed seek admission
preserve position. Read/seek have one owner; cancellation interrupts child I/O.
Caller-retained leases survive subsequent reads, seek and source destruction.

`we_process replay-frames DECODE_STAGE VIDEO` exercises this path and prints
metadata/RGB digests, then releases each lease. It is an inspection command;
native batch pose submission and automatic pose-to-fit chaining remain pending.
