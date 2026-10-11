# Completed poses and estimator replay

`load_completed_poses` imports the existing COLMAP canonical JSON v1 profile.
It checks the selected source SHA-256 against the completed job, validates stage
pointers, verifies the complete pose inventory and used decoded images, and
cross-checks frames, timestamps, dimensions, calibration and registered counts.
Manifest bytes used for parsing are hashed against the verified inventory.
Changed completion/canonical metadata fails admission. Stage verification is
shared with the existing fit coordinator: bounded manifests/inventories, exact
file set, finite byte budgets, hashes, cancellation and no escaping paths/symlinks.
Local hashes detect corruption; they do not authenticate an arbitrary producer.

The supported legacy profile has one connected camera/model, epoch 0/revision 1,
at least 3 registered frames/10 points and at least 90% selected frames registered.
Camera models are PINHOLE and OPENCV; unknown models fail explicitly. OPENCV keeps
Brown-Conrady `k1,k2,p1,p2`. Backend half-pixel principal points must agree with
the integer-center canonical matrix. Uncropped/unresized observations have an
identity image transform. Calibration IDs remain immutable in map snapshots;
coefficients and image-transform provenance participate in operation hashes.

World-from-camera values are already canonical: right-handed +Y up, optical
camera +X right/+Y down/+Z forward, wxyz quaternion. Import does not rotate them
again. Submap alignment is identity; camera SE(3) scale is one. Overall scale is
explicitly ARBITRARY and gravity unknown. No covariance, geometry or measured
scale is invented. Source/frame/calibration IDs reuse the [capture mapping](capture.md);
submap identity uses kind `submap`, external ID `0` for this single-model profile.

The optional `decode_stage` selects a specific immutable stage in the same job
when the mutable decode pointer no longer matches completed poses. This selection
is explicit and still undergoes source/frame validation; there is no search,
fallback or pointer rewrite. A mismatched default pointer is an error.

`make_completed_pose_replay` implements `pose_estimator` for inspected completed
output. Capabilities declare `COMPLETED_REPLAY`, streaming false, covariance false,
and the configured admission capacity. This adapter does not run a solver or infer
poses from submitted pixels. Caller-supplied pixels belong to the declared capture;
the checked dataset supplies encoded-image provenance. Pixel hashes enforce retry
identity, not a claim of independent RGB-to-PNG equivalence.

Start is required and occurs once. Packed RGB8 leases, IDs, calibration/dimensions
and original/capture times must match the dataset. Unknown frames, malformed
leases, unsupported depth/IMU and excessive memory fail without changing input.
The configured memory reserves 256 KiB for bounded metadata plus the currently
admitted RGB lease. A full queue returns WOULD_BLOCK. Successful admission consumes
the pixel lease; retained caller copies stay alive. An operation retry returns its
original ticket; changed headers/pixels or another operation for the same frame
are rejected. Already-consumed retry headers can recover the original ticket.

Drain respects its event budget and releases queued observation slots only when
their pose/state event is returned. Initial events are INITIALIZING, submap-created,
identity alignment. Admitted registered frames emit the existing final poses;
unregistered selected frames emit LOST. Finish requires all selected observations,
closes admission and is idempotent. After queued events, FINISHED and the terminal
event are emitted once. Request-stop is thread-safe; other calls are serialized.
The adapter freezes its own snapshot so mutable aliases cannot alter emitted poses.

`we_process replay-poses POSE_JOB VIDEO [--decode-stage DIR]` uses the shared native
application command: real frame leases → capacity-one estimator replay with retry
on backpressure → one atomic in-memory map commit. Pose timestamps must match their
observation records. This inspection does not alter job pointers or persist maps.
Canonical FlatBuffers pose assets, persistent map snapshots and complete worker
command control remain follow-up work.

## Fresh selected-frame batch

`make_batch_pose_estimator` implements `BATCH_SOLVE`, streaming false, covariance
false, for the same inspected single-camera, uncropped RGB selection (3–120 frames).
It takes explicit local worker and pose-Python executables, a fresh job directory
and a sequential or exhaustive-guided recipe. There is no solver library in the
native process and no model download. Frame/calibration IDs and original times
remain tied to the declared video digest. This profile does not accept rigs,
changing lenses, depth or IMU; unsupported data fails explicitly.

Start requires admission capacity for the entire inspected selection. Batch
poses cannot free unfinished observations through streaming pose events, so an
insufficient capacity is rejected before creating output rather than allowing
an admission deadlock. Memory reserves 2 MiB for bounded metadata plus the current
RGB lease; the default input journal budget is 2 GiB, including a 2 MiB reserve.
These admission bounds do not certify aggregate native/worker peak memory or disk
use. Worker output/time/thread limits remain separate and explicit.

Submit validates the next selected frame, packed layout and timestamps. It
prepares receipt/metadata state, writes a canonical P6 RGB file through bounded
cancellable writes, closes/checks/hashes it and renames it before consuming the
lease. Failed admission leaves caller data and admission state unchanged.
Retries of an accepted operation return its original ticket, including consumed
headers; changed pixels/headers are rejected. No pixel buffer is retained by the
estimator. Individual accepted files survive estimator destruction for diagnosis;
the complete journal is published only after all selected frames are admitted.
This is atomic publication after closed file writes, not a power-loss durability
qualification.

Finish closes admission, publishes a hashed journal/request and synchronously
runs `solve-journal` through verified FlatBuffers worker events. The application
invokes this on its job thread. The worker checks every PPM/header/RGB hash and
ordered identity/time/dimension record, converts exactly those pixels to a checked
PNG stage, then uses the existing pinned CPU COLMAP solve/export/undistort path.
The converter explicitly selects image2/PPM and a literal filename: automatic
probing can mistake valid RGB bytes for an audio module. The PNG stage records
`native_rgb_journal_v1`, source SHA and journal SHA; it is readable by the same
native frame source/importer. Partial output is retained but never published as a
successful pose job. Error codes remain explicit; a failed finish returns the same
failure on retry without launching another process. Request-stop interrupts and
reaps the process tree from another thread.

Before completion, drain emits INITIALIZING once. After successful checked import,
it emits submap creation, identity alignment, final registered poses in capture
order, FINISHED and one terminal event. Drain bounds allocations by the finite
selection even for an excessive caller budget. Successful finish is idempotent;
the completed immutable dataset can be acquired independently of the estimator.

`we_process solve-poses DECODE_STAGE VIDEO NEW_JOB --pose-python EXECUTABLE
[--recipe sequential|exhaustive_guided]` uses a shared application command, computes
the selected video's full SHA, submits real native frame leases and publishes a
map through the same event-to-map path as completed replay. Worker script/Python
overrides remain available. This command performs a fresh pose solve and no fit.
Original completed jobs and fit outputs are independent of its fresh destination.
