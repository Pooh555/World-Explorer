# Viewer settings, camera and automated completion

The spectator is independent of capture poses. New/reset fly bindings follow
specification §4.8: W/S forward/back, A/D left/right, Space/Left Ctrl up/down,
Left Shift accelerates. Wheel scales speed; Escape releases capture. The controls
overlay names the active SDL scancodes, including remaps.

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
`has_pending_work()` checks both under that mutex. Automatic screenshot, next
startup reconstruction and `--frames` exit respect this pending handoff; renderer
visibility is separately gated by the update ticket. A frame limit is a minimum
before completion, not permission to capture/exit before a selected scene is
visible. Errors still fail explicitly. Manual navigation/input/HiDPI/latency and
other native-platform qualifications require their own evidence.

Viewer preference/handoff unit tests build only with `WE_BUILD_VIEWER` and
`BUILD_TESTING`. They require the viewer's SDL/ImGui dependencies but no GPU
execution. CPU-only presets retain their independent contract tests.

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
cutoff fixture; `--batches` selects 1,027 splats to exercise compaction/shared-memory
reuse across batch boundaries. Defaults and existing fixtures remain unchanged.
`tests/gpu/compare_reference.py` requires the pinned local training environment and
native CUDA; it never downloads/trains. Diagnostic JSON input is bounded to 4 MiB.
It preserves float reference images, PNGs, error heatmaps and per-view metrics.
Fixed diagnostic tolerances (PSNR ≥45 dB,
maximum eight 8-bit levels) compare quantized SDR output; they do not
replace Q/P gates. Commands/results and untested cases belong in validation evidence.

Splat keys sort positive finite center depths front-to-back, stably retaining input
order for ties. A 16×16 compute workgroup scans these keys in fixed 256-splat
batches, compacts tile overlaps stably into shared memory, and composites each
pixel in FP32. Tile bounds, half-integer samples, alpha clamp 0.999, minimum alpha
1/255 and exclusive `next_T <= 1e-4` termination follow the pinned gsplat recipe.
Termination skips that contributor and retains the preceding transmittance for
background composition. Out-of-frame lanes participate in all shared barriers;
a group stops only after every in-frame pixel terminates. No overlap list,
unbounded per-splat dispatch loop or silently dropped contributor is used.

Each in-flight frame owns a budgeted FP32 pixel buffer; dimensions, storage range,
dispatch limits and shared-memory capacity are checked. Timeline completion gates
reuse; device-idle resize retires old pixel scratch before replacement. Actual
Vulkan allocation requirements count toward residency. A fullscreen triangle
copies completed captured-domain colors into the FP32 attachment in subpass 0;
subpass 1 clamps once to SDR, then draws UI. No FP32 attachment blending support
is needed. `vulkan_overlay_context.subpass` remains 1. Compute-write/fragment-read,
input-read/store and per-image reuse have explicit dependencies.

This bounded development path still scans the global sorted list per active tile.
The specification's overlap count/exclusive scan/reference index and qualified
performance are pending. Center-depth ordering remains an approximation for
extended/intersecting Gaussians; these diagnostic passes do not certify full parity.
