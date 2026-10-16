# Selected Linux runtime dependency receipts

[Selection](../../third_party/runtime-selection-packing-20261016.json) pins fifteen local
ELF roots: viewer/CLI, FFmpeg/ffprobe, system Python, three worker interpreter
entries and seven selected worker extensions. The
[current summary](../../third_party/runtime-audit-packing-20261016.json) records normalized paths,
byte identities, DT_NEEDED/RPATH/RUNPATH/SONAME metadata, loader resolutions,
dependency edges, system ownership/notices and local interpreter metadata.
The full raw receipt and command outputs stay ignored under `artifacts/`.
The [source gap inventory](../../third_party/dependency-audit-packing-20261016.json) binds
this supplement; original wheel, source, model and rights gaps remain open.
The [renderer runtime refresh](../validation/2026-10-16-packing-runtime-refresh.md)
recaptures the rebuilt development binaries, loader contexts and native
empty-viewer mapping, including fresh installed owner associations for its broader
observed library set. The complete local inventory check passes; strict source
and rights closure still exits 3 for the recorded gaps. The preceding
[VMA receipts](../validation/2026-10-16-vma-runtime-refresh.md) remain historical.

Run from the repository root with a new output name:

```sh
python3 tools/audit_runtime_dependencies.py \
  --selection third_party/runtime-selection-packing-20261016.json \
  --output artifacts/runtime-audit/run-001.json --require-resolved
python3 tools/check_dependency_manifest.py --manifest third_party/dependency-audit-packing-20261016.json
python3 tools/check_dependency_manifest.py --manifest third_party/dependency-audit-packing-20261016.json --local-evidence
python3 tools/check_dependency_manifest.py --manifest third_party/dependency-audit-packing-20261016.json --local-evidence --require-closure
```

Capture requires Linux ELF64 x86-64, the selected GNU readelf/glibc loader and
Arch package database. It uses argument arrays and a fixed environment containing
only PATH and LC_ALL; ambient LD_LIBRARY_PATH/LD_PRELOAD/LD_AUDIT are excluded.
The glibc loader's documented
[`--list` mode](https://man7.org/linux/man-pages/man8/ld.so.8.html) records dependency
resolution. It does not start the inspected application. Each extension's explicit
library-path context is recorded separately; it does not reproduce Python's full
import order, previously loaded libraries or module initialization.

GNU [readelf](https://sourceware.org/binutils/docs/binutils/readelf.html) reads
metadata without a backend import. SONAME aliases identify already observed
loaded objects, including the loader printed using an absolute alias. A basename
match never substitutes for an unresolved direct dependency pathname. RPATH and
RUNPATH remain distinct in receipts; the auditor delegates resolution to glibc
rather than implementing a competing loader search algorithm.

Current loaded worker ELF hashes must match bound RECORD entries; roots must
match the explicit selection. System files are mapped through read-only package
queries, retaining version, declared source URL/license and selected installed
notice hashes. Interpreter pyvenv configuration, BUILD stamp and license bytes
are associated with their selected binary directory. These metadata associations
do not establish original interpreter archives or bundled-component rights.

Limits: 32 roots, 512 distinct ELF nodes, 4 GiB per file, 16 GiB aggregate ELF
bytes, 2,048 commands, 15 seconds and 2 MiB output per command, 256 notices and
16 MiB aggregate notice bytes. Pipe reads stop at the output bound; timeout or
excess output kills/reaps the owned inspection child. Root/tool/receipt hashes and
node/notice file identities are rechecked before immutable publication. Paths
must resolve inside the repository, `/usr` or `/opt/cuda`; no cache discovery,
installation, training, video processing or model deserialization occurs.

Capture exit **0** means a receipt was published, **2** means invalid inputs or
inspection/publication failure, **3** means `--require-resolved` found unresolved
selected-context dependencies. Existing outputs are never replaced. The default
manifest check verifies tracked selection/auditor identity and complete root,
alias/edge structure. Its local mode additionally verifies the raw receipt, all
213 currently recorded ELF bytes, system notices and interpreter metadata. A stale
build or environment requires a new audit; do not update pins solely to erase a
failure. `--require-closure` still fails on original source/rights gaps.

These are controlled startup and extension inspection contexts. Vulkan ICD/layer,
SDL platform, OpenSSL provider, codec/plugin and CUDA driver lazy loading, Python
module execution, generated/JIT kernels and actual GPU work remain unobserved.
No complete SBOM, release-vcpkg, other-platform or Q/P/N/E certification follows
from a resolved graph. Missing libraries in a standalone extension inspection
are context failures; they do not establish GPU absence or a failed saved fit.

The separate [observed mapping supplement](backend_bindings.md) now checks fixed
backend imports and an empty native viewer. It retains its own execution contexts,
raw/source identities and exclusions; it does not retroactively expand this
loader receipt or qualify every lazy path.

The [15 October refresh](../validation/2026-10-15-runtime-refresh.md) captures
current snappy/zlib bytes and package metadata with unchanged selected root pins.
The active manifest points to fresh loader/mapping/owner records. The original
14 October summary and raw receipt remain historical; checking them against
current system bytes still reproduces their recorded failure.
