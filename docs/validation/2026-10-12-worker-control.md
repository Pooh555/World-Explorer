# Typed worker command/control evidence

2026-10-12, Asia/Hong_Kong. M1 remains IN_PROGRESS. Parent commit `5b37ab3`.
Working directory `/home/Pooh555/programming/World-Explorer` (repository root).

## Scope

FlatBuffers command/event protocol 2.0 adds bounded stdin START/CANCEL commands,
session/operation binding, acknowledgements, semantic retry receipts and
cooperative cancellation before native process-tree termination. GUI, CLI,
selected-frame batch poses and local fitting use the same native protocol entry.
Worker recipe/input validation and original stage/checkpoint identity checks
remain controlling; the transport session is not part of an optimizer identity.
Protocol 1 is rejected; direct Python JSONL diagnostics remain explicit.

The native process owner drains events/diagnostics during input backpressure.
POSIX uses pipes, thread-local SIGPIPE handling and descriptors reserved above
stdio; native backend subprocesses remain in the worker's process group. Windows
implements overlapped named-pipe input, binary Python streams, inherited-handle
whitelisting and lifetime Job Object cleanup. Windows/macOS execution remains
unqualified; Linux builds cannot verify those native paths. This is a one-batch
profile, not a streaming tracker or multi-job daemon. See [contract](../protocols/worker.md).

## Checks

All final commands exit **0**, with **64/64** CTest cases in each local profile.
Arch Linux, GCC 16.2.1, Clang/format/tidy 23.1.1, CMake 4.4.4, OpenSSL 3.6.5,
Python 3.14.8 and pinned FlatBuffers 25.2.10.

Actual commands: [2026-10-12 check reference](checks-2026-10-12.md),
DEV, CLANG, COMPAT, LIBCXX, ASAN and TSAN; results/counts and logs elsewhere apply
to this report only.

Native sanitizer execution used authorized escalation. Seven new fixtures and
extended event cases cover command argument/count/byte/UTF-8 limits, malformed
lengths/root/vtable/vector/string/version/session fields, CANCEL-before-START,
unsolicited/out-of-order acknowledgements, changed operation content, exactly-once
invocation across duplicate/equivalently encoded START and duplicate CANCEL,
fragmented prefixes/bodies, cooperative cancellation, ignored CANCEL/SIGTERM,
closed input, backpressure/oversized input and a desktop host with closed stdin.
The ignored-stop fixture uses the actual `worker.run_command` helper. On Linux,
its child records the same process group as its worker; after forced stop the
child is gone or exited/zombie, rather than executing independently. Tests use
synthetic invocation/checkpoint markers; these are not optimizer checkpoint proof.

`clang-format --dry-run --Werror` passes on all six changed first-party C++ files.
`python3 tools/check_boundaries.py` and `git diff --check` exit 0.
`clang-tidy -p build/dev src/reconstruction_adapters/process.cpp src/reconstruction_adapters/worker_protocol.cpp tests/unit/worker_protocol_tests.cpp tests/unit/application_tests.cpp --warnings-as-errors='*'`
exits 0. Python compile checks pass for `workers/{we_control,we_protocol,worker}.py`
and `workers/gsplat/fit.py`. No warning gate was weakened. Initial tidy findings
(integer multiplication widening and copied test callback parameters) were fixed.

An initial socket-based input attempt failed sandbox writes with EPERM (code 1);
the implementation now uses local pipes, without requiring native escalation for
ordinary worker tests. Early startup failures remain in ignored
`artifacts/validation/worker-control-tests-dev-initial.log`; initial tidy findings
are in `worker-control-tidy-initial-failure.log`. Compile diagnostics during test
construction were repaired before the final builds. Final build/test/tidy logs
are ignored `artifacts/validation/worker-control-*`.

Source SHA-256:
`aef06ffc3f51d1e67ff17b60313d951a8952200f39870cd63f987f72b91edec7`.
It hashes sorted `CMakeLists.txt` and `.cpp`/`.hpp`/`.fbs`/`.py` files under
`apps`, `include`, `src`, `tests`, `schemas`, `workers`, concatenating UTF-8
relative path + NUL + binary file SHA-256 (109 files). Native
`build/dev/we_process` SHA-256:
`9b21c374a64f88fe7ab0cc6e0d8b772c3b176083d5bd9e9587d68ceb6d423296`.

## Retained real capture

Source: ignored `data/local/sample.mp4`, SHA-256
`51263d9991dcbc2136d0809ecba1a8ba1b261896f3b222b7afba19784a153ef6`.
Only metadata inspection/hash verification was performed: no RGB extraction,
COLMAP solve, optimizer, GPU execution, downloads or source-pointer rewrite.

```sh
build/dev/we_process inspect --source data/local/sample.mp4 --job-dir artifacts/sample2-control-final-20261012
```

Exit **0**, verified START acknowledgement and a checked metadata completion.
The same command in a second process/session reuses the committed inspection;
root `result.json` remains SHA-256
`45b2f30931fbe13657870503d9017244e9c18b5fe8e5afb16bc9d6359c3cabf7`.
The result belongs to the same 20.948333-second, 3840×2160/30 FPS H.264 source.
This is preflight metadata evidence, not whole-video P1 timing.

A separate native inspection in
`artifacts/sample2-control-final-cancel-20261012` receives SIGINT on the CLI
immediately after its START acknowledgement. The CLI monitor sends typed CANCEL;
the real worker acknowledges it, emits `CANCELLED` before committing a stage and
the CLI exits **130**. No RGB/pose/appearance stage runs. The task-specific Python
probe launches `we_process` via an argument array, watches its stdout, signals only
that process and records its result. Logs/probes remain ignored under
`artifacts/validation/worker-control-real`, including `inspect-final.log`,
`inspect-final-restart.log`, `cancel-inspect-final.log` and `verification.json`.
Eight original pointer/result/input/scene manifests retain their saved hashes,
including the stale original decode pointer and independent pose recovery result.

## Remaining scope

Automatic full-video preflight/pose-to-fit orchestration is next. Native Windows/
macOS command transport and new-transport real optimizer cancellation/resume are
not verified by these CPU tests or marker files. Changed worker source continues
to change recipe hashes; older optimizer checkpoints are not silently upgraded.
Existing completed poses/fits remain available through explicit checked adoption.
No renderer/performance/corpus/geometry or full Q/P/N/E gate is promoted. The
reference ≤250k fixture, hardware/platform qualification and other M1–M6 work
remain pending; M7–M8 remain deferred.
