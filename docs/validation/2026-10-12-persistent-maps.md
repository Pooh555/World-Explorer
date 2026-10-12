# Persistent map checkpoint evidence

2026-10-12, Asia/Hong_Kong. M1 remains IN_PROGRESS. Parent commit `172be0c`.
Working directory `/home/Pooh555/programming/World-Explorer` (repository root).
The source and native executable identities are recorded below.

## Scope

Native persistent `map_store` factory, snapshot/receipt checkpoint v1 (`WEMP`),
cooperative OS writer ownership, shared canonical record validation, closed/flushed
immutable assets and atomic manifest publication. Restart preserves epoch/revision,
calibrations/observations/poses/submap transforms, correction/deletion invalidation
and bounded retry receipts. Source canonical pose assets are checked and copied
into the store. Existing immutable snapshots survive commits/reset/closure.
Shared application commands expose `init-map`, `import-map` and `inspect-map`.
The retained real capture was not re-decoded, solved, fitted or rendered; no
model downloads occurred. CPU worker fixtures still exercise diagnostic decoders.

Pre-publication failure/cancellation preserves RAM, prior manifest and caller
input. Post-rename directory-flush failure is explicitly ambiguous and requires
close/reopen; it is not described as rollback. Retention has byte/count limits;
old assets are retained instead of silently deleting history. See
[contract](../protocols/maps.md) and [decision](../adr/005-persistent-map-checkpoints.md).

## Checks

All final commands exit **0**, with **54/54** CTest cases in each local profile.
Arch Linux; GCC 16.2.1, Clang/format/tidy 23.1.1, CMake 4.4.4, OpenSSL 3.6.5,
Python 3.14.8; pinned FlatBuffers 25.2.10. The previous 46-case suite is extended
with eight persistent-map fixtures, including generator variants.

Actual commands: [2026-10-12 check reference](checks-2026-10-12.md),
DEV, CLANG, COMPAT, LIBCXX, ASAN and TSAN; results/counts and logs elsewhere apply
to this report only.

Native sanitizer execution used authorized escalation. Formatting,
`python3 tools/check_boundaries.py` and `git diff --check` exit 0.
`clang-tidy -p build/dev ... --warnings-as-errors='*'` exits 0 on
`src/map_store/{map_store,map_codec,map_files,map_persistence,pose_asset}.cpp`,
`src/application/map_commands.cpp`, `apps/we_process/main.cpp` and
`tests/unit/{persistent_map,pose_asset}_tests.cpp`. No warning gate was weakened.
Negative test predicates avoid a Catch2 bitmask analyzer false positive, and
invalid wire enums are injected as bytes rather than invalid native enum casts.
An extended cleanup assertion initially failed Catch2 expression decomposition;
it was repaired and **all six final builds/tests were rerun**. Failure log:
ignored `artifacts/validation/durable-map-initial-test-macro-failure.log`.
Other build/test/tidy logs: ignored `artifacts/validation/durable-map-*`.

Risk fixtures verify reopening with retained old-base retries, operation-content
collision rejection, corrections/deletions and old snapshot lifetime, exclusive
writer ownership, concurrent duplicate commits, cancellation, encoded/disk budget
failure, epoch reset/reuse, smaller receipt-budget rejection, expired retry resync,
rehashed malformed/future/invalid checkpoints, manifest tampering/missing-manifest
rejection, unreferenced crash leftovers, source dependency preservation/corruption,
and the 4096-entry admission boundary remaining reloadable. Canonical tests remain
byte-stable across the shared-code extraction, including asymmetric Sim(3), OPENCV,
crop/resize, original signed PTS and malformed/covariance cases. Synthetic inputs
remain diagnostics, not real reconstruction proof.

## Retained real capture

The existing canonical source is
`artifacts/sample2-canonical-20261012/canonical-current.weposes`: **19,960 bytes**,
SHA-256 `efac98bd6986f89c9dd3ec19cedffa65660d513376b243fb87489a2c52781ba2`.
It belongs to the successful native exhaustive-guided recovery, not the original
fit's different pose completion. Video remains in ignored `data/local/sample.mp4`;
no video or job was copied into Git.

```sh
build/dev/we_process init-map artifacts/sample2-persistent-map-20261012
build/dev/we_process import-map artifacts/sample2-canonical-20261012/canonical-current.weposes efac98bd6986f89c9dd3ec19cedffa65660d513376b243fb87489a2c52781ba2 artifacts/sample2-persistent-map-20261012
build/dev/we_process inspect-map artifacts/sample2-persistent-map-20261012
```

All exit 0. Separate-process reopening yields epoch 0/revision 1, **63 observations,
63 poses, one OPENCV calibration**, arbitrary scale. Repeating `import-map` in a
new process returns success with the identical manifest hash; its persisted
operation-1 receipt is reused. Initial canonical source bytes are copied exactly.
Checkpoint: **19,824 bytes**, SHA-256
`488f7c50ecbbba524465c4ec4af7273984c2c2045c2a2f51681ddec009af9342`.

A native verification executable links the actual dev map/core libraries, opens
that persisted map and re-encodes its recovered snapshot with the original
canonical provenance. Its SHA-256 equals the original canonical file exactly,
proving all serialized fields/doubles/IDs/timestamps survive recovery. Probe source,
compile command and output are retained under ignored
`artifacts/validation/durable-map-real/`. Pinned `flatc -t --strict-json
--defaults-json --raw-binary` diagnostics also inspect the checkpoint. Flatc's JSON
rounds doubles (maximum observed pose difference 4.9988e-13); this is explicitly
not exact numeric proof. Native byte-identical parity supplies the exact check.

An additional empty map at
`artifacts/sample2-persistent-map-write-failure-20261012` is initialized successfully.
A Python subprocess installs `RLIMIT_FSIZE=(4096,4096)` and ignores SIGXFSZ, then
runs the same native `import-map`. Actual file staging partially writes and returns
**exit 2 / write failure**. The prior manifest hash remains unchanged, owned staging
is cleaned, and a subsequent unrestricted `inspect-map` exits 0 at revision zero.
This is a real EFBIG partial-write test, **not a true ENOSPC or power-loss claim**.
Both final CLI and native parity were rechecked after source hardening. Original
recovery result/pose/decode pointers, original completed-job result and both scene
manifests retain their pre-run hashes. No COLMAP/FFmpeg/gsplat process was launched.

Ignored command/output/hash manifest:
`artifacts/validation/durable-map-real/verification.json`. All private source assets,
map checkpoints, probes and traces remain outside Git.

## Remaining qualification/work

Local Linux CPU persistence is verified. Native Windows/macOS, true ENOSPC/device
failure, post-rename flush-failure injection and power-loss qualification remain
unverified. Encoded/disk limits do not certify process-memory/performance gates.
Automatic pose/map/fit orchestration and complete worker command/control remain
M1 work. No Q/P/N/E gate or M1–M6 milestone was promoted; M7–M8 stay deferred.
Next: integrate canonical/map publication/reuse into shared pose preparation and
coordinator flow, retaining source/revision dependencies and cancellation/restart
behavior, then finish worker control without repeating retained reconstruction.

Final source identity: `fcc585dbd6983d909778e94278f04fce7f11a87c091fa441f3f2a12d69ee8f80` (sorted changed first-party source
paths + NUL + file bytes + NUL; inventory in ignored
`artifacts/validation/durable-map-source-identity.json`). Final dev CLI SHA-256:
`e3c4f362c37f7aaed4b857d8df78e3a17c04b7b2c1233ae1e23abb7ebd9e05f2`. Final reload/retry/native parity all exit 0;
forced partial-write import exits 2 with unchanged prior manifest and successful
subsequent reload.

Additional legacy-parent check: export/import/reload of the original completed
`artifacts/sample2-job` with its explicit retained decode stage each exit 0,
with 63 poses and canonical hash `c99c5155464981409b2988dc36c68ef695707592c6975e000bd1517a9dc7d478`. It is stored separately
at `artifacts/sample2-original-map-verification-20261012/map`; its pose completion
identity differs from the recovery's. Original pointers/manifests remain unchanged.
Exact commands/hashes: ignored sibling `verification.json`. No pose solve/fit
or pixel decoder ran.
