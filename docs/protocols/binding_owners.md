# Installed ownership of newly observed libraries

[Current owner summary](../../third_party/binding-owners-repair-20261020.json) associates the
**137 `baseline_unseen` ELF files** from the [mapping supplement](backend_bindings.md)
with installed worker distributions or system packages. The preceding loader and
mapping receipts remain historical, independently bound records. This supplement
does not establish original archive/source identity, bundled-component terms or
redistribution rights, nor does it qualify stale overlapping system files.

From the repository root, using a fresh ignored output:

```sh
python3 tools/audit_binding_owners.py \
  --mapping third_party/backend-bindings-repair-20261020.json \
  --selection third_party/runtime-selection-repair-20261020.json \
  --output artifacts/binding-owners/run-001.json
python3 tools/audit_binding_owners.py \
  --summary third_party/binding-owners-repair-20261020.json
python3 tools/audit_binding_owners.py \
  --summary third_party/binding-owners-repair-20261020.json --local-evidence
python3 tools/check_dependency_manifest.py --manifest third_party/dependency-audit-repair-20261020.json
```

Capture requires Linux and the selected Arch package database. It hashes exactly
the new mapped ELF files and requires their recorded byte identities. Selected
worker receipts must match their original loader-receipt bindings. Each matched
ELF must have a verified RECORD hash/size; associated METADATA, RECORD and notice
files are checked against retained installed inventories. Current metadata must
name the expected package/version. No backend import, environment provisioning,
model/checkpoint read, source-cache search or reconstruction is performed.

System associations use the selected package tool's read-only
[`pacman -Q`](https://man.archlinux.org/man/pacman.8.en) operations: `-Qoq` for
owners, `-Qi` for declared metadata and `-Qlq` for installed license-file paths.
Queries use argument arrays, PATH/LC_ALL only, the existing bounded pipe runner,
and exact `--` target boundaries. Notices are restricted to installed paths under
`/usr/share/licenses`; declarations are recorded as metadata, not legal clearance.
An absent separate installed notice remains a gap. Every package query is repeated
before publication; changed associations/output fail. All selected inputs are
watched from before hashing through final publication checks, including the
mapping/baseline, original receipts, audit sources and package tool.

Default summary checking binds tracked source/mapping/selection bytes, complete
node/group/receipt inventories, mapped hashes, namespace/prefix associations,
notice bounds and retained source/terms gaps. Local checking additionally verifies
the raw receipt and performs a fresh read-only capture to independently compare
current ownership, ELF/metadata/notice bytes and package database associations.
Rehashing both raw/public copies cannot conceal a missing installed notice. The
raw receipt keeps query argv/output locally; the public summary keeps associations
and hashes. Existing output paths are never replaced.

Limits: **512 ELF files / 256 groups / eight worker receipts / 100,000 files per
distribution / 256 notices / 16 MiB aggregate notices**, existing **4 GiB per ELF /
16 GiB aggregate ELF** bounds. JSON/metadata evidence uses **16 MiB** bounds.
Queries inherit **2,048 commands / 15 seconds and 2 MiB output per command**.
Exit **0** means a scoped receipt/check completed; **2** means invalid/stale
evidence or capture/publication failure. All source/bundled-term gaps and
`rights_qualified: false` remain explicit.

The full dependency manifest binds this supplement, then checks historical loader
and mapping records separately. A successful owner check does not make that full
local check pass if old system paths/bytes have drifted. On 15 October, the old
snappy path is absent and zlib's bytes differ; the [evidence](../validation/2026-10-15-binding-owners.md)
retains those failures. Fresh runtime observations must be recorded before
superseding any affected active pins. No old failure is rewritten as a pass.

The [runtime refresh](../validation/2026-10-15-runtime-refresh.md) now supplies
those fresh observations. Active full local checking passes with its new records;
the old summaries and raw receipts remain available with their historical scope.

The [current audit refresh](../validation/2026-10-20-native-inventory-repair.md)
records 137 targets in 16 installed groups with 30 notices and freshly binds the
package-query-tool bytes. Current local recapture checks all associations. The
preceding [broader observation](../validation/2026-10-16-packing-runtime-refresh.md)
retains its 160-target/31-group/41-notice scope. Archive/terms gaps stay explicit;
no native platform or redistribution qualification follows.
