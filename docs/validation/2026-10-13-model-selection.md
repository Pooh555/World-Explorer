# COLMAP model selection repair — 2026-10-13, Asia/Hong_Kong

Scope/status: investigate the same original video being rejected after fresh
reconstruction; M1 IN_PROGRESS. Parent `386879f`; implementation is the local
commit containing this report. Cwd: repository root. Native Linux development
profile only; no Q/P/N/E gate certification, source change, implicit download or
threshold relaxation. Input SHA-256 remains
`51263d9991dcbc2136d0809ecba1a8ba1b261896f3b222b7afba19784a153ef6`.

## Cause and correction

Inspection with pinned official pycolmap 4.2.1 found that the retained failed solve
`artifacts/sample_1/poses/stages/.poses-2zsy7q85/sparse` has model 0 with **four
registered frames / 167 points**, and model 1 with **all 63 selected frames /
29,846 points**. All four names in model 0 also occur in model 1; no selected
frame is unique to the small model. The worker incorrectly used directory count
as proof of disconnected coverage, rejecting the complete solution before export.
The earlier diagnosis that this source needed splitting/recapture was incorrect.

The fixed worker inspects actual registered frame identities. A unique strict
superset of every other candidate is eligible; equal ambiguous candidates,
incomparable/disjoint coverage and out-of-inventory images remain rejected.
The selected model alone supplies poses/calibration/training; no merge, frame
trimming or use of the small model's degenerate calibration. The existing 90%
registration threshold and all native finite/calibration/asset checks remain.
Selection metadata becomes an immutable stage asset.

## Recovery and checks

`python3 tools/recover_video_poses.py --request artifacts/sample_1/poses/batch-request.json
--failed-stage artifacts/sample_1/poses/stages/.poses-2zsy7q85
--output artifacts/sample_1/pose-recovery` actually completed. It verifies the
original committed decode/solver-input reference, validates a copied RGB journal,
and hashes retained sparse inventories into a new transactional job. It preserves
original assets and avoids feature extraction/matching/mapping. The new journal
conversion took **133.737 s**; selection/export/undistortion/publication **92.238 s**.
Canonical model 1: **63 frames, 29,846 points, 0.913462 px mean reprojection error**.

`build/dev/we_process replay-poses artifacts/sample_1/pose-recovery
 data/local/sample_1.mp4` exited 0: **63 observations, 63 poses, one calibration,
epoch 0/revision 1**. Checked retained inspection and completed recovered poses are
selected by `artifacts/sample_1/request-recovery.json`; original requests remain
unchanged. Sample 2/3 recovery requests adopt their checked retained video decode
and select separate fresh pose-job directories, respecting worker-source identity.

Six synthetic selection regressions cover reversed ordering, redundant subsets,
disjoint/partial overlap, equal coverage ambiguity, single-model threshold
ownership, and empty/unknown frame rejection. `python3 -m unittest discover -s
tests/integration -p 'worker*_test.py' -v`: **15/15** passed. `ctest --preset dev`:
**114/114** passed, actual elapsed 58.82 s. These are CPU/worker checks, not
synthetic reconstruction certification.

Raw records are local in `artifacts/sample_1/logs/model-recovery/`: selection
inspection, original-pose inventory, exact recovery/queue argv, worker tests,
CTest and native replay logs. Original model/data preservation is checked by
inventory hashes after integration. Recovery uses existing installed FFmpeg,
pycolmap and local trainer; no package/model download or remote fallback.

## Integration status

The sequential queue (PID 512423) actually started with the three
`request-recovery.json` files. Normal application admission accepted recovered
poses, prepared a durable map and started the unchanged **7,000-step** fit for
sample_1. Sample_2/3 are pending in that queue. Scene publication/native viewing
remain pending at this checkpoint; do not report completed reconstructions from
sparse poses. Current task: validate and view the completed sample_1 scene, record
actual terminal outcomes and preservation audit, then inspect subsequent queue
jobs without starting parallel processes.
