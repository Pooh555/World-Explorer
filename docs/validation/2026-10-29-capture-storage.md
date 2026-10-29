# Native-image ownership and evaluation accounting — 29 October 2026

**A0/AP0 IN_PROGRESS; A1 NOT_STARTED.** Explicit v2 receipt/capture/evaluation
profiles preserve the full selected native grids. Shared immutable image handles
retain exactly their own bytes, without copying or retaining the complete receipt.
V1 admission/storage limits and all AP thresholds remain unchanged. Actual image
storage is checked separately from synthetic calibration/pose/mask declarations;
no new-source solve, real capture/corpus, reconstruction or asset production passes.

Cwd `/home/Pooh555/programming/World-Explorer`; date/timezone 29 October 2026,
Asia/Hong_Kong. Clean parent `aca0a9f197c20c62ac2df0cb59d42a774d7fffbb`; ignored
`R=artifacts/validation/a0-capture-storage-20261029` retains source/output receipt bytes, scripts and argv/cwd/exit/time logs.
The independent 60-view selection remains in the unchanged preceding frozen-view directory.
Source identities and protection baselines are in `R/before.json`. Generated PNGs,
receipts and private media remain ignored and local. No installation/download occurs.

## Storage and isolation

The [receipt](../protocols/asset_receipts.md#explicit-capture-storage-profile-v2)
requires an independently selected CAPTURE_V2 profile matching schema 2, at most
768 MiB total and unchanged 16 MiB/file. It owns one allocation/blob; callers can
share only a selected blob or request an independent bounded copy. Capture v2
requires independently selected schema 2 and receipt v2, with 512 MiB inference /
128 MiB evaluation ceilings. V1 retains its 32 MiB/group limits. Repeated selected
frames count repeatedly against role limits even when they share one allocation.

[Evaluation v2](../protocols/evaluation_protocol.md#explicit-full-native-grid-accounting-v2)
requires matching root/mask versions and admitted receipt/capture v2. It accounts
for every pixel of both tracks with bounded RLE and a 256 Mi pixel aggregate;
v1 stays at 64 Mi. Per-mask/grid/view/byte/run limits, exact role/dependency
firewalls, exclusions and fixed metrics remain. This implements storage/accounting,
not a metric runner, real mask provenance or corpus/recipe freeze.

Admission retains the whole receipt payload in host memory. Shared images avoid
second capture/inference payload copies; this is not streaming to disk or an
enforced aggregate process-tree/RSS bound. Ordinary corpus-inspection JSON remains
v1; request integration and real independently localized evaluation cameras remain
open. Walking stays disabled with arbitrary monocular scale.

## Commands and checks

Use `python3 R/run_check.py TAG COMMAND...`; exact logs remain under `R`.
The implementation adds regression cases for explicit version selection, oversized
declaration rejection before payload reads, exact role-budget boundaries, shared
allocation/lifetime, complete 12-view 4K masks, isolation and publication/reopen.
The focused development checks pass **42 cases /25,120 assertions**; the five
new v2 cases pass **834 assertions**. They include an exact 512 MiB logical
inference boundary using shared synthetic bytes, over-budget refusal and a
12-view 4096² aggregate-mask refusal. The 4K success case accounts for both complete
tracks, checks inference exclusion and publishes/reopens the exact v2 record.

Actual PNG bytes bind the unchanged replacement source SHA-256
`a5630370d7039c78cc822dbd17a967711be3632bd2cdb43519eab8c2fa876144` through the
[prior pixel correspondence](2026-10-29-sample2-native-views.md) and unchanged
selection SHA-256 `10e8f500daeafff07fe69d794707c12e61114521e5474a8135dafe13bc6944f6`.
`prepare_real.py` builds a byte-only diagnostic graph, using synthetic context
3/5 and a clearly unqualified import declaration; it is **not** a real pose or
asset capture receipt. All 60 PNG hashes remain independently selected.

- `build/dev/we_asset_receipt_probe SOURCE_RECEIPT RECEIPT_SHA NEW_OUTPUT capture-v2`
  admits, shares, hashes and publishes the actual **564,064,383 image bytes**:
  **452,428,037 inference /111,636,346 held-out** in the external frozen selection.
  Its receipt SHA-256 is `f79b8056938b83777e2dbbf5521f27bbe4d5d24493cd567bd4cd56f7861d7757`;
  61 payload files total **564,064,503 bytes**. The extra 120 bytes are the
  unqualified import declaration, not a recipe qualification or rights clearance.
- Initial publication exits 0 in **4.386 s**, single child process maximum RSS
  **579,200 KiB** (Linux `resource.getrusage(RUSAGE_CHILDREN)`, about 566 MiB).
  Replay exits 0 in **4.473 s**, **579,100 KiB**. These observations overlap CPU
  builds/checks; no AP timing, aggregate process-tree/resource fit or worker limit
  is inferred. A missing `/usr/bin/time` prevented the first wrapper attempt;
  the built-in Python resource measurement needs no installation.
- `oracle.py` exits 0: every original and published blob, exact closed output
  inventory, full role totals and full projected mask count reconcile. Only
  synthetic masks are admitted; no real eligible-surface mask is manufactured.
  Full native/corpus/recipe qualification remains open.

The first style run found a missing private JSON include dependency on the extended
receipt probe. The probe target now explicitly links the existing JSON dependency;
no public dependency or warning/standard is weakened. The next run reports an
escaping JSON exception from probe main; a function-level exception boundary
now returns failure. Production code and admission contracts are unchanged. The formatter's literal
preservation check initially rejected include reordering before writing that file;
it now checks its own brace rewrite against clang-format output. Initial build/style
logs are retained. The first build overlapped source formatting and is diagnostic;
final verification uses completed builds and frozen source.

GNU16.2.1 C++26 development (viewer enabled) and explicit C++23 compatibility
(CPU-only, retained `WE_BUILD_VIEWER=OFF`) builds pass. The completed development
suite `ctest --preset dev` passes **944/944** checks. After the final probe-only
exception boundary, its target and full development build pass; receipt/manual-action
crash/recovery checks pass **22 cases /3,641 assertions**, and real completed-receipt
replay exits 0 in **4.443 s** at **578,684 KiB** child maximum RSS. Production
sources are unchanged between the full suite and these final fixture checks.

`python3 tools/check_cpp_style.py` on all **13 changed C++ files**, with workspace
LLVM **23.1.1**, passes format, initialization/header policy, clang-tidy, include
boundaries, documentation and whitespace checks. Compiler modes and exact
commands/results are in `R/build-*.json`, `R/ctest-*.json`,
`R/cpp-style-sealed.json` and version logs.
`ctest --preset compat-cxx23 -j 4` passes **919/919** checks. Native GPU/viewer
execution and other platforms are untested by this slice.

Final preservation verifies **378** protected source/product files, **327** old
sample_2 job files, **62** environment metadata files and **1,944** preceding audit
files unchanged. `R/finish.json` records tracked/untracked changes;
`R/implementation.json` pins final code. Specification, runbook and AGENTS bytes
are unchanged. Default/local/strict dependency checks retain exits **0/2/3**:
local runtime payload bytes are stale and **257** source/rights gaps remain.
No runtime receipt or qualification status is refreshed. Final documentation,
whitespace and tracked-Python Black checks are retained under `R`. The first
Black invocation lacked its project-local module path; the final check explicitly
selects the existing pinned `artifacts/tools/python-style-26.5.1` with `PYTHONPATH`,
without installation or tracked Python edits.

Remaining: application request/profile integration, actual new-source pose/capture
and evaluation correspondence, real recipe/resource/rights freeze, producer work
and the native A1 completed/PBR pilot. No M4 work or milestone promotion occurs.
