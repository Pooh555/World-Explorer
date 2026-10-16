# Buffered native input timing — 2026-10-16 Asia/Hong_Kong

Scope/status: polling-frame control behavior **VERIFIED on the local development
profile**; **N1/M2 IN_PROGRESS**. Physical input-to-display, 60 Hz H_V and full
qualification remain open. Parent `fc62257`; implementation is the commit
containing this report. Cwd: repository root. Ignored evidence root **R**:
`artifacts/validation/m2-input-latency-20261016`; runtime subdirectory **T**:
`R/runtime`. Source/binary/runner/check-log identities are bound in
`R/implementation-identity.json`, SHA
`fd9f8798629bab02321bfe42fd51ccb6fa2beed207280c83e6f10f25bc1b1941`.

## Implemented observation

`--input-latency NEW.json` buffers up to 4,096 relevant events and camera-update
rows in pre-reserved storage. Serialization and report writes occur after the
interactive loop and GPU idle. Exclusive creation preserves existing files;
trace/screenshot combinations fail before SDL/output creation. Each row retains
actual input gates/movement, pose/speed before and after `fly_camera::update`,
polling frame, preceding/current update times and raw event/poll timestamps.
Invalid timestamps/values and overflow remain explicit; no wrapped/zero duration
is substituted for a failed observation. The [viewer contract](../protocols/viewer.md)
describes scope. No camera, reconstruction, renderer or shader behavior changes.

SDL's [event timestamp contract](https://wiki.libsdl.org/SDL3/SDL_KeyboardEvent)
and [clock documentation](https://wiki.libsdl.org/SDL3/SDL_GetTicksNS) establish the
SDL initialization-relative domain used at all three endpoints. The local
[virtual-keyboard protocol](https://raw.githubusercontent.com/swaywm/wlroots/5adf325333602d5b1e7ccdeb122633bbc8040ace/protocol/virtual-keyboard-unstable-v1.xml)
uses millisecond source timestamps. Nanosecond fields do not establish nanosecond
source precision or physical input onset.

## Native observations

Existing GCC **16.2.1 C++26 Debug** and Clang **23.1.1 C++23/libstdc++ Debug**
profiles; SDL **3.4.18**, Wayland/Hyprland **0.56.2**, NVIDIA RTX 4060 Laptop,
driver **615.78.08** (`native-environment.json`). Full retained sample_1,
**295,952 splats**, source spectator view, 1280×800 requested logical window;
scene/manifest, binary, shaders, runner and helper hashes remain unchanged through
each process exit. No LOD trim or reconstruction rerun. These are local CPU-state
measurements under normal desktop scheduling, not controlled 60 Hz/reference
performance or presentation timings.

Commands `python3 R/run_native_timestamped.py NAME BINARY` and `python3
R/audit_native.py NAME` each exit **0** for all four names below. BINARY is
`build/dev/we_viewer` for GCC and `build/m2-vma-clang/we_viewer` for Clang. Runs
are sequential, after builds/CTest finish. The runner waits four seconds after
window setup, then sends controls; actual visibility remains recorded separately.
The table retains every event, including previsibility/uncaptured startup input.
Nearest-rank percentiles are computed per run; no cross-run average hides a tail.

| Run | All / visible-scene events | Visible event→state p50 / p95 / p99 (ms) | All-event maximum (ms) |
|---|---|---|---|
| gcc-final-1 | 144 / 144 | 3.830352 / 7.997137 / 8.299874 | 8.472170 |
| gcc-final-2 | 375 / 145 | 4.451546 / 8.094416 / 8.730743 | 587.406996 |
| gcc-final-3 | 144 / 144 | 3.615414 / 7.695680 / 8.700429 | 8.713367 |
| clang-final-1 | 144 / 144 | 4.038744 / 7.937789 / 8.460997 | 8.609123 |

Every run checks **48 movement downs / 48 releases / 16 look / 16 wheel / six
blocked downs** against actual before/after state in the polling frame: **134
checks per run**, all pass. All four processes exit 0 with **zero Vulkan errors**,
no dropped events, invalid timestamps or nonfinite event values. Visible-scene
poll→state p95 is **0.014169 / 0.009671 / 0.008450 / 0.008532 ms**, respectively.
The corresponding `commands.json`, `latency.json`, `audit.json` and viewer logs
retain exact context, input sequence, metrics and hashes.

One GCC repetition has **230 additional previsibility motion events** and a
**587.407 ms** event→state maximum before scene visibility/capture; all-event p95
is **547.407 ms**. This startup delay remains part of the report, with no claim
that steady-state numbers cover loading. Across final runs, **11** event
source timestamps precede the prior CPU update by **0.046846–0.712119 ms**,
consistent with millisecond source quantization. Those observations remain
flagged; no tolerance is introduced to certify the full one-frame requirement.

The initial `run_native.py gcc-1 build/dev/we_viewer` exits **0**, but targeted
Hyprland `sendkeystate` events reuse an old timestamp: visible-event maximum
**8,484.845 ms**. This run also overlaps build/CPU checks and is retained as a
runner diagnostic, not a latency baseline. The final local keyboard helper uses
fresh monotonic milliseconds and records the protocol timestamp for each request;
all 48 active movement-down timestamps are distinct in each final run. Existing
virtual-pointer bytes are reused. Helpers affect only the owned viewer's test
sequence; focus/cursor are restored. No protocol/model provisioning is performed.

## Checks and current runtime receipts

- `cmake --build --preset dev -j 4`; `cmake --build build/m2-vma-clang -j 4`:
  final builds **0**, `build-dev-final.log` / `build-clang-final.log`.
- `ctest --preset dev --output-on-failure -j 4`: **0**, **278/278**, **17.37 s**,
  including **149 worker cases**; `ctest-dev-final.log`. The earlier 277-case
  pass/105.06 s retains its earlier implementation and competing-build context.
- Each profile's `we_viewer_tests '[input_latency]'`: **0**, **five cases /
  26 assertions**. Checks cover malformed/reversed clocks without unsigned wrap,
  old backlog, blocked state, event-cap reporting, nonfinite values and caller-file
  preservation. Two actual CLI conflict checks return expected **2** with no output.
- clang-format/clang-tidy **23.1.1**, three changed translation units with
  `--warnings-as-errors=*`, include boundaries, docs and whitespace: **0**.
  Initial tidy exits 1 on swappable metadata parameters and Catch's external
  CHECK_FALSE enum analysis; named context fields and an explicit false comparison
  resolve these without changing assertions or disabling warnings (`tidy*.log`).
  `final-static-commands.json` retains exact version/format/boundary/docs/diff
  argv, exits and log hashes; final native launch identities still match current bytes.
- `python3 T/capture_loader.py`, `observe_viewer.py native-input`,
  `prepare_summaries.py`, owner capture, `prepare_inventory.py` and
  `check_inventory.py`: **0**. Owner capture uses `tools/audit_binding_owners.py
  --mapping third_party/backend-bindings-input-20261016.json --selection
  third_party/runtime-selection-input-20261016.json --output T/owners.json`.
  Loader: **15 roots / 213 files / 644 inspections / 123 packages / 96 notices /
  zero unresolved edges**, **13.848 s**. Native empty viewer: **900 frames / zero
  errors**, **9.768 s**. Mapping: **208 files / five contexts**, only viewer bytes
  change; four historical imports revalidate. Owners: **137 targets / 16 groups /
  30 notices / 64 queries**. Default and local inventory checks exit **0**;
  strict closure exits expected **3**, retaining **84 components / seven profiles /
  253 source/rights gaps**, owner terms separately unqualified; **234 inputs**.

| New tracked record | SHA-256 |
|---|---|
| [runtime-selection](../../third_party/runtime-selection-input-20261016.json) | `ee009fd864555eec7bca0756608942885661f97e269a18ee2c2eb549cdc6b479` |
| [runtime-audit](../../third_party/runtime-audit-input-20261016.json) | `6defc03031ea4b14b2483303d2b3ba7f5b76b3746055c1c610531e036ada8c46` |
| [backend-bindings](../../third_party/backend-bindings-input-20261016.json) | `6b0e18f8c8a40858722d46d7317f6d2818b384cd164202a3ec33d17e8fe949b2` |
| [binding-owners](../../third_party/binding-owners-input-20261016.json) | `5609502f5ca762824650382b8c3f46b80580fbac213174b36ddf0cb7803611f2` |
| [dependency-audit](../../third_party/dependency-audit-input-20261016.json) | `734164328631a86548946b62ae0c9bd4708adf48c32449b76cdf13ce05453c5b` |

Historical runtime receipts/failures remain intact. All **1,382 protected files**
match the preceding baseline (`preservation.json`), including saved runs, sources,
instructions and specification/runbook. No solver/optimizer, long stress,
provisioning, upload or push. Actual presented-frame/input-to-display latency,
queue-depth qualification, reference hardware, captured corpus/scale and other
platforms remain open; no full Q/P/N/E gate is upgraded.
