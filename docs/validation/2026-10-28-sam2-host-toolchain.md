# Isolated SAM host-toolchain candidate — 28–29 October 2026

**A0/AP0 IN_PROGRESS; provisioning BLOCKED; A1 NOT_STARTED.** The user retained the
current volume and freed space. This audit observes **314,470,408,192 bytes** free;
capacity is not reserved and recipe fit is unmeasured. The separate
[19-artifact candidate](../research/sam2_host_toolchain_20261028.json) pins an
advertised GCC/G++13.4/sysroot2.28 graph around unchanged prior candidates.
Compiler payload members remain unread and no environment, solver, compiler or
activation script is installed/run. No AP/Q/P/N/E or platform gate is upgraded.

Cwd `/home/Pooh555/programming/World-Explorer`; started 28 October, completed
29 October 2026 (Asia/Hong_Kong); filenames/candidate date retain the start date.
Clean parent
`9ada4a758055b048d41999b57a9734e0f6309393`. Ignored
`R=artifacts/validation/a0-sam2-toolchain-20261028` retains scripts, raw registry
responses, selected `info` bytes and argv/cwd/exit/time receipts. Final proposal
SHA-256: `5d626a87b3a20f42548f6ac0e244fd3b12a9630002faabd39a323a30a6327de7`.

## Observed selection

| Scope | Actual result |
|---|---|
| Advertised graph | **19** exact URL/build/version/SHA/byte pins, **35** dependency edges, **11** virtual checks; actual glibc2.44/Linux7.2.11 satisfy selected predicates |
| Components | GCC/G++13.4, binutils2.46.1, GNU13.4 runtime/development libraries, sysroot2.28/kernel headers4.18.0, GNU OpenMP and supporting packages |
| Download subtotal | **150,510,658 bytes**; combined prior Python/model/source/interpreter/CUDA plus host packages **4,043,559,722**. Other tools/stages and complete installed/build/staging/scratch costs remain excluded/null |
| Embedded metadata | **19/19** selected `info/index.json` dependency/constraint tables agree with registry records. **653,195 successful range bytes** across retained complete observations; failed attempt stays separate |
| Declared payload | **4,152** unique paths/**950,786,485 logical bytes**, **102** softlinks, **124** prefix relocations, zero selected path collisions; no missing declared sizes; **17** notice records retained |

[Conda-forge compiler documentation](https://conda-forge.org/docs/maintainer/infrastructure/#compilers-and-runtimes)
distinguishes compiler/runtimes. [Package metadata](https://docs.conda.io/projects/conda-build/en/latest/resources/package-spec.html)
describes prefix files separately from `info`; declared paths are not measured
installed bytes, validated binaries or source/build correspondence. Patched
channel metadata, relocation/activation, glibc/libstdc++/Torch/NVIDIA bindings and
all independent source/advisory/terms/redistribution reviews remain open. No general
conda solver/VersionOrder conformance is claimed by the restricted metadata check.

[CUDA12.6 policy](https://docs.nvidia.com/cuda/archive/12.6.3/cuda-installation-guide-linux/index.html#host-compiler-support-policy)
allows newer minor releases within supported compiler majors. Registry GCC13.5
routes return **404**; GCC13.4 packages exist. [GNU13 upstream maintenance has
ended](https://gcc.gnu.org/gcc-13/), so downstream patch/advisory review is an
explicit blocker, not assumed from a recent package build or a license label.

## Commands, corrections and scope

Use `python3 R/run_check.py TAG COMMAND...`.

- `inspect_registry.py`, `inspect_registry_134.py`: exit 0; retain 13.5 absence and
  exact 13.4/sysroot responses. `resolve_toolchain.py`: first exit **1**, historical
  MD5-only OpenMP variants hit the SHA-256 guard. Excluding those rows before
  ranking and explicitly selecting GNU OpenMP gives `toolchain-resolution-complete-run`
  exit 0. No hash requirement is weakened.
- `inspect_info.py`: first exit **1** after **15** successes; four small packages
  use ZIP64 size fields. `--retry` exits 0 after bounded ZIP64 decoding, reusing
  rehashed successful records. Limits stay **2 MiB network/package/pass**, **1 MiB
  compressed info member /32 MiB decoded info tar /8 MiB selected record /1,024
  tar entries**. Exact 206/range/length/ETag/If-Match, ZIP CRC and bounded ZSTD
  checks apply. No full-package or compiler-payload fallback occurs.
- `freeze.py`, `finalize_info.py`, `oracle.py`, `info_oracle.py`: exit 0; raw
  registry pins, active dependencies/conditional constraints, host virtual facts,
  embedded metadata, paths, declared counts/sizes and notice hashes reconcile.

The restored-space baseline verifies the previous **378** source/product hashes,
**327** old sample_2 job files and **62** environment METADATA files unchanged.
Final checks pass: documentation **1,823 targets /336 files**, whitespace and
Black26.5.1 on all tracked Python. Audit helpers use host Python3.14.8; the proposed
3.12.16 worker interpreter remains unacquired. Inventory default/local/strict exits
**0/2/3**: 86 components/seven profiles/257 gaps, with stale runtime bytes still
reported and no receipt refreshed. Final preservation also verifies **775** prior
audit files unchanged. Of **993** tracked inputs, **988** are unchanged; five
Markdown edits and two new documentation/data files are intended. Exact receipts
are in `R/finish.json`. Production/specification/runbook and prior candidates remain
unchanged. JSON/Markdown changes require no native/C++ rebuild. The next executable
source/review/cost and capture-freeze work remains in [progress](../progress.md).
