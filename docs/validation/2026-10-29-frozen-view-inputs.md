# Frozen inference producer and native consumers — 29 October 2026

**A0/AP0 IN_PROGRESS; A1 NOT_STARTED.** The new explicit worker preparation copies
only frozen inference PNGs into ordinary immutable stages. Native replay checks
exact roles/inventory; batch-pose admission requires an independently selected
manifest pin. Real source frames reach the bounded pose-input writer. No solver,
geometry/material/light fit or asset production runs. Owned asset-capture/receipt
and complete evaluation-mask budgets remain unresolved, with unchanged thresholds.
The [protocol](../protocols/frozen_views.md) defines this pre-pose boundary.

Repository root `/home/Pooh555/programming/World-Explorer` is the command cwd;
29 October 2026, Asia/Hong_Kong. Clean parent
`07d7741504bd2dbe8482bce2a048ad6a76099386`;
ignored `R=artifacts/validation/a0-frozen-views-20261029` retains source identities,
requests/selection, stages, RGB files, scripts and argv/cwd/exit/time receipts.
`R/implementation.json` pins the final source files. No media is committed/uploaded.

## Real source connection

The unchanged replacement source SHA-256 is
`a5630370d7039c78cc822dbd17a967711be3632bd2cdb43519eab8c2fa876144`.
The [preceding materialization](2026-10-29-sample2-native-views.md) owns independent
source/PNG correspondence and all 60 original views. The new strict role manifest
SHA-256 is `10e8f500daeafff07fe69d794707c12e61114521e5474a8135dafe13bc6944f6`;
it retains that selection's roles, original indices/PTS, full temporal blocks and
4K grid. It is pre-pose input metadata, not an admitted asset capture/corpus.

| Actual operation | Result |
|---|---|
| Worker preparation | Exit 0, **23.646 s**; **48 inference** PNGs/**452,428,037 bytes**, plus exact selection/result metadata, total inventory **452,459,804 bytes**. All **12** excluded payloads stay outside the stage; all 60 originals retained |
| Native CLI replay | Exit 0, **40.837 s** overall/**34.710 s** reported frame replay; exactly **48** original frame IDs/PTS/4K RGB sample hashes match the prior independent source decode |
| Existing batch writer | Fixture exit 0, **31.424 s**; all **48** RGB leases consumed one at a time, **1,194,394,416 bytes** persisted as PPMs. Every raw RGB hash matches source. No `finish()`, sealed journal, worker launch or poses |
| Independent oracle | Exit 0; exact selected image bytes, complete stage inventory, IDs/PTS, RGB equality, excluded-payload absence and unsealed input-only output reconcile |

Stage `decode-2e869b6541f136e1cc66fabefcb79affac30ff9c09f3c9d2dfa5ccb83011c3bb`,
completion SHA-256 `381053cd8bf4498e2ee0ada7aa055f1b6f0d05844adc4051dffbccc768b9dd09`.
The preparation request explicitly limits 48 inference frames, four CPU threads
and 2 GiB worker output. Copying uses 1 MiB chunks; native replay retains the
existing bounded per-image leases and the batch writer uses its 64 MiB admission
budget/2 GiB disk journal. These are enforced scoped limits, not measured aggregate
process-tree/RSS/device fit. Scale is ARBITRARY; walking remains disabled.

## Commands, failures and verification

Use `python3 R/run_check.py TAG COMMAND...`.

- `python3 -B workers/worker.py run --request R/prepare-request.json` prepares the
  real stage; a repeat verifies existing immutable cached stages.
- `build/dev/we_process replay-frames STAGE data/local/sample_2.mp4` and
  `build/dev/we_frozen_input_probe STAGE SOURCE_SHA SELECTION_SHA R/batch-input-only workers/worker.py`
  exercise actual native source/batch APIs. The fixture deliberately stops before
  a solver; it is not a product reconstruction command.
- `oracle.py` reconciles the real bytes. CLI `solve-poses` checks with missing/
  wrong role pins exit **2** before creating outputs or launching a backend.
- Focused native checks pass: **2 cases/854 assertions**, including eight malformed
  role/order/source/path/inventory variants and read-only failure preservation.
  New Python checks pass: **7 cases** on host3.14 and existing isolated3.10/3.12.
  Worker regression: **289 cases, 25 skips** for unavailable optional backends;
  skips do not qualify those paths.

An initial formatting helper corrupted string literals; files were restored from
the clean parent and intended edits reapplied. A corrected formatter verifies
literal preservation. A fixture initially omitted its source hash, then used an
unparenthesized Catch INFO conditional; optional access also failed clang-tidy.
Those are corrected. Verification that overlapped file rewriting/relinking was
invalid (including a launch PermissionError); final checks run on frozen source
after completed builds. Earlier failed logs/helpers remain in `R`; no warning or
standard is weakened. Multi-file Black hit sandbox multiprocessing restrictions;
file-at-a-time formatting/checking uses the pinned formatter.

Final C++26 development/C++23 compatibility builds pass. Full CPU suites pass
**939/939** and **914/914** respectively. A final fixture-only safe optional read
was rebuilt and its two cases rechecked on both profiles; production hashes are
unchanged. Full scoped C++ format/tidy/repository gate passes for all seven changed
C++ files with **LLVM23.1.1**. Pinned Black26.5.1 checks pass for all tracked Python
plus both new Python files. Repeat preparation exits 0 in **5.040 s**, reusing the
same immutable inspect/decode stages and completion hash.

Documentation links, whitespace and preservation pass. **378** protected source/
product files, **327** old sample_2 job files, **62** environment metadata files
and **1,944** preceding audit files remain unchanged. Of **998** tracked inputs,
**983** are unchanged; 15 intended edits and five new source/test/documentation
files comprise this slice. Exact final receipts are in `R/finish.json`. Default/
local/strict dependency checks retain exits **0/2/3**: stale runtime payload bytes
and 257 source/rights gaps remain; no runtime receipt is refreshed. No model or
environment is installed, no thresholds change and no native platform/AP gate
is promoted. Next: owned asset-image storage and complete evaluation resource
handling, as recorded in [progress](../progress.md).
