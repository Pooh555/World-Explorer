# Bounded full-sequence depth — 2026-10-20 (Asia/Hong_Kong)

M3 remains **IN_PROGRESS**. Owned temporal batches, independent native cache
unions and a streaming consumer extend the depth contracts beyond the experimental
18-view limit without enlarging a child cache's admission budgets. The real
63-frame run is currently **IN_PROGRESS**; full-sequence production is not yet
verified. No geometry, scale, walking, Q/P/N/E or asset-track gate is promoted.
The [production contract](../protocols/dense_inputs.md#bounded-full-sequence-depth)
and [native union contract](../protocols/depth.md#native-sequence-admission)
own bounds, dependency checks, recovery and compatibility.

Repository root is the default cwd. Parent `331e6b15e823`; ignored evidence root
`R=artifacts/validation/m3-depth-sequence-20261020`. `production-identity.json`
pins the executed coordinator/worker/preparation sources and selected request;
`final-source-identity.json` identifies the final implementation. Recipe SHA-256
is `5072b1d083445795bf1bc7cc31bd7328b7286f53ea2ae8b1cc2aa00ee5c50894`;
owned request SHA-256 is
`476b6b6d9c84345bee8bae61a43a7e318365398dfe6a66da41ce0aeb2445487a`.
The existing clean six-wheel profile is unchanged. Native device identity is in
`gpu-identity.txt`: RTX 4060 Laptop / driver 615.78.08 / 8,188 MiB. No decode,
pose solve, appearance refit, model download or upload ran.

## Completed checks

Actual commands/results, from repository root:

| Command | Result / raw log |
|---|---|
| `cmake --build --preset dev -j 4` | GCC 16.2.1 C++26 native viewer/CLI/core; `build-final.log` |
| `cmake --build --preset compat-cxx23 -j 4` | Explicit C++23; `build-cxx23-final.log` |
| `ctest --preset dev` | **381/381**, 155.05 s; `ctest-dev-final.log` |
| `ctest --preset compat-cxx23` | **357/357**, 154.07 s; `ctest-cxx23-final.log` |
| `ASAN_OPTIONS=detect_leaks=0 cmake --build --preset asan --target we_core_tests we_process -j 4` | Native sanitizer build; `build-asan-final.log` |
| `ASAN_OPTIONS=detect_leaks=1 build/asan/we_core_tests '[visibility]'` | **6 cases / 108 assertions**, native ASan/UBSan/LSan; `asan-focused.log` |
| `artifacts/environments/dense-clean-20261020/bin/python -m unittest discover -s tests/integration -p 'worker_dense*_test.py' -v` | **55 tests**, no skips; `dense-isolated-tests.log` |
| Same interpreter, pattern `worker_admitted_visibility_test.py` | **8 tests**, including two-child streaming/release, late corruption and metadata forgery; `reader-tests.log` |
| `python3 tools/check_cpp_style.py src/application/depth_visibility.cpp include/world_explorer/application/depth_visibility.hpp tests/unit/dense_input_tests.cpp` | Pinned format/tidy **23.1.1**, unchanged warnings; `style-passed.log` |
| Required tracked-file Black pipeline | Pinned **26.5.1**, all pass; `black-full.log` |

The Black invocation uses `PYTHONPATH=/tmp/world_explorer_python_style_tools`:

```sh
git ls-files -z -- '*.py' | xargs -0 -n1 python3 -m black --check --quiet --workers 1
```

The style command selects `artifacts/tools/llvm-23.1.1/usr/bin` in PATH and its
`usr/lib` in LD_LIBRARY_PATH. Earlier formatting/IPC failures and Catch2's
`CHECK_FALSE` enum-flags analyzer failure remain in the logs. Single-worker Black,
template brace spacing and equivalent explicit false comparisons fix those
checks; no assertion or warning was weakened. Initial full suites also passed;
the table reports final-source reruns.

## Real integration and limits

All **63** calibrated camera records and RGB hashes match the retained resized
inputs exactly (`resized-preservation.json`). Only scope/selection and current
implementation hashes change; PatchMatch numerical settings/filters and the
2048×1102 grid remain fixed (`recipe-differences.json`). Reference groups of nine
have three temporal support views on each side. Halo observations are not counted
twice. No opened evaluation grid influenced selection or thresholds.

Active CUDA cancellation after the first complete batch returns **130** after
323.603 s total work. Completed and populated cancelled stages are retained;
resume is running. This elapsed time includes processing, not just cancellation
latency. `gpu-commands.json`, `cancelled-preservation.json` and `gpu.py` own exact
command arrays and recovery checks. `post.py` waits for successful production and
replay before native full-sequence admission; its checks remain pending.

The unchanged schema-1 nine-view export independently reproduces the retained
receipt **byte for byte**, SHA-256
`85c9546d4589eee1f28ea86bfbc40af250f7b3d64487d33966f52ff949b2b099`
(`legacy-export-command.json`, `legacy-reproduction.json`). No CUDA rerun is used.
The initial preservation check passes **24,457 files**, four original bakes and
33 audit/bundle files, retaining the pre-existing 3,982 external-header differences.

`python3 tools/check_dependency_manifest.py --local-evidence` currently exits **2**:
rebuilt native payloads need fresh runtime receipts (`dependency-command.json`).
The prior inventory's 253 source/rights gaps remain unqualified. Native platform
qualification is not attempted. Full-sequence fusion/GUI handoff, measured scale,
collision/walking and reconstruction quality remain subsequent M3 work; the
current geometry entry still consumes the single-batch depth terminal.
