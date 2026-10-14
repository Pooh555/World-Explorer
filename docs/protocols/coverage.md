# CPU coverage measurement

The `coverage` preset isolates Clang source instrumentation from development,
sanitizers, vendored libraries and GPU targets. It keeps **C++26**; an incompatible
compiler/library fails configuration. Coverage requires tests, Clang, no viewer
and no sanitizer. `llvm-cov`/`llvm-profdata` must match the compiler's LLVM major.

```sh
cmake --preset coverage
cmake --build --preset coverage -j 4
cmake --build --preset coverage --target we_coverage_report
```

The report target runs all configured CPU CTest cases with a fresh process/module
profile directory under `build/coverage/coverage/run-*`. It fails on test, merge,
tool or denominator errors. LLVM atomic counters cover concurrent contract code.
The executable **and all seven production library archives** supply coverage
mappings, retaining functions in objects that tests never link or execute. No
previous run or Catch discovery profile enters a measurement.

Each run saves exact argv/exits, compiler/tool versions, source/build/object
SHA-256 identities, profiles, full LLVM JSON, separate group counters and an HTML
report with branch counts. Read `summary.json` and `html/index.html` in the printed
run folder. A missing production `.cpp`, duplicate file, invalid counter or empty
group aborts instead of yielding an inflated percentage. Headers without emitted
coverage regions are listed separately, including declarations-only contracts.

The reviewed denominator is [coverage_scope.json](../../tools/coverage_scope.json):

| Group | Included files |
|---|---|
| Non-GPU core | Every first-party C++ source/header in `src/` and `include/world_explorer/`, except `render_vulkan/` |
| Package/update | Whole `scene_store`, `map_store`, `reconstruction_adapters` implementations/headers plus the renderer update sink. This deliberately includes persistence, codecs, artifact admission and worker scheduling, rather than selecting only well-covered validation functions |

Vendored code, generated bindings, standard libraries and test harnesses are
excluded. Executable entry points and native viewer UI lie outside this CPU core
library measurement. GPU behavior needs image/synchronization evidence. **No
whole platform file or error path is excluded**: compiled POSIX/Windows code in
processes, locks and atomic publication remains included. Inactive preprocessor
branches are outside LLVM's local mapping; another OS needs its own measurement.
These exclusions define this scoped measurement, not an exemption from other
release checks.

Counters sum LLVM's per-file covered/count values; percentages are weighted by
counts, not averaged across files. LLVM branch coverage counts true/false outcomes
of mapped conditions, including short-circuit conditions. Uninstantiated templates
and inactive preprocessor code need separate review; source coverage cannot prove
those paths or safety under allocation/device failure.

`we_coverage_report` collects evidence even when targets are unmet and prints that
state. `we_coverage_check` reruns the same fresh measurement and exits 1 unless
core line **≥80%**, core branch **≥70%**, and package/update branch **≥90%** all
pass. These unchanged §3/E1 targets also appear separately in `summary.json`;
passing them alone does not certify E1's sanitizer/GPU/platform requirements.
