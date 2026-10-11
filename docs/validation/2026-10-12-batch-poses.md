# Native selected-frame COLMAP batch — 12 October 2026

Parent `734260e`; existing Arch toolchain/hardware identities are in progress.
Final changed-source bundle SHA-256 (sorted source paths and bytes, NUL-delimited):
`02d6d7f11111a7fd2d8a5ce7fc97373cbaad5dbe5711955fe0ecc1cc886e94b5`.
M1 remains IN_PROGRESS. Scope: native observation admission, isolated CPU solving,
verified worker events and in-memory map publication; no appearance fit, GPU,
reference performance or other native OS qualification.

Final local CPU/check commands:

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
python -m unittest discover -s tests/integration -p 'worker*_test.py' -v
clang-tidy -p build/dev src/reconstruction_adapters/batch_poses.cpp src/application/pose_pipeline.cpp src/application/pose_solve.cpp src/application/pose_replay.cpp src/reconstruction_adapters/worker_protocol.cpp src/reconstruction_adapters/decoded_frame_source.cpp src/reconstruction_adapters/completed_pose_replay.cpp apps/we_process/main.cpp tests/unit/completed_poses_tests.cpp tests/unit/worker_protocol_tests.cpp --warnings-as-errors='*'
python tools/check_boundaries.py
git diff --check
```

Five profiles pass **40/40 tests**, including the worker integration test with seven
Python cases. Native ASan/UBSan/LSan build/discovery and tests exit 0. Targeted tidy
23.1.1 exits 0. No warnings were weakened. Three new native test cases cover ordered
admission, memory/disk limits, occupied paths, preserved leases, retry collisions,
failure retention/idempotent finish and cross-thread process cancellation. Two
new Python cases verify actual RGB conversion, committed decode preservation on
missing solver and rejection of corrupted metadata/assets/uncommitted input.
Synthetic failure fixtures are not reconstruction passes.
The existing wire fixture also checks that a missing/modified completion asset
never reaches the typed completion observer. clang-format 23.1.1 dry-run passes on
all changed first-party C++ headers/sources.

Real-source commands (repository root):

```sh
build/dev/we_process solve-poses artifacts/sample2-job/stages/decode-36c925841001fd1027759ac7dcab6451304305b9987c5e1da3e8ab1f5eaa9c6b data/local/sample.mp4 artifacts/sample2-native-poses-20261012 --pose-python .venv-worker/bin/python
build/dev/we_process solve-poses artifacts/sample2-job/stages/decode-36c925841001fd1027759ac7dcab6451304305b9987c5e1da3e8ab1f5eaa9c6b data/local/sample.mp4 artifacts/sample2-native-poses-20261012-fixed --pose-python .venv-worker/bin/python
build/dev/we_process solve-poses artifacts/sample2-job/stages/decode-36c925841001fd1027759ac7dcab6451304305b9987c5e1da3e8ab1f5eaa9c6b data/local/sample.mp4 artifacts/sample2-native-poses-20261012-recovery --pose-python .venv-worker/bin/python --recipe exhaustive_guided
build/dev/we_process replay-poses artifacts/sample2-native-poses-20261012-recovery data/local/sample.mp4
build/dev/we_process validate artifacts/sample2-coordinated.wexplore
```

Source SHA-256 stays
`51263d9991dcbc2136d0809ecba1a8ba1b261896f3b222b7afba19784a153ef6`.
The first command exits **2** during conversion at frame 32. Independent ffprobe
diagnosis detects `libopenmpt`/audio on a valid PPM. Explicit image2/literal-pattern/
PPM input fixes the automatic probing error; the failed journal/stage is retained.

The second command exits **2**, preserving committed decode
`57d00ffd9b3b29329308176372c7a5a9f803b6d9895e2ea5354754d02e15332d` (93.536 s).
The CPU sequential solve returns models with 2 and 63 registered frames (31 and
14221 points respectively). The unchanged single-model gate rejects both as a
successful pose job; no final pose pointer/result is published.

Pillow independently decodes all 63 original and converted PNGs: their RGB digests
match every admitted journal digest exactly. PNG *file* hashes differ, so encoded
file identity is not substituted for pixel equivalence.

The explicit exhaustive-guided recovery uses a separate fresh journal/job. Its
PNG stage `fe56e03a9f668105d590690445d1de93a3dfadaa856ed14e3142a87e701095e1`
committed in 62.842 s. The full native admission/finish/event/map command exits
**0**: one connected model, **63/63 poses**, one OPENCV calibration, arbitrary
scale, epoch 0/revision 1. Pose solve/export/undistortion took **1162.089 s**.
These are per-stage times; native source replay/journaling and startup overhead
are additional. Final native completed replay and original scene validation also
exit 0. No gsplat optimizer was run.
The model contains 30294 points, mean reprojection error 0.921686 px; these do not
certify Q4 without ground truth. Journal SHA-256:
`cccaabe1b66c67565c7d5e759b6176fe8937a00724ecb098bdec07fb6f42c3d5`;
pose completion SHA-256:
`aa25dbf6dde45990216a4018caaf0ac30b0a19a22615446d327aac7ae7d282f6`.

The real-run native executable SHA is
`efdecc35bbe819df9beaaf501a9352184f72471ace563b74809e83f33245b3a5`.
This real run preceded the final shared Job initialization, failure-stage reporting,
schema strictness and completion-observer hardening. The solver/export algorithm
and admission path are unchanged; the final CPU suites and completed replay cover
the later code. Actual real-run worker/adapter hashes are retained separately.
Worker/adapter hashes, RGB parity and logs are ignored under
`artifacts/validation/batch-*`; stage commands/backend diagnostics are retained in
the job directories above. Existing successful `sample2-job`, its stale pointer
and completed fit are untouched. These per-stage development timings do not
certify P1 or reference hardware. M1/canonical FlatBuffers pose assets, persistent
map storage and complete command control remain incomplete.
