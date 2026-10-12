# Native CPU CI portability — 2026-10-12, Asia/Hong_Kong

Parent: `badd680`; implementation commit contains this report. Cwd: repository
root. Test/workflow repair only; M1 remains IN_PROGRESS. No reconstruction, GPU,
Windows/macOS execution or remote push performed locally.

## Supplied hosted evidence and causes

| Host | Observed result | Failure |
|---|---|---|
| macOS | 78/79 pass | Test 53 compares canonical `/private/var/...` to aliased `/var/...` |
| Windows | 74/78 pass | Test 53 compares long path to `RUNNER~1`; tests 55/56/58 fail fresh RGB admission with “Unable to start worker executable” |

The request parser correctly canonicalizes the output's existing ancestors;
its fixture expected lexical spelling. Compare against `weakly_canonical` because
the output directory does not exist yet; retain the exact selected-path assertion.

Fresh admission uses the production decoder, whose executable defaults to
`ffmpeg`. Windows CI omitted FFmpeg; the [runner inventory](https://github.com/actions/runner-images/blob/main/images/windows/Windows2025-Readme.md)
also does not list it. Hiding FFmpeg locally reproduces the launch failure before
pose START, followed by cancellation/receipt assertions expecting a later stage.
Windows now installs the versioned [Chocolatey FFmpeg 7.1.1 package](https://community.chocolatey.org/packages/ffmpeg/7.1.1).
Every CI job checks both `ffmpeg` and `ffprobe` execution before configuration.
No test is skipped, warning weakened or production path canonicalization changed.

Supplied logs are retained locally as `hosted-{macos,windows}.log`; their build
revision was not included. SHA-256:

- macOS: `a43c1b5e5d0f95de12d191777900789ce7986902126af534ffd9924e0bfb32e0`
- Windows: `05ff4035482250eaa138e4be16304747c41ebc66956e6b0424a3b97442b9b33c`

## Local checks

Arch Linux; GCC 16.2.1, Clang/libc++/format/tidy 23.1.1, CMake 4.4.4,
system Python 3.14.8; FFmpeg/ffprobe n9.0.2. All final commands exit 0 unless noted.
Logs and `source-identity.json` are ignored under
`artifacts/validation/native-ci-20261012/`.

| Actual command/check | Result / log |
|---|---|
| [COMPAT and LIBCXX build/test commands](checks-2026-10-12.md) | 79/79 each; `build-{compat,libcxx}.log`, `tests-{compat,libcxx}.log` |
| `TMPDIR=<temp-alias> build/compat-cxx23/we_core_tests 'automatic inspection request resolves local worker paths and rejects protected overlaps'` | Same aliased-root fixture: before exit 42, after exit 0; `alias-{before,after}.log` |
| `PATH=<empty-path> build/compat-cxx23/we_core_tests 'fresh coordinator chains decode RGB admission solve and publication with restart'` | Before repair, exit 42; missing executable code 2; `missing-ffmpeg-before.log` |
| `python -c "import subprocess; subprocess.run(['ffmpeg', '-version'], check=True); subprocess.run(['ffprobe', '-version'], check=True)"` | Actual CI preflight: normal PATH exit 0; empty PATH with absolute interpreter exit 1; `runtime-{tools,missing}.log` |
| `clang-format --dry-run --Werror tests/unit/reconstruction_job_tests.cpp` | pass |
| `clang-tidy -p build/dev tests/unit/reconstruction_job_tests.cpp --warnings-as-errors='*'` | no unsuppressed diagnostics; `tidy.log` |
| `.deps/bin/actionlint .github/workflows/native.yml` | pass, actionlint 1.7.7 |
| `python3 tools/check_boundaries.py`; `python3 tools/check_docs.py`; `git diff --check` | pass |

Placeholders resolve under the above ignored directory: `temp-alias` symlinks to
`temp-real`; `empty-path` contains no executables. Local checks do not qualify the
Windows Chocolatey install or native MSVC/macOS paths. Next external check is the
same hosted workflow after the user's next push; latest fixes remain
IMPLEMENTED_UNVERIFIED on those native profiles. Prior six-profile software
results retain their historical scope; private capture/pose/fit outputs were untouched.
