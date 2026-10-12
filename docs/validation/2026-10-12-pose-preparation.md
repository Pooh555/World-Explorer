# Durable pose preparation and coordinator evidence

2026-10-12, Asia/Hong_Kong. M1 remains IN_PROGRESS. Parent commit `ff7587c`.
Working directory `/home/Pooh555/programming/World-Explorer` (repository root).

## Scope

Shared retained-pose preparation publishes canonical assets through persistent
map admission before fitting. Restart retries operation 1 against the exact
source/decode/pose/optional-journal dependency at epoch 0/revision 1. The coordinator
holds map writer ownership through dependent scene publication. Fit camera views
and the complete registered inventory must match canonical poses; appearance
chunks retain conservative source-frame dependencies. GUI and CLI share this
coordinator. Fresh selected-frame solves now use the same durable publisher at
the sibling `NEW_JOB.map`; that post-solve connection was built but no additional
fresh solver was executed. Native map import now also accepts canonical records
in memory; file import retains the exact original byte/hash dependency.

No source-pointer repair, decoding, COLMAP solve, optimizer, model download or
GPU run was performed on the real capture. Scale remains arbitrary, with unknown
gravity and unavailable geometry/walking. Existing successful outputs and user
specification/runbook files remain unchanged.

## Checks

All final commands exit **0**, with **57/57** CTest cases in each local profile.
Arch Linux, GCC 16.2.1, Clang/format/tidy 23.1.1, CMake 4.4.4, OpenSSL 3.6.5,
Python 3.14.8, pinned FlatBuffers 25.2.10.

| Commands | Tested scope |
|---|---|
| `cmake --build --preset dev -j 4`; `ctest --preset dev --output-on-failure` | GCC C++26 viewer/CLI/core |
| `cmake --build --preset clang -j 4`; `ctest --preset clang --output-on-failure` | Clang C++26 |
| `cmake --build --preset compat-cxx23 -j 4`; `ctest --preset compat-cxx23 --output-on-failure` | Explicit GCC C++23 |
| `cmake --build build/ci-libcxx -j 4`; `ctest --test-dir build/ci-libcxx --output-on-failure` | Existing libc++ 23.1.1 / C++23 CPU configuration |
| `cmake --build --preset asan -j 4`; `ctest --preset asan --output-on-failure` | Native ASan/UBSan/LSan CPU |
| `cmake --build --preset tsan -j 4`; `ctest --preset tsan --output-on-failure` | Separate native TSan CPU |

Native sanitizer execution used authorized escalation. Three new fixtures and
extended coordinator sections cover restart/receipt/hash parity, exclusive
ownership, changed/advanced dependencies, corrupt/occupied/overlapping outputs,
stale-pointer explicit selection, cancellation after map commit, changed decode
dependencies before scene compilation, incomplete inventories and changed fit
camera views. Shared synthetic pose fixtures now supply checked metadata; these
are contract tests, not real reconstruction gates.

`clang-format --dry-run --Werror` passes on all 23 changed first-party C++ files.
`python3 tools/check_boundaries.py` and `git diff --check` exit 0.
`clang-tidy -p build/dev ... --warnings-as-errors='*'` exits 0 on
`src/application/{pose_pipeline,pose_preparation,pose_export,pose_solve,reconstruction_job,job_validation,scene_import}.cpp`,
`src/map_store/{map_store,map_persistence}.cpp`,
`src/reconstruction_adapters/completed_poses.cpp`, `apps/we_process/main.cpp` and
`tests/unit/{reconstruction_job,completed_poses}_tests.cpp`. The initial tidy run
found an unchecked optional in a test; the assertion now compares the optional
directly, and the full tidy and all six final build/test profiles pass. Logs are
ignored `artifacts/validation/pose-preparation-*`.

Source identity: SHA-256
`87dd76ab05132adcd019896a89a402147f40c158a102e3d97fa81ae55580ac9f`.
It hashes sorted `CMakeLists.txt` and all `.cpp`/`.hpp` under `apps`, `include`,
`src`, `tests`, concatenating UTF-8 relative path + NUL + binary file SHA-256
(93 files). Native `build/dev/we_process` SHA-256:
`b7676385423ea11771f07b210a3bf553f937948bc0b1e319cfae9c1a15cc389f`.

## Retained real capture

Source: ignored `data/local/sample.mp4`, SHA-256
`51263d9991dcbc2136d0809ecba1a8ba1b261896f3b222b7afba19784a153ef6`.
The original fit identity is
`0975ef8365bc402e3533a97c97087a425eb71b8cc0644691c8137f413dc235cb`.
The local request `artifacts/sample2-pipeline-20261012/reuse-original.json` selects
`reuse_completed`, `sample2-job`, its explicit retained
`decode-36c925841001fd1027759ac7dcab6451304305b9987c5e1da3e8ab1f5eaa9c6b`
stage, `sample2-fit-final`, 7000 steps/1280 px/2M cap, the above fit identity,
`original-map` and `original.wexplore`. No backend/Python paths are required.

```sh
build/dev/we_process reconstruct --request artifacts/sample2-pipeline-20261012/reuse-original.json
build/dev/we_process prepare-poses artifacts/sample2-native-poses-20261012-recovery data/local/sample.mp4 artifacts/sample2-pipeline-20261012/recovery-map
build/dev/we_process inspect-map artifacts/sample2-pipeline-20261012/original-map
build/dev/we_process inspect-map artifacts/sample2-pipeline-20261012/recovery-map
build/dev/we_process validate artifacts/sample2-pipeline-20261012/original.wexplore
python artifacts/validation/pose-preparation-real/verify.py
```

All exit 0. Reconstruction and preparation are repeated in separate processes;
map retry/publication and scene manifest hashes remain identical. Both maps have
63 observations/poses, one calibration and epoch 0/revision 1.

| Dependency | Original fit parent | Independent recovery |
|---|---|---|
| Canonical asset bytes | 19,880 | 19,960 |
| Canonical SHA-256 | `c99c5155464981409b2988dc36c68ef695707592c6975e000bd1517a9dc7d478` | `efac98bd6986f89c9dd3ec19cedffa65660d513376b243fb87489a2c52781ba2` |
| Checkpoint SHA-256 (19,824 bytes) | `a7a96deafef31dd12c7f1c36ff05dca2373bcf6838c76e910c230fe24aae0dba` | `488f7c50ecbbba524465c4ec4af7273984c2c2045c2a2f51681ddec009af9342` |

Original scene provenance retains pose completion
`cc144b5d655391172e02401cbc1c4da855e4bf77d3ed06113ad6c5d005714058`.
`reconstruct --request artifacts/sample2-pipeline-20261012/reject-wrong-map.json`
selects the original job/fit with `recovery-map` and exits **2**, refusing the
different canonical dependency before fitting or scene publication. Map and scene
manifests survive unchanged.

The independent Python check follows each scene's committed index (rather than
assuming its directory has no retained old assets), verifies chunk hashes and
compares metadata/transforms and splat payloads. All **304,867 splats** match
byte-for-byte; the new chunk adds 63 source frame IDs. Its 73,168,080-byte splat
payload SHA-256 is
`790caca73b368f90d893850bfaf4c9703af55b4da78d4900022498fe46b04893`.
Eight original pointer/result/input/scene manifests, including the stale original
decode pointer and independent recovery result, retain their saved hashes.
Requests, inspections, hashes and probes remain ignored under
`artifacts/sample2-pipeline-20261012` and
`artifacts/validation/pose-preparation-real`.

## Remaining scope

Typed bidirectional worker commands/control, full preflight and automatic fresh
video-to-pose-to-fit chaining are pending. Corrected-map refitting is not silently
adopted by this initial-fit profile. The ≤250k reference fixture, renderer parity,
corpus/metrics/geometry and native reference Windows/macOS/hardware qualification
remain outstanding. No new GPU performance, power-loss/ENOSPC, hosted CI or full
Q/P/N/E gate is claimed; M7–M8 remain deferred.
