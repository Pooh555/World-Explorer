# Candidate frame selection

`timestamp_sampling_v1` remains the default. Existing requests and completed
manifests retain their meaning. Explicit `frame_selection:
"quality_keyframes_candidate_v1"`, `"quality_keyframes_candidate_v2"` or
`"quality_keyframes_candidate_v3"` enables an experimental selector in a fresh
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
For v1, one minimum-eigenvalue corner per 4×3 cell is matched against the last selected
frame using mean-normalized 5×5 patches, a bounded ±6-pixel search, ambiguity
rejection and a forward/backward check. These are thumbnail image diagnostics;
they establish neither 3D coverage nor parallax, metric scale or lens stability.

The first usable frame seeds the selection; sufficient tracked displacement and
grid coverage select subsequent frames. Explicit rejection reasons are
`exposure`, `blur_or_texture`, `exposure_change`, `repeated`, and `low_motion`.
Insufficient tracks produce `tracking_break`; a large histogram change also
produces `suspected_cut`. Either stops publication, retaining diagnostics for
inspection. A tracking failure does not independently establish a camera cut.
For v1/v2, dimension metadata is the only lens-change detector. Thresholds and
these candidate recipes are not promoted defaults.

## Persistent v2 tracks

[`frame_tracking.py`](../../workers/frame_tracking.py) owns the current v2 policy
(`candidate_2_2`). It preserves the four-cell and 1.5-pixel selected-frame
minimums. A three-candidate initial window prefers spatially distributed
features; earlier eligible frames remain `seed_candidate` recovery observations.
Up to four separated corners per cell give at most 48 active points.

Integer patch search and bounded subpixel least-squares refinement run on a
3×3 binomial-filtered tracking image. Raw thumbnails still own image-quality
metrics and hashes. Raw patch correlation, forward/backward consistency and
bounded translation/affine consensus reject unrelated patch matches. Affine
consensus tests at most 128 deterministic non-degenerate triples; its residual
radius is 1.5 pixels and its required inlier ratio is 0.6. The refinement is a
project implementation of forward additive image alignment; see
[Baker and Matthews](https://publications.ri.cmu.edu/lucas-kanade-20-years-on-a-unifying-framework)
for the general least-squares framework, not validation of this recipe.

Four coherent **points** permit an intermediate reference to advance and admit
new corners. Selection additionally requires four image-grid **cells**, adequate
raw quality and sufficient tracked displacement. Narrow/low-texture candidates
can carry a connection without becoming keyframes. `insufficient_tracks` means
insufficient selection coverage; loss of a four-point connection on a usable
candidate is a `tracking_break`. Gaps are bounded to 12 candidates. No new seed
is created after a break.

Each point retains its birth candidate and coordinates. Displacement is the
median distance from those birth coordinates, with births reset at a selected
keyframe. It is not asserted to be displacement from the keyframe or net camera
motion. Point turnover connects intermediate observations; it does not prove
direct common landmarks between selected frames. Pose solving must still check
the unchanged registration/geometry criteria.

Each decision's `tracking` record binds its selected `anchor`, last connected
`reference`, continued-point birth interval and number of `new_points`.
Native admission follows that state machine, bounds available points and birth
indices, and rejects disconnected renewal, forward references and reseeding.
The viewer review displays the selected candidate version and **unqualified**.

## Conservative v3 calibration preflight

[`frame_calibration.py`](../../workers/frame_calibration.py), policy
`candidate_3_0`, observes the same v2 point correspondences and preserves its
selection decisions. It fits a centered least-squares 2D similarity between
connected observations. Assessment requires at least eight points in four
source-image cells, spans of 40×24 analysis pixels, scale in [0.5, 2], and RMS
residual at most 0.5 pixels. The scale ratio is symmetric: expansion and
contraction both trigger at a pairwise ratio of 1.04, or a connected cumulative
ratio of 1.1. Thresholds are frozen before the real development run.

An alarm is **ambiguous**, not an identified lens change. For a frontoparallel
plane, pinhole image scale depends on focal length divided by depth: zoom and
forward camera motion can produce identical pixels. Other camera/scene motion
can also trigger this conservative test. Any alarm raises `CALIBRATION_REQUIRED`
before RGB publication or pose solving; partial thumbnails/ledger remain. No
new calibration group is invented. Automatic lens segmentation and calibrated
multi-lens admission are unsupported by this profile.

Each decision adds `calibration`: reference, support point/cell counts, spans,
fitted scale/RMS, chain start and cumulative log scale. Statuses are
`unassessed` before a tracking reference, `insufficient_support` when assessment
conditions fail, `assessed` when the 2D test has no alarm, or
`ambiguous_scale_change`. A chain extends only through an assessed reference;
unsupported intervals do not certify intrinsics. A completed v3 job may contain
unsupported intervals. Neither `assessed` nor absence of alarms proves fixed
intrinsics, camera translation, 3D coverage or capture eligibility under Q1.

The native consumer checks support/count/reference bounds, finite values,
eligibility, chain arithmetic and both alarm thresholds. Rehashing a ledger
cannot admit an alarm under an `assessed` label. It validates declared diagnostic
consistency, not an independent refit from thumbnails. V3's implementation hash
is the SHA-256 of canonical sorted compact JSON mapping `frame_calibration.py`
and `frame_tracking.py` to their SHA-256 values (`implementation_files`). Both
files are pinned before/after analysis and protected from output overlap. V1/v2
retain their single-helper hash convention and exact policy digests.

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
