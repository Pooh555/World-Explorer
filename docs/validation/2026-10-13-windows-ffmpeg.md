# Windows FFmpeg setup — 2026-10-13, Asia/Hong_Kong

Scope: E2 / native CPU CI dependency preparation; **IMPLEMENTED_UNVERIFIED on
Windows**, locally checked workflow/archive handling. M1 remains in progress.
No renderer, worker, benchmark thresholds or specification changes.

## Cause and repair

The supplied Windows log stops at `choco install ffmpeg --version=7.1.1 --yes
--no-progress`: Chocolatey's V2 package lookup returns **HTTP 504**, then reports
"package not found" and exits 1. CMake/build/CTest were not reached. This is an
observed feed failure, not evidence that the version is absent. The publisher's
[7.1.1 release](https://github.com/GyanD/codexffmpeg/releases/tag/7.1.1) contains the
archive; [Microsoft's versioned manifest](https://github.com/microsoft/winget-pkgs/blob/master/manifests/g/Gyan/FFmpeg/Essentials/7.1.1/Gyan.FFmpeg.Essentials.installer.yaml)
records its URL/checksum, independently confirmed by downloading the bytes.

Replace the Chocolatey lookup with the same version's essentials ZIP. Best-effort
`actions/cache` v4.2.4 commit `0400d5f644dc74513175e3cd8d07132dd4860809` caches only
that archive under a Windows/version/SHA key; cache service failure can proceed
to strict setup/download validation. Cold setup uses curl with four
retries, 20-second connect timeout, 180-second attempt timeout and 100 MB cap.
Download to `.part`; check SHA before publication and again on every cache hit,
then extract under runner temp. Missing tools, mismatched versions, checksum or
download errors fail the step. Check both executables explicitly before exporting
PATH; preserve the existing shared `ffmpeg`/`ffprobe` preflight and all tests.

Archive SHA-256:
`04861d3339c5ebe38b56c19a15cf2c0cc97f5de4fa8910e4d47e5e6404e4a2d4`.
Actual size **92,234,348 bytes**; 48 ZIP members, all CRCs valid. Expected paths
`ffmpeg-7.1.1-essentials_build/bin/{ffmpeg,ffprobe}.exe` contain AMD64 PE binaries.
This is a CI tool dependency; no executable is committed or bundled into releases.

## Actual checks and limits

Cwd: repository root. Parent: `0d51ae2`; raw root
`R = artifacts/validation/windows-ffmpeg-20261012` (checks span 12–13 October).
Final workflow SHA-256: `0982ee85f6e2339e9e4682110e42aaccd7096bc7a1792c93e2c600424c277cda`.
`R/identity.json` hashes the workflow and local verification scripts;
`archive-verification.json` records archive/binary hashes. Publisher metadata,
manifest and cache tag→commit response are retained there.

| Command / check | Actual result |
|---|---|
| `R/actionlint/actionlint .github/workflows/native.yml` | Exit 0, actionlint 1.7.12; `actionlint-final.log` |
| Full setup script parsed with portable PowerShell 7.5.4 | Exit 0; `parse.log` |
| `python3 R/check_setup.py` | Exit 0 overall; real cold download + warm-cache prefix each exit 0; extracted executable hashes match; corrupted-cache full script exits 1 as expected before extraction/PATH changes, preserving sentinel; `setup-checks.json`, `cold.log`, `warm.log`, `corrupt.log` |
| `python3 R/check_retry.py` | Exit 0; synthetic localhost HTTP 504→200 with exact retry flags, two requests and correct body; Linux curl; `retry-check.json`, `retry.log` |
| `python3 tools/check_docs.py`; `git diff --check` | Both exit 0 |

The setup prefix includes checksum/extraction/path construction; it ends before
Windows executable invocation. No Windows binaries were executed on Linux and
no native Windows build/test pass is claimed. Local curl is exposed as `curl.exe`
only inside the verifier's tool PATH. Tools/archives/configuration remain ignored.
Verification tools were checked against publisher release SHA-256 digests before
unpacking. Initial portable PowerShell failed on its default cache location and
then invariant-mode archive-module loading; scoped XDG paths and allowing named
invariant cultures corrected the local verifier (`powershell-parse.log`,
`warm-invariant-failed.log`). These are local tool-environment failures.

C++/worker source is unchanged, so those suites were not rerun for this workflow
slice. Hosted Windows extraction/tool execution, PATH propagation and the complete
native CPU job await the next user-triggered push/rerun. Earlier Windows 77/78 and
[stage-flush repair](2026-10-12-windows-flush.md) retain their original scope;
this setup fix does not upgrade them to a native pass. No remote push performed.
