# Replacement sample_2: baseline-only A1 intake — 28 October 2026

**Technical intake VERIFIED locally; A0/AP0 remain IN_PROGRESS.** The user replaced
`data/local/sample_2.mp4`, identified it for A1, then declared it **baseline-only**
and authorized estimated references. This permits an experimental estimated
route; it supplies no measured distance/up or separately captured backside,
empty-background/changed-light truth. Those real AP/Q5/N2 gates remain open.
A1 production has not begun. This intake did not modify the replacement, old jobs
or dependencies.

Repository root `/home/Pooh555/programming/World-Explorer` is default cwd;
timezone Asia/Hong_Kong; clean parent `1a7dc0973f515fe2513fcfccaf83f42da82fc29f`.
Ignored `R=artifacts/validation/a0-sample2-intake-20261028` holds source/authorization
records, preview commands/images, raw probe output, immutable inspection output
and proposed split. Commands use `python3 R/run_check.py TAG COMMAND...`, with
actual argv/cwd/exit/time receipts. No media/images are tracked or uploaded.

| Source | Identity / observed metadata |
|---|---|
| Replacement | **4,281,253,564 bytes**, SHA-256 `a5630370d7039c78cc822dbd17a967711be3632bd2cdb43519eab8c2fa876144` |
| Prior recorded source | `10a6405980025eadfbe12e4c68d86bbab8d41af24e0fa48f2d12ca829f0e1de3`; historical `artifacts/sample_2/source.json` and failed jobs remain tied to it, not the replacement |
| Container/stream | H.264 High, 3840×2160, 8-bit yuv420p, 30000/1001 FPS, 360.360 s; no audio stream; limited-range BT.709 primaries/matrix, `iec61966-2-4` transfer tag |
| Original timeline | time base 1/30000, stream start PTS1980; 10,800 declared frames/packets, **10,798 decoded frames**, first/last PTS1980/10809777, constant step1001 and constant decoded dimensions |

Ten 960×540 seek previews (5,45,…,325,355 s) were inspected locally. They show a
furnished room with floor/walls/ceiling/openings and chairs/tables; mirrors, bright
window/light regions and repeated textures are visible. This sparse review proves
no completeness, static/lens/lighting condition, actual calibration or quality
threshold. Preview images are inspection aids, never selected inference/evaluation
assets. Source ownership/distribution rights are not authenticated by appearance.

| Actual local check, exit 0 | Result / tag |
|---|---|
| Stable source hash and ten bounded preview extracts | `intake-run`; all preview subprocesses pass, original source hash rechecked |
| FFmpeg9.0.2 metadata and packet probes | `ffprobe-run`, `packet-probe`; raw stream metadata/packet PTS retained |
| Shared `we_process inspect --source … --job-dir R/inspection` | `coordinator-inspect`: source-bound inspect stage `78fc17e559b7e9e882f95623171f5b9e19364a8721aee82298e322a2088423b0`, 4.50 s; metadata admission only |
| Complete four-thread FFmpeg null decode with `-xerror` | `full-decode`: 214.50 s, empty warning/error log; no pose/appearance computation |
| Four-thread decoded-frame timestamp scan | `frame-probe`: 227.23 s; original decoded PTS/dimensions checked |
| Split generation and independent coverage/index oracle | `partition-complete-run`, `partition-oracle-run`; complete disjoint roles and exact candidate PTS, no calibration/image/pose refs invented |

The first split attempt assumed packet and decoded-frame timelines coincided.
It refused the source-start assertion: packets at PTS **-22/979** precede the
visible stream. The corrected proposal uses actual decoded order; no timestamps
were synthesized, shifted, or used to alter the video. The failed log remains.

## Authorized estimated route and frozen proposal

`reference-policy.json` retains the user's exact declarations. Scale stays
**ARBITRARY**; visual/geometric up or generated reference diagnostics may be
estimated later with explicit provenance. No estimates were synthesized during
intake. Such generated stand-ins cannot grade their own producer or become real
controlled-light/background evidence. Local use was authorized; distribution and
underlying ownership remain unverified. Model provisioning is separate.

`partition-proposal.json`, SHA-256
`8aae1c869100dda50aa9cea7a686ab466a4077935c3b36503735d0165d095466`,
divides decoded order into six consecutive blocks and withholds each block's last
sixth: **9,000 inference /1,798 holdout frames**. Content-independent midpoint/
quartile rules select **48/12 candidate views** spanning the full source. Previews
were seen before freezing; no solve/fit/tuning occurred. This is a within-source
temporal holdout, not a separately recorded inspection path. Candidate image
hashes/calibration/poses remain null; this is **not an admitted capture/corpus or
executable worker request**. The legacy 3 FPS selector would select **1,080**
frames against its 120 cap; no silent truncation/sampling/threshold change occurred.
At native 4K, all 12 observed/completed mask pairs would require **199,065,600
pixels**, above the current evaluation protocol's **64 Mi-pixel aggregate**.
Encoded image receipt sizes are unknown. `candidate-budget.json` records these
limits; freeze an explicit view/image/mask profile before admission rather than
assuming the candidate maxima fit together or silently resizing/truncating.

`preservation-final-run` confirms the new source stayed unchanged during intake
and all **327 old sample_2 job files** are unchanged. Of **378** historical
protected hashes, **377** remain identical; the sole difference is the user's
explicit live source replacement, pinned in `source-replacement.json`. Historical
receipts are not rewritten and recovery of the replaced original is not claimed.
All **986** tracked files were checked: six intended Markdown updates and **980
unchanged**, plus this new report. Production/specification/runbook are unchanged.
`docs-complete`, `diff-complete` and `R/finish.json` reconcile final records.
No solve, fit, completion, reference synthesis, model installation or A1 result
was produced; code/build/sanitizer reruns are inapplicable to this Markdown/data
intake. Next reconcile view/mask/receipt budgets and role-aware materialization/
admission alongside the pending recipe lock. Do not reuse the historical request
or report these source checks as A1 completion.
