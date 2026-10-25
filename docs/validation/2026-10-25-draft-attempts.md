# A0 draft attempt outcomes — 25 October 2026

**VERIFIED: local CPU attempt-journal slice; A0/AP0 remain IN_PROGRESS.**
Shared commands now retain immutable failed/cancelled draft attempts separately
from prepared/committed mutation history. The [protocol](../protocols/draft_attempts.md)
owns bounds, identities, cancellation finalization and recovery. Synthetic local
CPU evidence does not certify real reconstruction or asset production.

Cwd: repository root; timezone Asia/Hong_Kong. Parent
`c7fbc8a08ba622f4baba000497b6dca861e7b4f5`.
`R=artifacts/validation/a0-draft-attempts-20261025` (ignored).
`implementation.json` pins thirteen build/source/test inputs, SHA-256
`3928efeff67534acc60e47406de35f4d3504618913da7357857a8b9ad7f619a2`. No dependency, worker
environment, threshold or benchmark-scope change.

## Exercised behavior

Nine new cases cover exact committed replay without execution, cancellation at
both mutation durability boundaries, observer failures, writer contention,
changed identities and pending interleaving, path/source protection, invalid
admission before writes and native typed-mask admission. Maximum reserved attempt
ID admits; zero/overflow IDs and odd/excess bases refuse. A cancelled attempt stays
cancelled after a new attempt resumes the same prepared mutation successfully.
Old journal pins require resync. Source/journal byte snapshots remain unchanged.
The 512-attempt ceiling is implemented, not stressed end-to-end.

Interrupted outcome publication leaves only the start committed. Recovery selects
the already durable canonical outcome before rerunning work, including when its
pending blob is missing. Three actual child processes exit 77 after attempt start,
after committed-outcome blob installation and after cancelled-outcome installation.
Their owned histories recover without changing cancellation into success.
These are controlled crashes, not arbitrary power-loss/disk-full qualification.

The independent standard-library oracle (`python3 R/oracle.py`, expand R) matches
attempt-intent/outcome encodings, four unchanged replays, eight refused identity
substitutions and three native crash recoveries. Its cancelled pending blob is
removed from its own synthetic staging root and reconstructed exactly; the later
successful retry preserves the old cancellation. Previous v1 mutation encodings
also match, with two replays, six refusals and two native crash recoveries.
Source hashes remain unchanged (`attempt-oracle-results.json`, `oracle-results.json`).

## Actual checks

| Command | Exit / result; logs under R |
|---|---|
| `cmake --build --preset dev -j 4`; `ctest --preset dev -j 4` | 0; GNU 16.2.1 C++26 viewer/CLI/core, **635/635**; `build-complete.log`, `ctest-dev.log` |
| Same with `compat-cxx23` (`--parallel 4` build) | 0; explicit C++23 CPU, **610/610**; `build-compat.log`, `ctest-compat.log` |
| `build/dev/we_core_tests '[draft-attempts],[draft-mutations],[object-mask-edit]'` | 0; **28 cases / 9,669 assertions**; `focused-final.log` |
| Same under `build/compat-cxx23/` | 0; **28 cases / 9,669 assertions**; `focused-compat.log` |
| `cmake --build --preset asan -j 4 --target we_core_tests`; native `build/asan/we_core_tests '[draft-attempts],[draft-mutations],[object-mask-edit]'` | 0; **28 cases / 9,669 assertions**, ASan/UBSan/default LSan, no reports; `build-asan.log`, `asan.log` |
| LLVM 23.1.1 `python3 tools/check_cpp_style.py` on twelve affected C++ paths | 0; format/tidy/boundaries/docs/whitespace; `style-final.log` |
| Pinned Black 26.5.1 tracked-file check per AGENTS | 0; `black.log` |
| `python3 tools/check_docs.py`; `git diff --check` | 0; `docs.log`, `whitespace.log` |
| `python3 R/oracle.py`; `python3 R/check_preservation.py` | 0; independent encodings above; **378 protected media/result hashes match**; `oracle.log`, `preservation.json` |

Initial tidy found local constant naming, a const value preventing automatic move
and nested fixture optionals that assertions did not prove to the analyzer.
Explicit guarded access and ordinary automatic move fix these without warning
suppression (`style-first.log`). The initial seven-case run passed 1,254 assertions
before adding lock/native-crash cases (`focused-first.log`).

Attempt outcomes are historical metadata. They grant no typed adoption rights;
outer journal errors do not assure terminal state. An execution resync error
requires reopening the draft rather than assuming it never committed. Legacy direct mutation calls
still return errors without an attempt journal. Worker integration, real failure
qualification, actor/rights authenticity, full capture freezes, geometry/material
payload adoption and A1 visible products remain open. No media processing, model
installation, inference, runtime inventory refresh or GPU/platform qualification
ran. Saved runtime receipts remain stale after relinking; see [progress](../progress.md).
