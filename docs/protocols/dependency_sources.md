# Selected dependency source and notice gaps

The current dated [inventory](../../third_party/dependency-audit-20261015.json) separates
native development archives, three installed worker profiles, observed Linux
system/build packages, unresolved release-vcpkg declarations and metric models.
Each component retains version, source evidence, archive availability, notice
identity and explicit gaps. `rights_qualified` remains false. This is a selected
dependency inventory, not a complete SBOM, source build attestation or rights
clearance. Sources and gaps must be re-audited when selected dependencies change.

Run from the repository root:

```sh
python3 tools/check_dependency_manifest.py --manifest third_party/dependency-audit-20261015.json
python3 tools/check_dependency_manifest.py --manifest third_party/dependency-audit-20261015.json --local-evidence
python3 tools/check_dependency_manifest.py --manifest third_party/dependency-audit-20261015.json --local-evidence --require-closure
```

The default check needs only tracked inputs. It checks bound bytes, selected
worker package identities and completeness, native archive declarations and
release registry/version declarations. The local check additionally needs the
exact ignored raw receipts, development archives and selected metadata/notices
listed in `inputs`. Missing or changed evidence fails; no cache search, network,
installation, backend import or checkpoint loading occurs. Local checks validate
historical raw RECORD inventories against their hashes; they do not freshly hash
every installed payload or establish source provenance from installed bytes.

Exit **0** means the inventory is consistent, **2** means invalid/missing/stale
evidence, and **3** means `--require-closure` found open gaps. Inventory consistency
never upgrades a component's rights or an M1/Q/P/N/E gate. The checker writes no
files and cannot silently replace an earlier receipt.

Native archive hashes are checked against `cmake/dependencies.cmake`. A bounded
in-memory archive snapshot is rehashed before the selected notice scan, avoiding
a separate unchecked archive reopen. The scan never extracts filesystem paths;
it rejects duplicate/linked/oversized selected notices and limits expanded member
bytes. Retained notices must match the archive. The JSON notice explicitly records
one existing normalization: removal of exactly one trailing space on the first
line. Both raw and retained hashes stay visible; all other bytes must match.

Bounds: 16 MiB JSON/ordinary evidence, 32 MiB compressed archive, 1 MiB selected
notice, 50,000 archive members, 256 MiB declared expanded bytes, 256 evidence
bindings, 16 profiles and 256 components per profile. Duplicate JSON keys and
non-finite constants fail. Bound paths must stay inside the repository, including
after symlink resolution. Archive scans are limited notice checks, not package
extraction validation or a complete third-party source audit.

The supplemental [Triton 3.0.0 notice](../../third_party/notices/triton-3.0.0-LICENSE.txt)
is from commit `55a4ab051c88ba2baa031e520a339d3fded6468f`. One installed Python
file matches that tagged source exactly. This supports the selected-version notice
trace; it does not prove the wheel was built from that entire commit. The wheel's
missing installed notice, original archive/source binding, embedded NVIDIA tools
and headers, LLVM/AMD dependencies and transitive terms remain explicit gaps.

The release graph is a declaration at an immutable vcpkg registry baseline; it
has not been resolved or built here. Linux package metadata/notices do not qualify
that graph or other platforms. The observed FFmpeg enables GPL/version3 features,
so it cannot be represented as the proposed minimal LGPL release build. Metric
package, backbone and learned linear-weight rights remain separate from numerical
parity and their existing installed/downloaded byte identities.

The [selected runtime supplement](runtime_dependencies.md) now binds controlled
Linux loader contexts, ELF fingerprints/edges, package ownership/notices and
interpreter metadata. The same checker validates its tracked structure and,
with `--local-evidence`, raw receipt and current payloads. That local check streams
ELF files using separate 4-GiB per-file / 16-GiB aggregate limits. Lazy plugins,
actual Python imports/GPU execution, original source archives and rights remain
unqualified; this does not upgrade the earlier source/notice audit's scope.
