# Candidate frame selection

`timestamp_sampling_v1` remains the default. Existing requests and completed
manifests retain their meaning. Explicit `frame_selection:
"quality_keyframes_candidate_v1"` enables the experimental selector in a fresh
worker/decode job. Schema-2 reconstruction requests put this optional field in
`decode`; the viewer review labels it **unqualified**. No solver, measurement or
appearance qualification follows from successful selection.

## Bounds and analysis

The candidate profile accepts 2–4 timestamp samples/s, using original monotonic
PTS and time base. It considers at most `min(480, 4 * max_frames)` candidates,
retains 3–120 full-resolution RGB PNG keyframes, and fails rather than silently
truncating either budget. Display rotation matches the legacy decode. Selected
images remain uncropped/unresized; the separate analysis pass area-resizes to
96×64 GRAY8. Its scale/half-pixel offset and display dimensions are explicit.
Original IDs, PTS, capture times and the single `lens-0` identity are preserved.
Dimension changes fail and require separate calibrated captures.

[`policy()`](../../workers/frame_quality.py) owns the versioned, unqualified
thresholds. Analysis records Laplacian variance, mean/clipped luminance, corner
count, histogram distance, tracked image-grid cells and median displacement.
One minimum-eigenvalue corner per 4×3 cell is matched against the last selected
frame using mean-normalized 5×5 patches, a bounded ±6-pixel search, ambiguity
rejection and a forward/backward check. These are thumbnail image diagnostics;
they establish neither 3D coverage nor parallax, metric scale or lens stability.

The first usable frame seeds the selection; sufficient tracked displacement and
grid coverage select subsequent frames. Explicit rejection reasons are
`exposure`, `blur_or_texture`, `exposure_change`, `repeated`, and `low_motion`.
Insufficient tracks produce `tracking_break`; a large histogram change also
produces `suspected_cut`. Either stops publication, retaining diagnostics for
inspection. A tracking failure does not independently establish a camera cut.
Visual lens changes remain unresolved; dimension metadata is the only lens
change detector. Thresholds and this candidate recipe are not promoted defaults.

## Provenance, admission and failure

`candidates.gray` retains all bounded analysis thumbnails in timestamp order;
original source IDs allow a future recovery recipe to decode additional images
from the pinned local video. `quality.json` records every candidate's identity,
metrics, reason and selected flag, plus policy/implementation hashes, profile,
rate and analysis transform. Selected rows also bind their final PNG path/hash
and exactly equal the published `frames` projection. Rejected rows claim no
selected PNG. Timestamp-grid exclusions are represented by the sampling policy,
not additional quality decisions for every source frame.

The result and completion inventory bind the ledger hash and raw thumbnail
bytes. Native replay checks the ≤1 MiB ledger, supported policy digest, source,
candidate bounds/order/PTS/calibration, analysis-grid/PNG dimension binding,
finite metric ranges, selected-policy
conditions, selected projection and each thumbnail's hash. Regular contained
files are required. Ordinary PNG/frame admission still applies. The shared
decode command also pins the helper bytes in owned parameters and rejects a
different returned selection profile, implementation or sampling rate.

Analysis polls cancellation between candidates/tracks and enforces the stage
deadline; FFmpeg retains its existing process/output limits. Failure never
publishes a decode completion or replaces the previous terminal result. Partial
thumbnails/ledgers remain in ignored incomplete stage directories. Completed
reuse verifies the entire inventory. The legacy result/schema is unchanged.
