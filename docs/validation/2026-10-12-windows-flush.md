# Windows worker stage flush — 2026-10-12, Asia/Hong_Kong

Parent: `aa060c4`; implementation identity is the commit containing this report.
Cwd: repository root. M1 remains IN_PROGRESS. No remote push, native Windows/GPU
execution, private capture reconstruction, optimizer or checkpoint upgrade.

## Diagnosis and change

Latest supplied Windows run: **77/78 CTest cases pass**, including all native
contract cases. Only `worker_integration` fails: real decode raises `OSError:
[Errno 9] Bad file descriptor` in stage asset `fsync`; the journal test then
observes failure at decode rather than the expected unavailable pose solver.
The user reports the other native-build jobs now pass; no new macOS log/count
was supplied. Hosted Windows input SHA-256:
`a0db8ee3baafa699547c8d822104ce36d667230854afa7ef7fd2b017b1aa4534`.

[Python fsync](https://docs.python.org/3/library/os.html#os.fsync) uses MS `_commit`
on Windows; [Windows flushing](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers)
requires write access. The worker reopened owned stage files with `rb` before
flushing. It now uses non-truncating `r+b`; asset flushes still precede completion
publication, and exceptions still preserve incomplete output. No flush is skipped
or error suppressed. JSON/checkpoint writers already use writable handles.

Changing worker source changes recipe/owned-job identities intentionally. Existing
outputs remain readable through explicit checked adoption; older owned automatic
requests require a compatible worker identity or separately selected fresh job.
Do not rewrite ownership, cached stages or old optimizer identities to reuse them.

## Checks

Arch Linux, Python 3.14.8 and worker Python 3.12.15; existing GCC 16.2.1 C++23
CPU configuration, local FFmpeg/ffprobe n9.0.2. Ignored logs, hosted input and
`identity.json`: `artifacts/validation/windows-flush-20261012/`.

| Actual command/check | Result / log |
|---|---|
| `python3 -m unittest discover -s tests/integration -p 'worker*_test.py' -k stage_flushes -v` before fix | exit 1, modeled Windows write-access check reproduces EBADF; `regression-before.log` |
| `python3 -m unittest discover -s tests/integration -p 'worker*_test.py' -v` | exit 0, 9/9, no skips; `python-tests.log` |
| `.venv-worker/bin/python -m unittest discover -s tests/integration -p 'worker*_test.py' -v` | exit 0, 9/9, no skips; `python312-tests.log` |
| `ctest --preset compat-cxx23 --output-on-failure` | exit 0, 79/79, nine Python tests inside worker case; `tests-compat.log` |
| `python3 -m py_compile workers/worker.py tests/integration/worker_pipeline_test.py` | exit 0 |
| `python3 tools/check_boundaries.py`; `python3 tools/check_docs.py`; `git diff --check` | exit 0 |

New synthetic boundary tests model Windows write-access requirements while
performing real flushes, check binary payload/inventory/cache parity, and inject
asset EIO to verify previous pointers/results/completion survive while partial
assets remain unpublished. Existing tests execute real tiny FFmpeg decode and
journal conversion; no real pose solve/training gate is claimed. No C++ changed
or rebuild required. Final Windows repair remains IMPLEMENTED_UNVERIFIED until
the next hosted rerun. Native viewer/release and power-loss qualification remain separate.
