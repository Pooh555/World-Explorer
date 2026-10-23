# M3 selected dependency refresh — 2026-10-23 (Asia/Hong_Kong)

**VERIFIED: current selected Linux dependency evidence.** M3 remains
**IN_PROGRESS** pending its plan §8.3 software exit review. This refresh captures
current bytes and retains source/rights boundaries; it is not a complete SBOM,
release-vcpkg build, platform qualification or redistribution clearance.

Default cwd: `/home/Pooh555/programming/World-Explorer`. Parent: `eb626d3`.
Ignored evidence root **R**: `artifacts/validation/m3-dependency-refresh-20261023`.
The active [inventory](../../third_party/dependency-audit-m3-20261023.json)
retains every profile, component, comparison, boundary and gap from the
[Jolt inventory](../../third_party/dependency-audit-jolt-20261021.json).
The 26-file `R/final-identity.json` pins new public/raw receipts, changed checker,
helper scripts and exact tool binaries; its SHA-256 is
`afed46c00bb8642cedc68c8c2d959cdda6b9947c877a125b06109a657eb19cf2`.
Only viewer/CLI differ among the fifteen selected root bytes
(`selection-changes.json`). Jolt remains statically linked; its existing archive
SHA-256 `e7f9621e480646c434150e1fbe3a9410f4ec4b04ffe54791e0678326b741b918`
and exact MIT notice are checked by local admission. No dependency version,
warning, comparison or gap rule changes.

## Capture and checks

Dated helper scripts in R retain argument arrays, fixed child environments,
source hashes, exits and timings. All successful commands below exit **0**:

| Command / record | Actual result |
|---|---|
| `python3 R/capture_loader.py` → `loader-command.json` | New selection; 15 roots / 213 ELF files / zero unresolved edges |
| `python3 R/run_probes.py training sandbox`; `poses sandbox`; `metrics-cpu sandbox`; `training native` | Four fresh guarded import observations, no guard denials; no model/checkpoint, tensor allocation, optimizer or JIT compilation requested |
| `python3 R/observe_viewer.py native` | Owned empty RTX 4060 Laptop viewer: 900 frames / zero Vulkan errors / 47 mapped ELF files |
| `python3 R/prepare_summaries.py` | Five contexts / 208 mapped ELF files / 137 beyond loader baseline / zero changed overlap |
| `python3 tools/audit_binding_owners.py --mapping third_party/backend-bindings-m3-20261023.json --selection third_party/runtime-selection-m3-20261023.json --output R/owners.json` | Fresh 137-file / 16-group / 30-notice ownership and byte associations |
| `python3 R/prepare_inventory.py` | New owner/manifest summaries; 236 input bindings; retained profile/comparison/boundary equality asserted in `inventory-delta.json` |
| `python3 tools/check_dependency_manifest.py` | New default inventory passes |
| Same command with `--local-evidence` | Current raw receipts, ELF payloads, archive notices and fresh owner associations pass |

Native training observes CUDA available / one device / context not initialized;
sandbox training observes CUDA unavailable / zero devices. This difference is
execution context, not GPU absence. Existing three worker environments are
unchanged; no solver/media/model processing or upload occurs.

`--local-evidence --require-closure` exits **3**, retaining **257 open gaps /
86 components / seven profiles**, `rights_qualified: false`. The preceding
non-Jolt inventory had 253 gaps/84 components; the four Jolt gaps were already
present in the retained 21 October inventory. Old explicit 20/21 October
manifests still exit **2** on changed evidence. Their reports/raw receipts remain
historical and are not rewritten as passes (`historical-*.log`).

Four focused suites run with `python3 -m unittest discover -s tests/integration
-p NAME -v`: `worker_dependency_manifest_test.py`, `worker_runtime_audit_test.py`,
`worker_backend_bindings_test.py`, `worker_binding_owners_test.py`. All **65
cases** pass (11/16/23/15), recorded in `test-*.log`. The checker's only production
change selects the new default manifest. No C++ or reconstruction code changes.

The repository-pinned Black **26.5.1** is installed only in ignored workspace
tooling. All tracked Python files pass the required per-file command:

```sh
git ls-files -z -- '*.py' | xargs -0 -n1 python3 -m black --check --quiet --workers 1
```

Execution uses `PYTHONPATH=artifacts/tools/python-style-26.5.1` for that tool
only (`black.log`); global Python and worker environments are unchanged.

## Exact C++ tooling

The official [LLVM 23.1.1 release](https://github.com/llvm/llvm-project/releases/tag/llvmorg-23.1.1)
metadata and published digest are retained in `llvm-release.json`. The
1,986,848,633-byte Linux X64 zstd archive hashes to
`b7ddbabd70fa1d206948bc83f59e59aa84eaf4cd09b6b89cc3ece28177710a6f`.
`fetch_llvm.py` checks size and SHA-256 before publication; `extract_llvm.py`
selects only format/tidy, required libraries/resource headers and notices with
contained-path/type checks. Both tools report **23.1.1**, commit `6dfe167`;
global compilers and C++26/C++23 targets are unchanged. Earlier 23.1.3-only
reports retain their original limitation. The exact command exits 0:

```sh
PATH="$PWD/artifacts/tools/llvm-official-23.1.1/bin:$PATH" python3 tools/check_cpp_style.py include/world_explorer/collision/kinematic_capsule.hpp include/world_explorer/collision/triangle_cook.hpp include/world_explorer/scene_store/collision_support.hpp src/collision/kinematic_capsule.cpp src/collision/triangle_cook.cpp src/scene_store/collision_support.cpp tests/unit/collision_step_tests.cpp tests/unit/capsule_session_tests.cpp tests/fixtures/collision_fixture.cpp
```

This format/tidy gate covers nine collision/session C++ paths
(`style-exact.log`), including include-boundary/docs/whitespace checks. Both
formatting and warnings remain enforced. No formatting change was needed.
The five-item [M3 exit review](2026-10-23-m3-exit-review.md) now identifies
frame-quality selection as remaining software work; M3 does not close here.


Final documentation check exits 0: **1,177 local targets / 232 files**;
`git diff --check` and the 26-file identity recheck pass. Final logs are
`docs-final.log`, `whitespace-final.log`, `identity-check.log` and `black-final.log`.
Earlier source/rights, installed-header and unobserved-lazy-path boundaries remain;
no full Q/P/N/E or asset milestone is promoted.
