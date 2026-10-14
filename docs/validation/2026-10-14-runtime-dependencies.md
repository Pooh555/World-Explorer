# Selected Linux ELF runtimes — 2026-10-14 Asia/Hong_Kong

Scope/status: **M1/E2 IN_PROGRESS**. Controlled loader dependency receipts and
ownership/notice gaps; no source/rights, GPU, other-platform or full-gate
qualification. Cwd: `/home/Pooh555/programming/World-Explorer`. Parent `966596d`;
implementation is the local commit containing this report. Raw ignored evidence
`R = artifacts/validation/runtime-dependencies-20261014`.
[Selection](../../third_party/runtime-selection-20261014.json) SHA-256:
`b474a78226070b729fb64c38c42b36385f17510e725488f9058faee9be509cbd`;
[summary](../../third_party/runtime-audit-20261014.json) SHA-256:
`c08e86dab78ae10fd437fbf2b44d5f13969595bac5982d1f23bb5d034ffb6ca3`.
The extended [gap inventory](../../third_party/dependency-audit-20261014.json)
now hashes to `f376a03126185ae1f7085f6707ec4c3e9d142d104b6faa3a8353c35b80bd1aa0`;
the earlier report retains its original commit/hash scope.

## Observed contexts and limits

The selected glibc loader's documented
[`--list` mode](https://man7.org/linux/man-pages/man8/ld.so.8.html) supplies actual
resolution for a fixed environment containing PATH/LC_ALL. GNU readelf supplies
ELF metadata. No application entry point, Python backend import, video job,
optimizer, checkpoint or GPU operation runs. Worker extension library paths are
explicit inspection contexts, separately labeled from actual Python imports.

| Context | Recorded loader result |
|---|---|
| Native viewer / CLI | **13 / 11** resolved loader entries; viewer startup has no PyTorch/CUDA dependency |
| FFmpeg / ffprobe | **163 entries each**, exposing the broad installed system graph rather than assuming the proposed minimal release build |
| System / training / pose / metric Python | **4 / 7 / 7 / 7 entries**; local pyvenv metadata identifies CPython **3.10.22 / 3.12.15 / 3.10.22**, with BUILD stamps and license-byte identities. Two unique standalone interpreter binaries remain source-unattributed |
| Seven worker extensions | **5–29 entries each**; selected gsplat/PyTorch contexts resolve the installed CUDA 12 wheel libraries. This does not observe CUDA driver initialization or Python import behavior |

Final receipt: **15 roots / 213 distinct ELF files / 4,586,883,942 bytes /
644 inspection commands / zero unresolved edges**. Ownership: **170 system files
from 123 packages, 39 RECORD-checked worker ELF files, two project executables and
two standalone interpreters**. **96 system notice files** and three interpreter
metadata/notice associations are retained by hash. The public summary omits raw
host argv/output; the full receipt stays local. Source/wheel archives, bundled
terms and rights remain open; no dependency or model is provisioned.

The [protocol](../protocols/runtime_dependencies.md) defines bounded pipe/process,
ELF/notice, path and publication checks. The existing manifest checker additionally
verifies complete root/SONAME/edge structure, exact raw-summary correspondence and
current ELF/notice/interpreter bytes. Original **249 component gap entries** remain;
they are separate from the supplemental runtime package gaps.

## Actual checks and failures

Arch Linux x86-64, GCC **16.2.1**, CMake **4.4.4**, GNU readelf **2.47**.
`R/run_checks.py` and `*-commands.json` bind exact argv, cwd, exits and source
hashes. `R/identity.json` binds additional receipt/generator/build identities.

| Commands | Exit / result / evidence |
|---|---|
| `python3 tools/audit_runtime_dependencies.py --selection third_party/runtime-selection-20261014.json --output R/runtime-final-checked.json --require-resolved` | **0**, all selected contexts resolve; `audit-final-checked.log`, immutable `runtime-final-checked.json` |
| `python3 tools/check_dependency_manifest.py`, then `--local-evidence` | Both **0**; full current ELF/notice/metadata hashes match; `manifest-{0,1}.log` |
| Same local check with `--require-closure` | Expected **3**; source/rights gaps remain; `manifest-2.log` |
| `python3 R/run_checks.py focused`; `workers` | **13/13 new / 92/92 worker cases** each on Python **3.14.8 / 3.12.15 / 3.10.22**, no local skips; corresponding `*-{0..2}.log` |
| `cmake --preset dev`; dev build `-j 4`; dev CTest | All **0**, GCC C++26 **260/260**, 62.00 s; `dev-{0..2}.log` |
| `cmake --preset compat-cxx23 -DWE_BUILD_VIEWER=OFF`; compatibility build `-j 4`; compatibility CTest | All **0**, explicit C++23 CPU **242/242**, 61.14 s; `compat-{0..2}.log` |
| Docs, boundaries, diff | All **0**; `static-commands.json` / logs |
| Repeat capture targeting existing final receipt | Expected **2** before inspection/publication; unchanged receipt hash; `existing-output-checked.log` |

The first observer incorrectly marked **62 edges** unresolved because glibc
printed its own loader using an absolute alias. `runtime-initial.json` and
`audit-initial.log` retain exit 3. Validated SONAME aliases repair that observer;
the regression also rejects substituting a basename for a different direct path.
The preceding standalone gsplat inspection lacks PyTorch's already loaded
libraries; its missing `libc10.so` is a context failure, not a failed reconstruction.
Corrected/final receipts resolve every selected edge. Final review closes an
identity-observation gap: the before-stat now precedes hashing and remains bound
through metadata inspection/publication. `hash-stat-gap-probe.json` reproduces
stale dependency acceptance using the previous-placement shim; the corrected
observer rejects it. Synthetic tests check
mutating roots/dependencies, malformed/count/byte/path limits, bounded child
output, literal argv, missing dependencies, metadata association, omitted graph
entries and independent raw/current-byte validation. A Linux-only pipe test is
explicitly skipped on other OSes; no such platform run is claimed here.

All **2,088 protected files** match the previous slice's current inventory; all
**94 tracked src/include files** match the parent. No saved run/model/recipe or project schema, target or coverage scope changed;
no environment provisioning occurred. C++ formatting/tidy/sanitizers and
native GPU/UI were not rerun for this Python audit slice. Concurrent test timings
are not performance qualification. Actual import order, lazy driver/ICD/provider/
codec loads, JIT/build dependencies, rights and native/reference gates remain
unqualified. Next work is in [progress](../progress.md).
