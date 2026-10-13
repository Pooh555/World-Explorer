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
