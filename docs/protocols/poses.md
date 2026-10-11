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
Fresh isolated COLMAP batch solving, canonical FlatBuffers pose assets, persistent
map snapshots and complete worker command control remain follow-up work.
