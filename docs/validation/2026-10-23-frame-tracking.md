# Persistent candidate tracking — 23 October 2026

Scope: M3 / plan §5.1 steps 2–4, Asia/Hong_Kong. **VERIFIED for the local
selection/admission/recovery slice; M3 remains IN_PROGRESS.** Visual lens-change
handling and general quality remain unqualified. Parent `a6e04ea`; repository
root is the cwd. Evidence root `R=artifacts/validation/m3-frame-tracking-20261023`.
`implementation.json` binds 13 changed code files, SHA-256
`3b7d7a63b4580ce98a840cdc87e082493f688741831b962369d121a15a01782f`.

## Implementation and development failures

The explicit `quality_keyframes_candidate_v2` profile carries bounded subpixel
point tracks, replenishes corners through connected intermediate observations,
and records reference/birth/renewal history for native admission. The selected
four-cell/1.5-pixel minimums remain; intermediate connectivity requires four
coherent points. Raw correlation, forward/backward checks and bounded affine
consensus reject unrelated matches. The viewer labels the version unqualified.
[Protocol](../protocols/frame_selection.md) owns semantics, including displacement
from each point's birth rather than inferred camera motion. V1 and timestamp
sampling retain their behavior; no existing manifest is migrated.

Three pre-release real attempts fail without publication. Their policies,
helper bytes, inputs and ledgers remain under R:

| Attempt | Failure / elapsed |
|---|---|
| Initial persistent reference | Point loss before keyframe renewal; 37.378 s |
| Birth renewal with four-cell intermediate requirement | Narrow-view connection refused; 36.590 s |
| Four-point connectivity with translation consensus | Four provisional keyframes, then spatially varying motion rejected; 62.728 s |

Synthetic turnover, narrow-view and camera-rotation regressions reproduce these
failures (`turnover-red.log`, `narrow-red.log`, `affine-red.log`). Filtering also
exposed false cut matches; raw-patch agreement fixes that development failure.
No Q/P/N threshold, source corpus or pose-registration requirement is reduced.
The final `candidate_2_2` policy is frozen before its real run in
`frozen-policy-affine.json`; policy SHA
`15dd3f26902aecee0a24daa66714afa048c0d5c14ae3e9c680da7a66b668589f`.

## Real local integration

Source `data/local/sample_1.mp4`, SHA
`51263d9991dcbc2136d0809ecba1a8ba1b261896f3b222b7afba19784a153ef6`:
**63 candidates → 24 selected**, 37 low-motion, one blur/texture and one initial
seed-candidate rejection. The fresh worker decode succeeds in **65.736 s**.
`real-selection.json` records all indices and identities; completion SHA
`56b1e696fd14b7a1aecf0eaa4655c3d0dcde4e6e48ca7d19b979954010e1d718`.

The standalone `decode_driver.cpp` calls actual shared `inspect_video` /
`decode_video` through framed workers into `shared-real`. Its fresh run and
completed restart exit 0; all 24 frame records/PNG hashes match the worker run
(`shared-parity.json`). Native `we_process replay-frames` exits 0. Shared decode
completion SHA:
`fe1a3a9c05fcc58aac37de652c97acca4c11b45bd7ded4c7eb673513d9c82de1`.

The sequential pose stage reuses the completed decode, then fails after
**90.608 s**, with 24 connected images/88 verified pairs but no valid initial
camera pair. Its incomplete stage and prior decode result remain. One bounded
recovery uses the unchanged 24 frames through native `we_process solve-poses
DECODE VIDEO R/guided-recovery --pose-python .venv-worker/bin/python --recipe
exhaustive_guided`. It succeeds in **456.257 s**, publishing **24 observations /
24 poses / one calibration** into `guided-recovery.map`; the pose stage takes
411.058 s. `guided-recovery.json` owns exact arguments/results. Registration
thresholds are unchanged; this attempt's one recovery is consumed.
`prepare-poses` reopens that durable map with four asset/manifest files byte
unchanged (`map-reopen.json`). Scale stays **ARBITRARY**, geometry **UNAVAILABLE**. Times above are single-run
stage observations, not reference-device performance qualification.

## Checks and preservation

| Actual commands | Result / log under R |
|---|---|
| `cmake --build --preset dev -j 4`; `ctest --preset dev --output-on-failure -j 4` | GNU 16.2.1 / C++26, 490/490 pass; `build-final.log`, `ctest-dev.log` |
| Build/test `compat-cxx23` with the same options | Explicit C++23, 465/465 pass; `build-cxx23.log`, `ctest-cxx23.log` |
| `.venv-worker/bin/python -m unittest discover -s tests/integration -p 'worker_frame_*test.py' -v` | Python 3.12.15, 25 cases pass; `worker-py312-affine.log`; system Python 3.14.8 cases also pass inside CTest |
| `build/asan/we_core_tests '[decoded_admission][quality],[decoded_admission][tracking]'` | Four cases / 12,908 assertions pass, no sanitizer reports; `asan.log`; native ASan/UBSan/LSan build/run, leak detection enabled |
| Exact LLVM 23.1.1 `python3 tools/check_cpp_style.py` on the nine changed C++ paths | Exit 0, `style-final.log`; formatting, tidy, boundaries and whitespace; no warning changes |

Pinned Black's prescribed per-file tracked check and explicit new-file checks
pass. Documentation/link and diff checks are recorded at finish. One native
sanitizer-build automatic approval review times out; the permitted retry succeeds.

**230 protected files are unchanged**, including all three videos, saved job JSON,
retained bake and v1 evidence. The 148 new completed/retained product files are
separately pinned. No fit, dense solve, fusion or bake runs; worker environments
are unchanged. Default dependency inventory passes; `--local-evidence` exits 2
because rebuilt runtime bytes differ. Existing 257 source/rights gaps remain.
Visual lens/zoom stability, 3D coverage, corpus quality, measured geometry/walking
and native platform/release qualification remain open. A0–A6 and M4 have not begun.
