# Native completed-pose import/replay — 12 October 2026

Parent `1716033`; existing Arch host/tool versions in progress. M1 IN_PROGRESS.
Scope: typed native import, estimator admission/events and atomic in-memory map
publication from existing output; no new pose solve, fit, GPU or native OS qualification.

Actual commands, all final runs exit 0:

```sh
cmake --build --preset dev -j 4
ctest --preset dev
cmake --build --preset clang -j 4
ctest --preset clang
cmake --build --preset compat-cxx23 -j 4
ctest --preset compat-cxx23
cmake --build --preset asan -j 4
ctest --preset asan
cmake --build build/ci-libcxx -j 4
ctest --test-dir build/ci-libcxx --output-on-failure
clang-tidy -p build/dev src/reconstruction_adapters/completed_poses.cpp src/reconstruction_adapters/completed_pose_replay.cpp src/reconstruction_adapters/worker_artifacts.cpp src/reconstruction_adapters/decoded_frame_source.cpp src/map_store/map_store.cpp src/application/job_validation.cpp src/application/pose_replay.cpp apps/we_process/main.cpp tests/unit/completed_poses_tests.cpp tests/unit/contracts_tests.cpp --warnings-as-errors='*'
python tools/check_boundaries.py
git diff --check
```

GCC/Clang C++26, explicit GCC C++23, isolated libc++ 23.1.1 C++23 and native
ASan/UBSan/LSan pass **37/37 CPU/worker tests**. clang-format/tidy 23.1.1 and
boundaries pass. Six new synthetic test cases cover calibration/half-pixel and
asymmetric optical transforms, corrupted/unknown/duplicate metadata and assets,
explicit stale-pointer selection, queue budgets, retry collisions, consumed
leases, immutable alias isolation, event/finish ordering, cross-thread cancellation,
distortion hashing and transactional rejection of wrong pose timestamps.
Synthetic fixtures do not certify reconstruction.

An automatic native sanitizer approval review timed out, then a retry was
approved. Sandbox build-time Catch discovery also triggered LSan's ptrace failure;
an initial subsequent CTest run listed the old 31 tests and is not used as current
evidence. The successful native rebuild/discovery and full 37-test run resolve it.

Real-source commands:

```sh
build/dev/we_process replay-poses artifacts/sample2-job data/local/sample.mp4
build/dev/we_process replay-poses artifacts/sample2-job data/local/sample.mp4 --decode-stage artifacts/sample2-job/stages/decode-36c925841001fd1027759ac7dcab6451304305b9987c5e1da3e8ab1f5eaa9c6b
build/dev/we_process reconstruct --request artifacts/sample2-reconstruct.json
build/dev/we_process validate artifacts/sample2-coordinated.wexplore
```

The first command exits **2**: the current mutable `decode.json` selects
`decode-1819fcc816a76dce95514ddaf7f36dc5c71336a7c763b6237f1b70db5bc72134`, whose
frame/calibration prefix is `31fa05e1c483228f`. The completed pose job and selected
video have SHA-256 `51263d9991dcbc2136d0809ecba1a8ba1b261896f3b222b7afba19784a153ef6`.
The importer correctly rejects that mismatch. The pointer is preserved.

Explicit selection of the retained matching decode stage exits **0**, producing
**63 observations / 63 poses / 1 OPENCV calibration**, epoch 0, revision 1,
arbitrary scale. Distortion coefficients are retained in typed records/map
snapshots. This reads existing PNGs through the native source, exercises capacity-one
backpressure/event draining and commits an immutable in-memory map snapshot.
No COLMAP or optimizer is rerun. Existing fit reuse/scene validation also exit 0:
optimization skipped, the existing 304867-splat captured scene remains valid.

Logs are ignored under `artifacts/validation/pose-*`; private input and original
stages remain local. No performance or aggregate-memory certification is claimed.
See [pose contract](../protocols/poses.md). Fresh native batch solving, canonical
FlatBuffers pose/map storage, complete control protocol and original reference/
platform/fixture gates remain unfinished. No milestone was promoted to VERIFIED.
