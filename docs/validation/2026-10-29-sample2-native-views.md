# Native sample_2 selected views — 29 October 2026

**A0/AP0 IN_PROGRESS; A1 NOT_STARTED.** All **48 inference/12 held-out** candidates
from the existing partition are materialized at **3840×2160**, with original
indices/PTS and exact PNG hashes. Their aggregate bytes fail the existing
capture/receipt resource preflight. No native capture admission, camera calibration,
pose solve, geometry/material/light fit or asset production has run. The user's
baseline-only estimated route remains authorized; measured scale, independent
reference passes and real A1 qualification remain open.

Cwd `/home/Pooh555/programming/World-Explorer`; 29 October 2026, Asia/Hong_Kong;
clean parent `05e88ca8cbe9c1192eec03f850e4dab0fa6a019c`.
Ignored `R=artifacts/validation/a0-sample2-views-20261029` retains PNGs, manifests,
FFmpeg logs, source/PNG pixel hashes, scripts and argv/cwd/exit/time receipts.
The new source SHA-256 remains
`a5630370d7039c78cc822dbd17a967711be3632bd2cdb43519eab8c2fa876144`.
The unchanged partition proposal SHA-256 is
`8aae1c869100dda50aa9cea7a686ab466a4077935c3b36503735d0165d095466`.
No video or generated image is added to Git or uploaded.

## Actual materialization and resource results

| Check | Actual value | Existing limit / result |
|---|---:|---|
| Selected views | 48 inference /12 held-out | 60 ≤64 capture views; no selection omitted |
| Native image dimensions | 3840×2160 | No crop or spatial resize |
| Largest PNG | 10,725,726 bytes | ≤16 MiB per image |
| Inference PNGs | 452,428,037 bytes | **Exceeds** 32 MiB per inference role group |
| Held-out PNGs | 111,636,346 bytes | **Exceeds** 32 MiB per evaluation role group |
| All PNGs | 564,064,383 bytes | Images alone **exceed** 64 MiB owned receipt aggregate |
| Evaluation views /pixels per mask | 12 /8,294,400 | ≤16 views /16 Mi pixels per mask |
| Projected complete evaluation masks | 12×2×3840×2160 =199,065,600 pixels | **Exceeds** 64 Mi pixels aggregate; masks not generated |

These are measured file sizes and contract arithmetic, not a run of native
capture admission with missing pose/calibration inputs. Existing bounds remain
unchanged. Images stay **UNADMITTED_NATIVE_SOURCE_VIEWS** under the ignored root.
The 48 selected inference views are not yet connected to a role-aware producer.
Both the 12 selected holdouts and every frozen held-out temporal block remain
excluded from future optimization; an unfiltered full-video legacy request would
violate that selection. No views are dropped or rescaled to obtain a pass.

FFmpeg software H.264 decode uses `select` on original decoded indices,
`format=rgb24`, `-copyts` and timestamp passthrough, then lossless RGB8 PNG encoding.
The source-declared BT709 limited-range matrix drives RGB conversion; primaries/
transfer metadata are retained in logs. This is decoded captured appearance,
without fitted exposure/color correction, physical color calibration or relightable
albedo. All selected PTS equal `1980 +1001×source_index`, time base `1/30000`.
Scale remains ARBITRARY; up remains UNVERIFIED. No measured walking is enabled.

## Commands and verification

Use `python3 R/run_check.py TAG COMMAND...`.

- `materialize.py`: exit 0, **219.825 s**. The source is hashed before/after; the
  partition is checked before extraction. FFmpeg receives an argument array,
  local file/pipe protocols, no overwrite, four codec threads, 600 s timeout,
  4 GiB address-space and 32 MiB per-file ceilings. All 60 outputs and selected
  showinfo timestamps/dimensions reconcile. The file ceiling is an extraction
  guard, not a widened native receipt limit; measured process peak is unreported.
- `check_png.py` through the unchanged fusion environment with `-B -I` verifies
  PNG checksums/format and decodes RGB samples without writing bytecode. This
  uses its existing Pillow11.3.0; exit 0, **25.272 s**, all 60 decode successfully.
  No new installation or reconstruction worker is invoked.
- `check_source_pixels.py` independently decodes the original selected video
  indices to raw RGB SHA-256 without PNG encoding; `oracle.py` compares every
  decoded PNG sample hash, original PTS/role/selection and resource bound.
  Source decode exits 0 in **195.306 s**; the oracle exits 0 with all **60/60**
  exact RGB matches, unique image hashes and four failed aggregate limits.

The companion [compiler audit](2026-10-29-sam2-channel-source.md) shares the clean
baseline and final checks. Documentation links pass: **1,840 targets/338 files**;
whitespace and pinned Black26.5.1 on all tracked Python pass. Final preservation
verifies **378** current source/product identities, **327** old sample_2 job files,
**62** environment metadata files and **945** preceding audit files unchanged.
Of **995** tracked inputs, **989** are unchanged; six intended Markdown changes
and three new documentation/data files form this slice. Exact receipts are in
`artifacts/validation/a0-sam2-channel-20261029/finish.json`. Production code,
specification, runbook and all prior candidates remain unchanged. No native/C++
rebuild or runtime receipt refresh occurs; the previously recorded runtime-byte
inventory gap remains open.

Next, resolve ownership/streaming and full evaluation-mask resource handling for
these unchanged native views, then connect the inference-only selection to the
existing consumers. Do not bypass current receipt bounds, overwrite prior jobs or
run an unfiltered whole-video request. The exact handoff is in [progress](../progress.md).
