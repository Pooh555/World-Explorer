# Worker event transport — 11 October 2026

Parent commit `86ae7b3`. Scope: existing Arch host. M1 remains IN_PROGRESS; native
Windows/macOS execution and full platform/benchmark gates are pending.

CLI/GUI and local fit coordination use FlatBuffers events instead of treating
worker stdout as JSONL. Execution moved behind the adapter boundary with separate
binary stdout and diagnostic stderr. See [protocol](../protocols/worker.md) and
[dependency decision](../adr/004-flatbuffers-worker-events.md).

Actual commands, each exit 0:

```sh
cmake --preset dev
cmake --build --preset dev -j 4
ctest --preset dev
cmake --preset clang -DWE_CXX_STANDARD=26 -DWE_BUILD_VIEWER=OFF
cmake --build --preset clang -j 4
ctest --preset clang
cmake --preset compat-cxx23 -DWE_BUILD_VIEWER=OFF
cmake --build --preset compat-cxx23 -j 4
ctest --preset compat-cxx23
cmake --preset asan
cmake --build --preset asan -j 4
ctest --preset asan
```

All four profiles pass **27/27 CPU/worker tests**: GCC 16.2.1 C++26 development,
Clang 23.1.1 C++26 CPU, GCC C++23 CPU, and native ASan/UBSan/LeakSanitizer CPU.
New checks cover byte-fragmented framing, prefix/root-offset/identifier corruption,
version/UTF-8 errors, duplicate handshake, sequence gaps, truncated/missing terminal,
exit mismatch, diagnostic separation, typed failure and missing/changed manifest
references. A corrupt stream followed by a 20-second sleeping worker is cancelled
and reaped within the four-second test bound. Synthetic fixtures do not reconstruct.

The existing isolated libc++ 23.1.1 C++23 CPU profile also builds and passes 27/27,
using the exact configure command recorded in the CI follow-up report, followed by
`cmake --build build/ci-libcxx -j 4` and
`ctest --test-dir build/ci-libcxx --output-on-failure`. This checks the changed
compiler/library boundary locally; it is not a hosted macOS pass.

Real-source integration commands:

```sh
build/dev/we_process capabilities
build/dev/we_process inspect --source data/local/sample.mp4 --job-dir artifacts/wire-inspect
build/dev/we_process fit --source artifacts/nonexistent-backend --data artifacts/nonexistent-model --video data/local/sample.mp4 --output artifacts/wire-fit-failure --steps 7000 --max-image-dimension 1280 --worker-python .venv-train/bin/python
build/dev/we_process reconstruct --request artifacts/sample2-reconstruct.json
build/dev/we_process validate artifacts/sample2-coordinated.wexplore
```

Capabilities/inspection use Python 3.14.8 through the verified native receiver and
exit 0. Real inspection takes **0.113713 s** for its inspect stage and publishes a
checked manifest reference. The deliberately invalid gsplat backend path in the
Python 3.10 training environment returns **exit 2** with a verified BACKEND_FAILED
event and separate git diagnostic; fitting/GPU work never starts. Completed real
fit reuse and validation exit 0: optimization skipped, **304867 splats**, prior
63/63 connected poses retained. Neither poses nor optimization were restarted.

clang-format/tidy 23.1.1, include boundaries and `git diff --check` pass. Tidy covers
process/protocol/facade/new tests with no unsuppressed first-party diagnostics.
Archives/generated bindings/private video/manifests/logs remain ignored under
`.deps/`, `build/`, `artifacts/validation/wire-*`. Apache-2.0 notice is retained.

Human JSON requests still use argv and cancellation uses native process trees.
Persistent bidirectional commands, concrete native frame/pose adapters, canonical
FlatBuffers pose/map persistence and existing reference/platform gaps remain
unfinished. No milestone or Q/P/N/E requirement was promoted to VERIFIED.
