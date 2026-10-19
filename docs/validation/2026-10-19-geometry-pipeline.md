# Unified retained-cache geometry job — 2026-10-19 (Asia/Hong_Kong)

M3 remains **IN_PROGRESS**. The shared application/CLI and viewer background job
now own retained-depth fusion/reuse, native conversion/admission and immutable
scene handoff. This is partial arbitrary-scale geometry, without fresh CUDA
depth, measured alignment, collision/walking, Q5/N2 or A-track qualification.
The [contract](../protocols/fusion.md#unified-retained-cache-solver-and-viewer-job)
owns the request, output/replay and cancellation semantics.

Repository root `/home/Pooh555/programming/World-Explorer` is the cwd. Parent
`65dace6`; ignored evidence root `R=artifacts/validation/m3-geometry-pipeline-20261019`.
`identity.json` pins all 11 changed code files, five tested executables and both
current requests. `toolchain.txt` records GCC 16.2.1 / Linux x86-64; development
C++26 and explicit CPU C++23 remain separate. Native graphics uses RTX 4060 Laptop;
driver **615.78.08**, 8,188 MiB (`gpu-identity.txt`); actual viewer logs
identify the device and Vulkan validation. Worker environments,
external headers, cached sources and prior reconstructions were not modified.

## Commands and checks

Actual command arrays/cwd/exits/durations and logs are in
`real-current-commands.json`, `adverse-commands.json`, `populated-commands.json`,
`native-commands.json`, `native-handoff-fixed-command.json` and
`quality-commands.json` / `quality-final-commands.json`. Their local drivers and diagnostic sources remain under R.
Build/test entry points executed from the repository root:

```sh
cmake --preset dev
cmake --build --preset dev -j 4
ctest --preset dev
cmake --build --preset compat-cxx23 -j 4
ctest --preset compat-cxx23
ASAN_OPTIONS=detect_leaks=0 cmake --build --preset asan --target we_core_tests -j 4
ASAN_OPTIONS=detect_leaks=1 build/asan/we_core_tests 'geometry*'
python3 tools/check_cpp_style.py <10 changed C++ paths from R/cpp-paths.txt>
git ls-files -z -- '*.py' | xargs -0 -n1 python3 -m black --check --quiet --workers 1
```

Development CPU CTest passes **378/378** in **162.29 s**; explicit C++23 passes
**354/354** in **93.70 s**. Focused synthetic pipeline and viewer checks pass
**2 cases / 64 assertions** and **1 / 8** before the final additional process-
deadline test; both final full suites include that test. The scoped repo style
gate passes all ten changed C++ paths with actual format/tidy **23.1.1**, unchanged
warnings and no VFS/header overlay. Documentation/boundary/whitespace checks pass
there; final documentation checks are recorded separately. Final initializer-
spacing cleanup rebuilt development and passes **6 geometry cases / 136 assertions**
and **1 viewer case / 8 assertions** (`final-focused-{core,ui}.log`). Those changes
are whitespace only; the full-suite results above precede that final rebuild.
Native ASan/UBSan/LeakSanitizer passes the final **6 / 136** geometry checks
(`asan-focused.log`). Initial sandbox ASan test discovery failed because LSan
cannot run under ptrace; only build-time discovery disables leak detection.
The actual focused sanitizer run uses `detect_leaks=1` outside the sandbox.

Pinned Black **26.5.1** runs from an isolated `/tmp` tooling directory, completed
offline using retained dependency wheels after a sandbox DNS failure. The changed
`tools/geometry_job.py` passes. The exact required full check exits **123**:
**24 unchanged files** need formatting (`black-failures.json`). Their current
bytes equal HEAD (`black-baseline.json`); no broad source/recipe rewrite was made.
An aggregated diagnostic invocation stalled in multiprocessing and was cancelled;
individual-file diagnostics identify all 24 failures. This is existing style
debt, not a Python pass. No worker environment was altered to install the formatter.

## Real data, cancellation and native handoff

Both retained native visibility caches complete the new pipeline and strict
replay. `real.py` initially rejects the historical converter pin after intervening
style changes. Fresh requests bind current converter/coordinator bytes and a
current fixed fusion recipe: **only `implementation_sha256` differs** from the old
recipe (`recipe-differences.json`); every numerical policy/filter and input cache
remains unchanged. The rejected request/log and historical outputs remain intact.

| Complete partial mesh | Vertices | Triangles | Native submitted frames | Vulkan errors |
|---|---:|---:|---:|---:|
| Temporal | 32,074 | 47,559 | 3,147 | 0 |
| Parallax | 61,598 | 82,248 | 1,550 | 0 |

Temporal viewer startup performs a fresh owned solve/conversion; parallax startup
first loads the saved 295,952-splat sample_1 scene, then reuses/adopts geometry.
Frame counts include processing/UI frames, **not** performance qualification.
Inspected screenshots show fragmented normal-color surfaces and large unknown
gaps. All voxel planes match the retained prior outputs exactly. Mesh array order
varies: position sets and remapped unoriented triangle sets match exactly; sorted
normal maximum differences are **1.19e-7 / 1.79e-7**. The initial raw-array ordering
assertion failure is retained; no byte-determinism or geometry improvement is claimed.

A real deepest fusion child is SIGSTOPped after writing **607,860 bytes** of
`observed-voxels.npz`. Cancel returns **130** in **2.226 s**, reaps all three
observed descendants and publishes no solver terminal. Resume selects a fresh
attempt; prior partial bytes remain identical. A separate earlier cancellation
at a zero-byte NPZ boundary completes in **2.129 s**. Post-conversion cancellation
retains the real native asset without its terminal; resume uses native attempt 1
and strict replay skips conversion. Changed current-pose/source bytes at the
solver/adoption boundary, foreign occupied outputs and top-level writer
contention reject without native publication (`writer-contention-command.json`). Synthetic unit tests additionally cancel an active supervised child
and reject bad deadlines, checksums, schemas and indirect-cache overlaps.

Native `application_state::geometry` diagnostics preserve the prior capture's
pixels exactly after failure/cancel, then publish the full 47,559-triangle mesh
through renderer update admission: **210 frames / zero Vulkan errors**. The first
diagnostic used an invalid final test camera; its exit 2 is retained. The corrected
camera rerun passes (`native-handoff-fixed/report.json`). Actual viewer runs pass;
sandbox X11 failure remains separate from native GPU evidence.

`preservation.json` / `preservation-final.log` check **24,457 protected files**, all four original bakes and
33 prior audit/bundle files unchanged; the pre-existing **3,982 external header
differences** remain. Those require isolated worker-profile reconciliation before
fresh CUDA depth. No native Windows/macOS or release/rights/full gate is promoted.
