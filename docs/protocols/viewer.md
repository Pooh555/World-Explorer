# Viewer settings, camera and automated completion

The spectator is independent of capture poses. New/reset fly bindings follow
specification §4.8: W/S forward/back, A/D left/right, Space/Left Ctrl up/down,
Left Shift accelerates. Wheel scales speed; Escape releases capture. The controls
overlay names the active SDL scancodes, including remaps.

Bookmarks are session-local and restore the saved fly pose, viewing up, speed and
vertical FOV together, after scene/epoch validation. Fit and Source view can
change the current FOV without changing the saved view.

UI layout uses SDL display scale divided by window pixel density, recalculated
before each ImGui frame. Font raster sizes are physical pixels: one immutable
atlas contains 13 sizes (1–4×, quarter steps); the closest size at or above the
requested scale is selected with a compensating logical font scale. Nonfinite,
nonpositive or >4 scale/density fails explicitly. Theme changes start from an
unscaled style. Import stays centered and its size is constrained to the current
viewport, including resizing while it is open. This implementation does not
qualify every native platform or physical display.

`--input-trace NEW.jsonl` explicitly enables native diagnostic observations:
at most 4,096 frame rows of camera/UI/focus/remap/scale state and widget rectangles.
The parent must exist and the trace path must be new. Write failure fails the
viewer explicitly. F12 (outside key remapping) requests at most 16 viewer-only
PPM readbacks beside the trace, named `<stem>-<frame>.ppm`. It does not capture
other applications or inject events. Use a dedicated ignored artifact directory
and isolated `--settings`; traces/readbacks are local diagnostic data. Per-frame
JSON flushing/readbacks alter timing, so these runs cannot certify N1 latency.

Synthetic labels follow the active snapshot's [scene provenance](scene.md), not
the launch option. Toolbar and ready status identify synthetic scenes even with
side panels hidden; imported/replaced scenes and terminal diagnostics use the same
classification. `--diagnostic-fixture` only chooses the initial generated scene.

Preferences schema 1 stores `left_panel`, `right_panel`, `controls` and seven
`keys` ordered forward/back/left/right/up/down/accelerate. Explicit saved mappings
are restored exactly, including previous defaults; they are never guessed to be
legacy values needing reassignment. Reset is an explicit user action. Read limits:
64 KiB, supported schema, exactly seven integer keys, each `0 < key <
SDL_SCANCODE_COUNT`. Wrong schema/types/counts/ranges retain current in-memory
settings without partial changes. Save writes a temporary file and replaces the
selected file; crash/power-loss qualification remains separate.

One owned job publishes immutable snapshots under the UI-state mutex. `busy`
clearing does not imply UI admission: a snapshot can still await consumption.
`has_pending_work()` checks both under that mutex. Starting any load, worker or
reconstruction checks the same busy/pending condition under the mutex; rejection
preserves pending snapshot, status and start time. UI commands own the `jthread`
handle. Cancel calls `request_cancel()` after releasing the state mutex because
stop callbacks execute synchronously and may need that mutex. Automatic screenshot, next
startup reconstruction and `--frames` exit respect this pending handoff; renderer
visibility is separately gated by the update ticket. A frame limit is a minimum
before completion, not permission to capture/exit before a selected scene is
visible. Errors still fail explicitly. Manual navigation/input/HiDPI/latency and
other native-platform qualifications require their own evidence.

Reconstruction processing uses one mutex-protected typed progress value. Native
stage transitions cover request validation, video inspection/selected frames,
poses/map, fitting or completed-fit verification, compilation and viewer loading.
Only checked worker events supply backend elapsed time; the fitter's bounded JSON
`step` must be an unsigned integer within the checked request's total. Other
backend text stays in the bounded diagnostic log. Unknown stage/invalid optional
metadata never fabricates a count, percentage or ETA. Whole-job elapsed time stays
separate from event-scoped backend elapsed time; ETA remains unknown.

Each native stage clears previous step/timing fields. Failure/cancellation names
the active stage and retained output; post-publication load failure keeps the scene
location. Completion clears active progress and processing under the same mutex
that publishes the pending snapshot. Late progress is ignored after completion.
Completed reuse shows verification rather than an invented fit. UI rendering,
Cancel, logs and the existing renderer visibility gate retain their own lifetimes.

An optional viewer reconstruction observer runs synchronously on the owned job
thread after each typed state update, outside the state mutex, borrowing progress
and its stop token. Diagnostic observers must honor stop and use bounded waits;
they do not change the coordinator or synthesize completion. Tests use exact stage
boundaries to cover Cancel/reopen, post-publication stop and pending admission.

`we_gpu_reconstruction_handoff <prior scene> <completed request> <new report directory>`
exercises the same owned job and GUI Cancel command with native Vulkan readback.
It requires explicit completed-fit reuse and existing completed poses/fit, uses
one-million-splat/512 MiB renderer limits, and checks pause/old-pixel preservation,
reopen/pending admission and full-scene visibility. A hidden 1280×720 diagnostic
surface omits ImGui; this is not mouse-widget, HiDPI, latency, FPS or another
platform's qualification. Reports/captures belong in ignored artifacts.

Viewer preference/handoff unit tests build only with `WE_BUILD_VIEWER` and
`BUILD_TESTING`. They require the viewer's SDL/ImGui dependencies but no GPU
execution. CPU-only presets retain their independent contract tests.

## Import request review

The Import dialog runs `Review request` on the same owned background-job thread,
using the shared strict reader. Appearance stays active; review is read-only and
does not start a worker, decoder, pose solve, optimizer or scene publication.
Explicit retained inspection runs shared preflight: bounded committed-stage
inventory/hash/schema validation, source-byte/hash identity and current output
storage. The summary shows checked codec/dimensions/duration/source size, original
time base/display rotation, captured color domain and arbitrary scale.
Automatic inspection leaves metadata pending; review checks current storage without
searching for or adopting an unselected cached inspection.
One immutable presentation value holds at most 32 rows / 128 KiB text. Error
bodies are limited to 2,048 bytes. It shows selected fresh/retained pose action,
start/resume/reuse, source/scene output, frame-selection/CPU/storage caps when
specified, and fit steps/image/splat limits. Completed reuse marks fit values as
compatibility requirements, not work to be performed or final scene size. Legacy
requests expose absent inspection without inventing frame/storage values.
Backend paths and separate manual worker commands remain advanced controls.
Storage uses each destination's nearest existing directory, including fresh
inspection/decode/pose jobs on separate volumes. Unknown/insufficient space or a
file ancestor rejects review. Available bytes are snapshots, not reservations or
whole-job peak estimates; inputs/storage are checked again when running. Legacy
requests without inspection do not claim metadata or storage checks. The request
digest/canonical selection is rechecked after metadata/storage review before
publishing eligibility. Metadata does not certify decoding or capture quality.

`Run reviewed request` rereads the canonical path with the reviewed digest on the
job thread. Editing the field invalidates eligibility, including an in-flight
review. Changed/deleted/retargeted files or failed rechecks clear the summary and
require another review before running. Input/stage/backend checks remain in the
coordinator; their failure never silently changes recipe or reuses another fit.
A scrollable details area keeps Run/Close outside its content viewport.

`we_viewer --review-request FILE` opens and reviews Import without launching the
job. It cannot combine with startup `--reconstruction-request`; frame exit and
readback wait for the selected review and initial scene visibility. Automated
review failure exits explicitly. `--reconstruction-request` keeps its established
startup execution path. Current native sizes/input limits are in validation,
not broader platform/HiDPI qualification.

## Fixed-camera rendering

`camera_view` accepts optional `calibrated_projection` with canonical integer-center
`pinhole_camera` intrinsics and framebuffer width/height. Calibration overrides FOV;
its dimensions must match the actual framebuffer. A resize mismatch fails explicitly,
without admitting pending scene updates. Spectator views omit calibration and retain
a symmetric projection derived from vertical FOV. No capture pose drives navigation.

`raster_intrinsics` validates finite position, orthonormal right/down/forward optical
basis, positive ordered clipping planes and representable focal/principal values.
It adds 0.5 to canonical principal points once at the raster boundary. Vulkan and
gsplat therefore sample the same half-integer raster centers. Projection uses the
pinned classic gsplat covariance filter and `0.3 * tan_fov` derivative margin;
`tan_fov = 0.5 * extent / focal`. This does not claim full renderer parity.

`we_gpu_reference` generates a labeled synthetic scene bundle, four calibrated
1280×720 captures and their exact input JSON through normal update admission.
`--compositing` selects a separate saturation/overlap fixture with seven views,
including orbit/close-up; `--termination` selects a separate near-opaque exclusive
cutoff fixture; `--batches` selects 1,027 splats to exercise shared-memory reuse across batch
boundaries. `--batches-bounded` uses the identical fixture/views with a 2,048-reference
per-batch cap to force partitioning; default fixtures and tolerances remain unchanged.
`tests/gpu/compare_reference.py` requires the pinned local training environment and
native CUDA; it never downloads/trains. Diagnostic JSON input is bounded to 4 MiB.
It preserves float reference images, PNGs, error heatmaps and per-view metrics.
Fixed diagnostic tolerances (PSNR ≥45 dB,
maximum eight 8-bit levels) compare quantized SDR output; they do not
replace Q/P gates. Commands/results and untested cases belong in validation evidence.

`we_gpu_reference <new output> --real <scene bundle> <completed fit directory>`
loads the complete checked scene, requires matching fit/capture/PLY identities and
verifies the original PLY checksum. It captures first/middle/last saved fit cameras
with original resized intrinsics, dimensions and roll. The saved float32 affine
rotation is inverted directly when deriving position, rather than assuming its
transpose is an exact inverse. This diagnostic does not drive spectator navigation.

Real diagnostic schema 2 requires explicit `--real` in `compare_reference.py`.
It bounds JSON to 4 MiB, frames to 120, dimensions to 4096 per axis, chunks to 1024
and the full scene to one million splats. Each separately hashed binary chunk
contains 60 little-endian float32 values per splat: mean3, scale3, wxyz4, opacity,
coefficient-major RGB SH48, degree. Exact size/count, contained paths, finite values,
transforms, complete fit configuration identity and original camera correspondence
are checked before reference rendering. Real and synthetic labels remain separate;
no splats are trimmed to fit the synthetic profile.

Scene upload already rebases world means around the admission camera in double
precision before float32 conversion. The real diagnostic records that origin;
both renderers consume the same rebased float32 payload and equivalent camera
transform. Projection evaluates affine basis rows plus translation; quaternion
normalization and world covariance followed by camera covariance transformation
follow the pinned trainer. Floating-point behavior at the exclusive cutoff remains
sensitive; three passing captured views do not certify all cameras or Q6 packing.

Splat keys sort positive finite center depths front-to-back, stably retaining input
order for ties. GPU overlap count/exclusive scan precedes reference emission.
Emission follows depth order; stable 32-bit tile sorting therefore preserves the
logical `(tile, depth, original order)` ordering using two-word `(tile, splat)`
references. GPU tile ranges restrict each 16×16 workgroup to its contributors,
read in fixed 256-splat shared batches and accumulated per pixel in FP32.
Tile bounds, half-integer samples, alpha clamp 0.999, minimum alpha 1/255 and
exclusive `next_T <= 1e-4` termination follow the pinned gsplat recipe. Termination
skips that contributor and retains preceding transmittance for background.
Out-of-frame lanes participate in shared barriers; the group stops only when all
in-frame pixels terminate. Center-depth ordering remains approximate for extended/
intersecting Gaussians; diagnostic comparisons do not certify full renderer parity.

`renderer_options.max_tile_references` bounds each emitted batch (default/maximum
8 million). Saturating counts are read after timeline completion, before admission,
allocation or emission. Oversized work splits recursively into disjoint tile
rectangles. Every full-resolution pixel and contributor is retained; a single tile
that cannot fit fails explicitly before emission. Whole-frame diagnostic
`tile_reference_count` sums accepted rectangles and may exceed the per-batch cap;
this does not broaden the P2 workload envelope. Host count waits remain a measured
synchronization cost, not a fully asynchronous P6 implementation.

`renderer_options.reuse_tile_plans` defaults on. The renderer caches only the last
successfully submitted frame's disjoint rectangle partition, up to 128 regions.
Every candidate is counted again on the GPU and admitted under current reference,
allocation, dispatch and residency limits before emission. Camera/scene changes
can split candidates further; counts, GPU addresses and allocation decisions are
never cached. A failed/incomplete frame preserves the previous complete partition.
Swapchain refresh resets the plan, including pixel resizes within the same tile grid.
Pause/empty frames do not count or replace it. Excessive fragmentation discards the
whole cache after completing all work, rather than truncating coverage. Fixed CPU
metadata holds 128 cached, 128 accepted and 192 pending rectangles; split depth is
bounded by uint32 dimensions. Sparse transitions retain the old partition until
resize/reset; automatic merging is not implemented and can cause extra counts.

`we_gpu_viewer_regression ... --fresh-tile-plan` disables candidate reuse for paired
comparisons in the same executable; it can combine with `--timings`. Completed
samples include `reused_tile_regions`, distinct from current count submissions and
accepted batches. `we_gpu_tile_plans <new output directory>` checks labeled
synthetic density changes, camera movement, pause/empty, resize, per-tile overflow
and fresh/cached screenshot equality; it requires native validation and timestamps.

Two in-flight frames own FP32 pixel buffers. Projection/key/scan scratch grows to
resident count, not configured maximum splats. Completed frame ownership gates
scratch reuse; device-idle resize retires pixels, ranges and reference pools before
replacement. Actual Vulkan allocation requirements count toward residency; the
reference admission calculation shares remaining capacity between frames and
reserves one SDR screenshot readback allocation. `framebuffer_bytes` counts owned
pixel buffers, `scratch_bytes` excludes pixels, and `allocated_bytes` includes
tracked live/retired/staging buffers; these are not total process/device VRAM.
Dimensions, storage/dispatch/shared-memory limits, 128-byte push constants and ten
storage-buffer descriptors per stage/set are checked explicitly.

A fullscreen triangle reads completed FP32 pixels directly from storage, clamps
RGB once to the SDR swapchain and then draws UI in the same subpass.
`vulkan_overlay_context.subpass` is 0. No extra FP32 swapchain attachments or
input-attachment pass remain. Compute-write/fragment-read and acquired-image
color-output dependencies are explicit; timeline completion gates frame reuse.

`we_gpu_viewer_regression <scene directory> <new report.json>` checks the full
scene at 1280×720, hidden 3840×2160 with screenshot readback, actual native
fullscreen, and restoration. It requires validation, checks fullscreen flags and
renderer/actual pixel extents, and records eight warmup plus 24 measured frames
per phase, verifying every measured draw submitted a render frame before calculating
throughput. Window managers may choose restored window dimensions. Wayland must
receive a committed visible buffer before fullscreen mapping. Reported throughput
is GPU-completed render frames, not P2/P3 presented-FPS certification.

## Synthetic clipping boundaries

`we_gpu_reference NEW_DIRECTORY --clipping` selects a separate six-splat fixture,
labeled **synthetic inclusive clipping float neighbors v1; not a capture**. Its
fixed 1280×720 calibrated camera is at the origin, with near **1** and far **8**.
Mean depths are the immediately adjacent float32 values on either side of each
plane, plus exact equality. Perspective ratios use powers of two and six isolated
image regions. This tests **mean-depth culling**, not clipping Gaussian volumes.
Both planes include equality, matching the pinned gsplat packed projection.

`tests/gpu/compare_reference.py NEW_DIRECTORY --clipping` admits the complete
fixed profile before rendering: exact depth-bit positions, camera, transforms,
scale, opacity, color/SH layout, background and all six contributions. Real and
clipping flags cannot combine. Existing synthetic and real profiles are unchanged.
Reports retain ordinary PSNR/SSIM/max-error results and twelve presence checks,
one for each region in each renderer. Outside regions must stay within one RGB8
level of background; each on/inside region must differ by more than 32 levels.
These structural checks prevent background-only agreement from passing; global
diagnostic tolerances remain PSNR ≥45 dB and maximum error ≤8 levels.
[Clipping-boundary evidence](../validation/2026-10-15-clipping-boundaries.md)
records the tested compiler/device scope. No real reconstruction or Q gate follows.

## Calibrated reference camera selection

`we_gpu_reference NEW_DIRECTORY --real BUNDLE COMPLETED_FIT` keeps its original
first/middle/last camera selection and schema 2 output. Optional trailing
`--fit-indices 79,0,40` selects original **zero-based fit dataset indices**, in
that exact order. Selection is real-profile only: canonical decimal CSV, no
whitespace/signs/leading zeros, duplicates or out-of-range values. Bounds are
479 input bytes, 120 unique indices and 3–120 original frames. Every selected
calibration/extent is checked before SDL/GPU initialization. Inputs and completed
fits are read without modification; the output directory must be new.

Explicit output records `fit_frame_indices` and names images `fit_index_N.ppm`.
Every view retains the original frame name/index and calibrated camera. The first
selected camera determines the declared admission/render origin; changing order
may change float32 rounding without changing the scene or camera conventions.
Default output retains `fit_first`, `fit_middle`, `fit_last` and its prior origin.

`tests/gpu/compare_reference.py NEW_DIRECTORY --real --fit-indices 79,0,40`
requires the same explicit selection, order, count, names and original camera
correspondence. Missing/mismatched selection fails admission; no implicit subset
is accepted. Default fixtures require no selection flag. Reports include each
original frame index/name and its native image hash. Fixed diagnostic tolerances
remain PSNR ≥45 dB and maximum RGB8 error ≤8 levels; these compare calibrated
uncompressed renderers, not captured-image quality or Q6 packing fidelity.
[All-camera development evidence](../validation/2026-10-15-all-camera-reference.md)
records the actual tested scope. Native Vulkan/CUDA device access is required;
synthetic fixtures retain their labels and existing modes.

## Optional stage timings

`renderer_options.collect_timings` defaults off. It records CPU waits for frame
reuse, count timelines, acquisition and presentation calls, plus completed GPU
projection, eight-pass depth sorting, count/scan, tile indexing, composition and
presentation-draw intervals. Tile indexing includes emission, stable radix sorting
and range construction; presentation draw includes SDR/UI, not physical display.
Repeated/rejected count regions and accepted batches accumulate in their owning
render-frame sample. CPU wait and GPU intervals overlap and must not be added.

GPU collection requires the selected queue's nonzero valid timestamp bits, a
finite positive timestamp period, and graphics/compute timestamp support. Otherwise
CPU samples remain available and GPU fields are explicitly unavailable. Each of
two frame slots owns exactly twelve query slots, reused after existing completion
waits. Stage-boundary `ALL_COMMANDS` timestamps can perturb scheduling;
[writes](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdWriteTimestamp2.html)
and [64-bit reads](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetQueryPoolResults.html)
use validated queue capabilities. No query read adds `WAIT_BIT` or an extra
completion wait. Driver query-pool memory is outside the reported owned-buffer
count; query count stays fixed at 24 regardless of partitioning.

The renderer publishes one newest completed sample per frame slot, carrying its
submitted frame sequence and framebuffer extent. Idle/resize drains submitted
queries before slot reuse; paused/empty frames clear unused stages. Timestamp
deltas mask unsupported bits and account for one counter wrap; intervals must be
shorter than the device's counter range. Aborted draws do not publish samples.

`we_gpu_viewer_regression <scene> <new report.json> --timings` retains all 24
measured completed samples per phase after eight warmup frames, checks IDs/extents,
and reports p50/p95/p99 plus raw samples. Unsupported GPU measurements are null,
never a zero-time pass. `we_gpu_reference ... --timings` and
`we_gpu_smoke <output.ppm> --timings` exercise bounded-query reuse and paused/empty
lifetime behavior without introducing performance thresholds.

Tile-key sorting uses the validated maximum global tile ID to skip unused high
bits. Four-bit radix pass counts round upward to even pairs, preserving the
original ping-pong output buffer and stable depth/tie ordering. A zero-only key
needs no sorting. Positive float depth keys continue to use all eight passes.
This changes neither reference budgets nor admitted contributors.

## Source-view orientation

New fit publications store source-inspection schema 2 with a checked capture-up
reference. **Source view** / `--source-view` reproduces the recorded orientation
instead of removing its roll against arbitrary world +Y. Fly keeps this reference
after the reset; Space/Ctrl move along the chosen up direction, and Fit/bookmarks
preserve it. Without an explicit source reset, default navigation still uses +Y
world-up. Legacy schema 1 keeps the previous world-up reset. The UI distinguishes
world-axis and capture-orientation references; gravity remains unmeasured.

The [viewing-reference decision](../adr/009-capture-view-up.md) scopes this
user-requested option. No Gaussian/SH/pose rotation, floor inference or collision
activation occurs. Recompiling a completed fit with explicit `reuse_completed`
updates viewing metadata through normal atomic scene publication, skipping
optimization and preserving fitted assets.
