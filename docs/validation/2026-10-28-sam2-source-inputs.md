# SAM source and build inputs — 28 October 2026

**A0/AP0 IN_PROGRESS; provisioning BLOCKED; A1 NOT_STARTED.** The
[addendum](../research/sam2_build_inputs_20261028.json) verifies the pinned SAM
source archive and selects interpreter/compiler archive metadata around the
unchanged 41-artifact candidate. No members are installed, no interpreter/compiler/
model payloads are acquired, and no SAM/model code is executed. Source verification does not
qualify builds, rights, runtime closure, resource fit or AP/Q/P/N/E gates.

Cwd `/home/Pooh555/programming/World-Explorer`; Asia/Hong_Kong; clean parent
`5d6072b9228ca01787a4812149753b1288e75514`. Ignored
`R=artifacts/validation/a0-sam2-source-20261028` retains policies, actual inputs,
publisher metadata, inventories, oracles and argv/cwd/exit/time receipts.
Addendum SHA-256:
`2161fe70d987bdb2b98ee6e25d9e4a538a33394d71f7c70fb039423fd484ab22`.

## Actual source verification

The exact [source commit](https://github.com/facebookresearch/sam2/tree/2b90b9f5ceec907a1c18123530e92e794ad901a4)
archive is **55,645,345 bytes**, SHA-256
`1f2fbfad3ffa38110368abac76c6ef9df9c282a66d5c2807bc94abf4d2fb30f8`.
Before transfer, the policy freezes **64 MiB compressed /96 MiB expanded tar /
8,192 members /32 MiB per blob**. The pinned tree has no `.pt`, `.pth`,
`.safetensors`, `.onnx` or `.ckpt` paths.
The bounded gzip expansion produces a **65,054,720-byte** tar; members remain
unmaterialized. Its **656 entries** contain 87 directories, **565 regular files**
and **four relative symlinks**. All 569 blobs/64,563,748 bytes and executable modes
match the retained Git tree. Link bodies also match Git blob identities; targets
are verified regular files inside the root. Hard links/escapes/duplicates reject.

An independent GNU tar1.35 listing and stdout-only payload stream reproduce all
**565 regular hashes /64,563,627 bytes** and all four link targets. No extraction
destination exists. HTTP tree/commit metadata plus Git blob SHA-1 correspondence
and local SHA-256 provide the stated identity boundary, not an independent signed
source or legal attestation. Source/cache bytes are not installed or job budgets.

## Build candidates and discovered blocker

[CPython3.12.16](https://www.python.org/downloads/release/python-31216/) is a security
release. The proposed [standalone20261026 build](https://github.com/astral-sh/python-build-standalone/releases/tag/20261026)
pins an exact **34,505,639-byte** install-only stripped archive and advertised
SHA-256. The existing 3.12.15 interpreter is observed with `-I -S -B`, unchanged;
no site packages are imported or bytecode written. All 41 selected package
requirements and 55 edges still agree for Python3.12.16 metadata; this is not a
new-interpreter execution or build-compatibility result.

The [NVIDIA12.6.3 redistribution manifest](https://developer.download.nvidia.com/compute/cuda/redist/redistrib_12.6.3.json)
pins candidate nvcc12.6.85, cudart12.6.77 and CCCL12.6.77 archives, together
**52,057,232 bytes**; versioned install/EULA documents are hash-bound. Payloads,
embedded notices, full dependency/installed correspondence and independent reviews
remain unverified. The known subtotal including prior Python/model pins and SAM
source is **3,893,049,064 bytes**; host compiler/sysroot/other tools/stages remain
excluded and whole-plan costs stay null, with no cache credit.

[CUDA12.6's policy](https://docs.nvidia.com/cuda/archive/12.6.3/cuda-installation-guide-linux/index.html#host-compiler-support-policy)
supports GCC6.x–13.2/Clang7.x–18.0, allowing newer minors within supported majors.
Observed GCC/G++16.2.1 and Clang23.1.4 are outside those ranges. Scoped GCC12/13 and
Clang18 PATH lookups find none. Ninja1.13.2/CMake4.4.4 have observed binary hashes,
not a selected complete build closure. No unsupported-compiler override, global
compiler change, environment upgrade or model activation occurs.

## Commands and remaining scope

`python3 R/run_check.py TAG COMMAND...` records actual commands.
`source_archive.py`, `archive_oracle.py`, `probe_local_tools.py`,
`select_build_inputs.py`, `check_python_target.py`, `freeze.py` and
`addendum_oracle.py` each exit **0**. No failed source/build audit is hidden;
unsupported host compiler and unbounded costs remain explicit blockers.

The project volume filled during receipt writing (**ENOSPC**), truncating this
new report and seven receipt JSON files. These are retained in `R/enospc/` and
are not passes. Only this turn's reproducible **65,054,720-byte expanded tar** is
removed; the verified compressed source and prior evidence remain intact.
Fresh recovered checks and storage observations are recorded in `R/finish.json`.
The compressed source hash is rechecked and bounded gzip expansion is verified
without another tar file. Later free space is about **0.9 GB**, not a reservation.
Recovered documentation links (**1,814 targets /335 files**), whitespace and
Black26.5.1 checks on all tracked Python files pass. Inventory default/local/strict
checks exit **0/2/3**: 86 components/seven profiles/257 gaps, with stale runtime
bytes still reported. Preservation passes for **378** source/product hashes,
**327** old sample_2 files, **62** environment METADATA files and **694** prior
audit files. Of **991** tracked inputs, **986** remain unchanged, with five intended
Markdown edits and two new documentation/data files. No runtime receipt is refreshed.
No production/C++ code changes or native build are needed for JSON/Markdown edits.
Both prior candidates, old jobs, private media and environments are preserved;
the next compiler/sysroot/cost and capture-budget work is in [progress](../progress.md).
