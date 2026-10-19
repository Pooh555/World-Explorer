# Owned fresh CUDA depth — 2026-10-20 (Asia/Hong_Kong)

M3 remains **IN_PROGRESS**. An isolated clean dense profile and owned depth
attempts now connect fresh CUDA MVS to native pose-bound cache admission, fusion,
scene publication and the existing GUI/CLI background handoff. This qualifies
the local nine-view development slice only: geometry remains fragmented,
arbitrary-scale and **3.34–9.89% valid per selected depth grid**. Full-sequence
coverage, measured alignment, collision/walking, Q5/N2 and A-track gates stay open.
The [depth contract](../protocols/dense_inputs.md#owned-cuda-depth-production) and
[fresh entry](../protocols/fusion.md#fresh-depth-through-the-shared-geometry-job)
own schemas, resource limits, recovery and commit semantics.

Cwd is `/home/Pooh555/programming/World-Explorer`. Parent `802dfb3`; ignored root
`R=artifacts/validation/m3-dense-owned-20261020`. `identity.json` pins source,
selected configurations and tested binaries. Linux x86-64 / GCC 16.2.1 C++26 and
explicit CPU C++23; backend CPython 3.12.15 / COLMAP CUDA 4.2.1. Native GPU is
RTX 4060 Laptop, driver 615.78.08, 8,188 MiB (`gpu-identity.txt`). No video decode,
pose recovery, training/refit, model download or upload ran.

## Profile reconciliation

The old installed-byte auditor exits **0**, recording six matching package
versions and **1,188 listed files**, but NumPy/CUDA runtime/cuRAND are
`UNVERIFIED_RECORD`. This corrects the prior handoff's expectation of a nonzero
exit. The receipt's integrity fields, rather than process success, expose **140
changed headers / 280 size-and-digest issues** (`old-installed.json`).

All six retained wheels match their original hashes and sizes
(`archive-verification.json`). Offline `uv venv` and `uv pip install --no-index
--require-hashes --only-binary :all: --link-mode copy` create
`artifacts/environments/dense-clean-20261020` from those local wheels. All six
packages and all **1,188 listed files** verify; no legacy/unlisted distribution
is present (`clean-installed.json`). Compared with the old installation, 140
headers differ, plus two relocated NumPy scripts and their RECORD. The old
installation remains unchanged. The
[selected identity](../../workers/identity/dense-development-20261020.json)
retains archive/receipt pins and explicitly leaves redistribution/rights open.

## Commands and verification

Exact command arrays, cwd, exits, timings and raw logs are under R in
`setup-commands.json`, `gpu-commands.json`, `adverse-commands.json`,
`native-commands.json`, `asan-real-replay-command.json`, `quality-commands.json`
and `checks.json`. Corresponding local drivers/sources remain there. Entry points:

```sh
cmake --build --preset dev -j 4
ctest --preset dev
cmake --build --preset compat-cxx23 -j 4
ctest --preset compat-cxx23
ASAN_OPTIONS=detect_leaks=0 cmake --build --preset asan --target we_core_tests -j 4
ASAN_OPTIONS=detect_leaks=0 cmake --build --preset asan --target we_process -j 4
ASAN_OPTIONS=detect_leaks=1 build/asan/we_core_tests 'geometry*,fresh geometry*'
python3 -m unittest discover -s tests/integration -p worker_dense_job_test.py -v
build/dev/we_process run-geometry "$R/fresh-request-current.json" \
  0d477af4004d68cddda22edab714a08af036abfe1a9125bfe216936e7071335c "$R/fresh-job"
```

Development CTest passes **379/379 / 116.32 s**; explicit C++23 passes
**355/355 / 108.30 s**. Six synthetic ownership/cancellation/recovery tests pass;
they skip explicitly on non-Linux hosts and certify no backend or geometry.
Native ASan/UBSan/LeakSanitizer passes **7 cases / 151 assertions** plus strict
real fresh-job replay. Only build-time discovery disables leak detection;
actual sanitizer runs use `detect_leaks=1` outside the sandbox. The scoped repo
style gate passes all five changed C++ paths with format/tidy **23.1.1**, unchanged
warnings and no overlay. Pinned Black **26.5.1** passes both new Python files.
The required full tracked-file formatter check retains **24 unchanged baseline
failures**, separately recorded; no existing worker/source recipe was reformatted.
An aggregated two-file Black check stalled and was cancelled; the required
individual-file command and both explicit new-file checks completed.

## Real pipeline and failure boundaries

All 63 calibrated frames prepare and pass native resized-input admission. The
frozen nine-view recipe changes only worker/resize implementation hashes after
intervening style edits (`recipe-differences.json`); dimensions, backend defaults,
filters, source rows and all numerical settings remain unchanged. The first test
template selected equivalent pose bytes at different paths; strict preflight
rejects it before ownership. The corrected template consistently selects the
current map pose path. No threshold or validator was relaxed.

Cancel during real CUDA execution returns **130** in **3.241 s**, kills/reaps both
observed descendants and retains **23 populated files** without a depth terminal.
Resume selects attempt 1 and completes depth → native cache → fusion → conversion
→ scene in **241.571 s**, including **210.576 s** MVS. Exact replay completes in
**4.677 s**, without launching CUDA or conversion. These are stage timings, not
whole-video/P1 qualification. Every retained partial file is unchanged.

All nine full depth/RGB grids and their calibration/pose/dependency records copy
exactly through native admission (`exact-depth-copy.json`, `audit.py`). The final
raw mesh has **32,074 vertices / 47,559 triangles**. Native viewer startup reuses
this fresh job after opening the complete saved appearance scene: **607 submitted
frames / zero Vulkan errors**. The real owned-state handoff diagnostic additionally
passes **238 frames / zero errors**, with identical prior pixels after failure
and cancellation (`native-handoff/report.json`). Inspected screenshots show
partial normal-color fragments and unknown gaps, with walking unavailable.

Cancelled replay after native depth, writer contention, changed environment
receipt/recipe bytes, corrupt admitted cache, a foreign output and explicit use
of the unreconciled old profile all reject. Committed scene bytes remain unchanged;
negative private fixtures are restored and final replay passes. Environment
mismatch rejects before backend launch even though the old audit exits 0.
The full local dependency check still exits **2** on changed existing evidence
(`dependency-current.log`); release dependency closure is unqualified.
`preservation-final.log` checks **24,457 protected files**, four original bakes
and 33 prior audit/bundle files unchanged, retaining the pre-existing **3,982
external header differences**. The new isolated profile does not qualify other
worker installations, hardware, platforms, distribution rights or full gates.
