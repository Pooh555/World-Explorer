# Installed worker identity receipts

Run the [audit tool](../../tools/audit_worker_environment.py) with the selected
worker environment's Python and its exact requirement file:

```sh
.venv-train/bin/python tools/audit_worker_environment.py --requirements workers/gsplat/local-environment.txt --output artifacts/worker-audit/training.json
.venv-worker/bin/python tools/audit_worker_environment.py --requirements workers/pose-requirements.txt --output artifacts/worker-audit/poses.json
```

Each output must be new and inside ignored `artifacts/`; immutable publication
creates the receipt atomically and refuses replacement. The selected environment
is read-only. No backend module import, worker launch, checkpoint deserialization,
installation or network request occurs. `--alexnet-weights FILE` hashes only
explicit local bytes; it does not review rights or qualify the metric. No cache
search or implicit download occurs in the tool.

The standard-library audit scans `.dist-info` in the selected virtual environment's
purelib/platlib locations. It checks exact declared versions (including the local
version in a selected direct wheel filename), records unpinned/duplicate/missing
packages and reports uninspected legacy `.egg-info` metadata. RECORD-listed paths
must resolve inside that environment, including legitimate relative script paths.
Missing files, duplicate references, unsupported digest algorithms, path escape,
changing files/metadata or exceeded budgets abort without publishing a receipt.

Each listed regular file receives SHA-256/size; supplied SHA-256/384/512 RECORD
hashes and sizes are checked from the same file read as the inventory digest. Missing hashes on RECORD itself and bytecache are
permitted; other absent hashes or mismatches make that package unverified.
Missing RECORD is explicitly unverified. Metadata and notice identities remain
available even when a package cannot be fully checked. The installed inventory
digest is SHA-256 of sorted canonical JSON file entries; notice inventory uses
the same compact/sorted-key JSON encoding. Notices are identified by
license/notice/copying in the basename, not classified into a rights verdict.

Bounds: 256 distributions, 100,000 listed files, 4 GiB per file, 32 GiB of total
listed bytes, 16 MiB metadata/receipt, 4,096-character record paths. Cancellation
before publication leaves existing receipts untouched. A successful command means
the inventory was captured; callers must inspect requirement and integrity fields.

The [installed receipt format](https://packaging.python.org/en/latest/specifications/recording-installed-packages/)
allows absent RECORD hashes and installer-specific metadata. These receipts identify
listed installed bytes, not original wheel archives, all unlisted additions,
interpreter/OS libraries, a complete SBOM, source authority or redistribution
clearance. Hashes do not establish provenance independently of the installation.
Metric qualification always remains false in this audit: implementation, learned
LPIPS linear weights, AlexNet weights, rights and evaluation parity need separate
qualification. Full raw receipts stay ignored; the compact
[local development summary](../../workers/identity/local-development-20261013.json)
records hashes/version tuples without host paths or media.

## Metric policy and selected local bytes

[Metric audit](../../tools/audit_metric_environment.py) reuses the installed audit
and freezes specification §3.3's RGB, PSNR aggregation, SSIM window/constants,
TorchMetrics AlexNet/normalize/reduction/resize and evaluation-region/exposure
policy. It records package/inventory hashes, RECORD-checked LPIPS and backbone
implementation files, bundled learned linear weights, explicit SSIM source and
explicitly selected AlexNet bytes. It never imports backend code or loads a
checkpoint. Selected implementation files and requirements are rechecked after
the installed scan; changing inputs abort. No personal cache discovery occurs.

Required dependencies must be installed, exactly pinned and RECORD-verified;
unlisted distributions, legacy metadata and any unverified package block identity
readiness. Missing inputs publish `BLOCKED` with specific reasons. Complete bytes
publish `IMPLEMENTED_UNVERIFIED`: hashes alone cannot establish model architecture,
rights, numerical parity or dataset validity. `metric_qualified` always remains
false. `--require-identities` returns 3 after publishing a blocked receipt; ordinary
receipt creation returns 0, input/publication errors return 2. Publication uses the
same 16-MiB bound and atomic no-replacement operation as worker receipts.

[CPU parity check](../../tools/check_metric_parity.py) requires a complete receipt,
checks its schema/policy/digest and current environment before backend imports,
and restricts tensor-only checkpoint loading to the two checksum-selected files.
Network connections are disabled. The explicit cache is selected with no fallback.
Independent feature traversal and linear-weight distances validate LPIPS; a 2D
Gaussian double-precision reference validates SSIM. Synthetic per-image and
normalization checks retain their own tolerances and do not replace Q thresholds.
Real dataset crop/mask/resolution, corpus and rights remain separate.
The [isolated profile](../../workers/metrics/README.md) and
[compact CPU receipt](../../workers/identity/metric-evaluation-cpu-20261014.json)
record current supported scope without altering training recipes/checkpoints.

## Development image-pair admission

The [image-pair tools](../../workers/metrics/README.md#retained-image-pair-evaluation)
pin a completed fit's complete contiguous temporal holdout, encoded image hashes,
RGB8 domain, dimensions, recorded bicubic target resize, common crop and explicit
absence of an observed-region mask. They reject partial/reordered/duplicate or
training selections, changed fit/source images and unsupported domains before
scoring. Every selected input is rechecked before publishing a new immutable
report. No fit, optimizer checkpoint, calibration, pose or scene is modified.

The shared metric loader now hashes and loads one bounded learned-weight
snapshot, using an 8-MiB memory spool that spills larger files to an automatically
closed temporary file. Path replacement after hashing cannot supply different
bytes to tensor loading. The selected-file whitelist, regular-file/4-GiB limit,
`weights_only=True` and offline network guard remain active. Synthetic mutation,
image admission, numerical and real retained evaluations have separate
[evidence](../validation/2026-10-14-holdout-evaluation.md). Original fit FP32
metrics and previous receipts retain their original identities/scope.
