# Canonical pose/calibration asset evidence

Date: 2026-10-12, Asia/Hong_Kong. Active M1 remains IN_PROGRESS. Parent commit:
`6e1e47d`. Working directory: repository root
`/home/Pooh555/programming/World-Explorer`. The committed source inventory/identity
is recorded below; generated bindings use pinned FlatBuffers 25.2.10.

## Implemented scope

Native canonical FlatBuffers v1 (`WEPA`) writer/reader with immutable snapshot
ownership, JSON inspection and shared application/CLI completed-pose export.
Preserves double world/submap transforms, camera intrinsics/distortion and crop/
resize coefficients, stable 64-bit IDs, original PTS/time bases, nanoseconds,
revision/epoch, invalidations, optional covariance and source/decode/pose/journal
provenance. V1 has arbitrary scale and unknown gravity; unsupported measured
provenance is rejected. No backend coordinate conversion is repeated.

Readers hash the bytes parsed, verify bounded structure, versions/profiles/enums,
record counts, dependencies, duplicates, finite values, SE(3), covariance and
world/submap composition. Unaligned borrowed buffers are copied safely. Atomic
hard-link publication follows closed/flushed staging; existing files and concurrent
winners cannot be overwritten. The source and job remain untouched. No operation
receipts are stored: this is not durable map-store restart/revision recovery.

## Local checks

All commands below exit **0**; each profile passes **46/46** CTest cases. GCC
16.2.1, Clang/format/tidy 23.1.1, CMake 4.4.4, OpenSSL 3.6.5, Python 3.14.8 on
Arch Linux. These extend the previous 40 tests with four canonical-asset fixtures
and two retained-job/provenance fixtures. Generator cases exercise malformed
structure and rehashed invalid data independently of checksum mismatch.

| Commands | Profile / result |
|---|---|
| `cmake --build --preset dev -j 4`; `ctest --preset dev --output-on-failure` | GCC C++26 viewer/CLI/core; 46/46 |
| `cmake --build --preset clang -j 4`; `ctest --preset clang --output-on-failure` | Clang C++26; 46/46 |
| `cmake --build --preset compat-cxx23 -j 4`; `ctest --preset compat-cxx23 --output-on-failure` | Explicit GCC C++23; 46/46 |
| `cmake --build --preset asan -j 4`; `ctest --preset asan --output-on-failure` | Native ASan/UBSan/LSan CPU; 46/46 |
| `cmake --build build/ci-libcxx -j 4`; `ctest --test-dir build/ci-libcxx --output-on-failure` | Existing libc++ 23.1.1 / C++23 CPU configuration; 46/46 |
| `cmake --build --preset tsan -j 4`; `ctest --preset tsan --output-on-failure` | Separate native ThreadSanitizer CPU; 46/46 |

Sanitizer discovery/execution used authorized native escalation. Final runtime
edits were rechecked in all six profiles. The final comment-only clarification of
covariance conventions was followed by another dev build/test; declarations,
serialized bytes and runtime behavior are unchanged.

`clang-format --dry-run --Werror` on changed first-party C++ files,
`python3 tools/check_boundaries.py` and `git diff --check` exit 0. Targeted
`clang-tidy -p build/dev ... --warnings-as-errors='*'` exits 0 for
`src/map_store/pose_asset.cpp`, `src/application/pose_export.cpp`,
`src/reconstruction_adapters/{completed_poses,decoded_frame_source}.cpp`,
`apps/we_process/main.cpp` and `tests/unit/{pose_asset,completed_poses}_tests.cpp`.
Initial tidy findings were repaired with explicit expected-return budget checks,
reserved vectors, meaningful optional guards and complete mutation dispatch;
no warning gate was weakened. Logs: ignored `artifacts/validation/pose-asset-*`.

Fixtures cover asymmetric transforms and nonunit Sim(3) alignment, exact
round-trip bytes, IDs above 2^53, signed original PTS, OPENCV/crop preservation,
optional/singular/indefinite covariance, malformed offsets/truncation, future
schema/unknown enums, invalid dependencies/revisions/timestamps/world transforms,
record/byte limits, checksum mismatch, cancellation, input-buffer lifetime,
existing-file preservation, racing writers, failed publication cleanup, retained
synthetic-job export, source changes, and native journal identity mismatch.
Synthetic inputs are diagnostics only and never certify real reconstruction.

## Retained real capture integration

Source `data/local/sample.mp4`, SHA-256
`51263d9991dcbc2136d0809ecba1a8ba1b261896f3b222b7afba19784a153ef6`.
Used the existing successful single-model recovery
`artifacts/sample2-native-poses-20261012-recovery`; no COLMAP, FFmpeg pixel decode,
optimizer, GPU execution or downloads occurred.

```sh
build/dev/we_process export-poses artifacts/sample2-native-poses-20261012-recovery data/local/sample.mp4 artifacts/sample2-canonical-20261012/canonical.weposes
build/dev/we_process inspect-poses artifacts/sample2-canonical-20261012/canonical.weposes efac98bd6986f89c9dd3ec19cedffa65660d513376b243fb87489a2c52781ba2
```

Both exit 0. Asset: **19,960 bytes**, SHA-256
`efac98bd6986f89c9dd3ec19cedffa65660d513376b243fb87489a2c52781ba2`.
Reloaded immutable snapshot: epoch 0/revision 1, **63 observations/63 poses**, one
OPENCV calibration and one identity world/submap alignment. Arbitrary monocular
scale, unknown gravity; no geometry/collision claim. Preserved journal hash
`cccaabe1b66c67565c7d5e759b6176fe8937a00724ecb098bdec07fb6f42c3d5`
and pose completion hash
`aa25dbf6dde45990216a4018caaf0ac30b0a19a22615446d327aac7ae7d282f6`.

Independent Python comparison against retained worker JSON verifies **exact**
values for all 63 world translations/wxyz rotations, original PTS/time bases,
capture nanoseconds and encoded-image hashes; intrinsic matrix and four OPENCV
coefficients agree. No second half-pixel/rotation conversion. Repeated export
from the final runtime implementation produces identical bytes. Original recovery
result/pose/decode pointers, original completed-job result and both existing
scene manifests retain their pre-export hashes. Ignored evidence:
`artifacts/sample2-canonical-20261012/{verification,inspection,parity}.json` and
export logs. The source and assets are private and excluded from Git.

## Limits and continuation

Local native Linux CPU interchange/publication is verified. Native Windows/macOS
publication, power-loss/disk-full qualification, measured scale/gravity provenance,
durable map receipts/recovery, persistent worker command control and automatic
video-to-fit chaining remain pending. No Q/P/N/E gate, native reference profile,
full M1 milestone or M7–M8 work was promoted. Next: durable map snapshots/revisions
with checksummed asset-first, atomic manifest publication, faithful retry receipts
and failure/restart tests, using retained outputs without rerunning reconstruction.

Final first-party source identity: `7cff7b8a9dfc5b38423f61cf62adc5035946441073bdeb06d084f2cc0a96f7f1` (sorted changed
source paths + NUL + file bytes + NUL; inventory in ignored
`artifacts/validation/pose-asset-source-identity.json`). Final dev CLI SHA-256:
`4d85c688da8d7b7cf153804ef5417df25778bb79710cd1c5fab9149a1307ccad`. Its export to
`artifacts/sample2-canonical-20261012/canonical-current.weposes` and checked
reload both exit 0 and reproduce the same asset/inspection.
