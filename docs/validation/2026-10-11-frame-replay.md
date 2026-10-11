# Native decoded-frame replay — 11 October 2026

Parent `e3969bb`. Existing Arch host and tools described in progress; FFmpeg
`n9.0.2`. M1 remains IN_PROGRESS. No new pose solve, fit or GPU run.

Actual commands, all exit 0:

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
clang-tidy -p build/dev src/reconstruction_adapters/decoded_frame_source.cpp apps/we_process/main.cpp tests/unit/decoded_frame_source_tests.cpp --warnings-as-errors='*'
python tools/check_boundaries.py
git diff --check
clang-format --dry-run --Werror include/world_explorer/reconstruction_adapters/decoded_frame_source.hpp src/reconstruction_adapters/decoded_frame_source.cpp apps/we_process/main.cpp tests/unit/decoded_frame_source_tests.cpp
```

Each profile passes **31/31 CPU/worker tests**. GCC/Clang C++26, explicit GCC
C++23, isolated libc++ 23.1.1 C++23, and native ASan/UBSan/LSan are distinct
profiles; this is not a hosted Windows/macOS result. New synthetic fixtures check
lease lifetime, deterministic identities, rational PTS, reset/seek/EOF,
UTF-8 filenames, corrupt inventories/images/metadata, integer/timebase errors,
pixel/output budgets, retry position and cancellation of a sleeping decoder.
Format/tidy 23.1.1 and boundaries pass without weakening checks.

Real-source command, exit 0:

```sh
build/dev/we_process replay-frames artifacts/sample2-job/stages/decode-36c925841001fd1027759ac7dcab6451304305b9987c5e1da3e8ab1f5eaa9c6b data/local/sample.mp4
```

The source SHA-256 is recorded in progress. All **63** existing committed images
replay through `frame_source` as 3840×2160 RGB8 leases of **24883200 bytes**,
with PTS 0–620 at 1/30 and capture time 0–20666666666 ns. Final read-loop duration
is **18.3978 s**, including diagnostic RGB hashing; source open/video hash are
outside this timer. This is one CPU integration run, not a P1/P2 benchmark.
Some compilation/check jobs overlapped; no repeated timing distribution or
aggregate memory measurement is claimed. An initial replay before the decoder
allocation/rotation flags took 40.6683 s. A `/usr/bin/time -v` probe exited 127
because that utility is absent; the successful final command ran without it.

An independent Python `subprocess.Popen` invocation decoded the first/last PNG
with `ffmpeg -nostdin -hide_banner -loglevel error -noautorotate -i PNG -frames:v 1
-f rawvideo -pix_fmt rgb24 pipe:1`, hashing bounded 65536-byte stdout blocks. Both
exit 0 and produce exactly 24883200 bytes matching the native RGB digests:

- First: `9ad32f6458567de740a0d8934dad6524a34c561b0e0cf81ebe823dae2050a398`.
- Last: `5f6f8d0a899e4537ebcfeb3daa1b60be858af7e7cb5a3a1099c7893b9cb9883f`.

Large/private inputs, generated images and logs remain ignored; results are in
`artifacts/validation/replay-*`. The existing poses and completed fit are untouched.
See the [source contract](../protocols/capture.md) for limits and the legacy
source-prefix provenance limitation. Native batch pose admission, canonical
calibration/pose assets, durable maps and remaining reference/platform gates are
still unfinished. No milestone or acceptance threshold was promoted to VERIFIED.
