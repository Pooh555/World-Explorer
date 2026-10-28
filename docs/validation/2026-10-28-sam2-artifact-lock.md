# SAM control artifact candidate — 28 October 2026

**A0/AP0 IN_PROGRESS; provisioning BLOCKED.** The
[candidate](../research/sam2_control_artifacts_20261028.json) closes the declared
no-extra Python dependency graph for proposed Linux x86_64/CPython3.12/
torch2.5.1+cu124/torchvision0.20.1+cu124/SAM2. This is metadata/source inspection,
not an installer, complete environment/build/runtime lock, rights approval,
inference or hardware-fit qualification. A1/M4 and all AP/Q/P/N/E gates remain open.

Repository root `/home/Pooh555/programming/World-Explorer` is cwd; timezone
Asia/Hong_Kong; clean parent `8177fa19f14b1e6b096e096ec9f991ed6ba75847`.
Ignored `R=artifacts/validation/a0-torch-transitives-20261028` retains scripts,
raw publisher responses, selected archive records and argv/cwd/exit/time receipts.
Final proposal SHA-256:
`a9c0db0efdf44b6191909fe3077d59ca80acb7ba923cd9b8d783e674e3a38000`.

## Observations and boundary

| Checked scope | Result |
|---|---|
| Artifact selection | **35** exact URL/file/version/SHA/byte pins; **47** active edges including seven SAM requirements, **262** excluded marker/extra requirements; no extras selected |
| Python download subtotal | **3,041,954,835 bytes**; plus expected single checkpoint **3,197,963,301**. Excludes SAM source/interpreter/compiler/tools/other stages; whole-plan costs remain null |
| Metadata/notice reads | Release JSON ≤2 MiB, PEP658 records ≤128 KiB; archive ranges ≤3 MiB/artifact with 206/range/ETag/If-Match/CRC checks. Successful archive inspections read **4,352,095 network bytes**, including duplicate ranges; failed retries are separately retained |
| Archive records | **156** records, including **56** notices and **12** vendored setuptools distribution records. Root METADATA matches hash-valid sidecars; nested records are separately identified |
| Whole-artifact SHA verification | Two small source archives (iopath/ANTLR) and four small wheels whose initial bounded read covers the complete file: MarkupSafe, portalocker, typing-extensions, wheel. Large wheels/model are unverified; only selected records and the two small sdists are retained on disk |
| Missing archive notices | Triton3.1.0 and ANTLR4.9.3; exact-commit supplemental source notices pinned below, without complete archive correspondence or independent review |

[PyPI's API](https://docs.pypi.org/api/json/) distinguishes publisher metadata
from uploaded contents. The audit verifies PEP658 digests and compares them with
the selected wheels' root METADATA; JSON alone does not prove payload contents.
The two source-only packages' literal setup requirements are inspected without
execution and independently reconciled against their egg-info requirement files.
Their generated-wheel/backend compatibility remains untested. Setuptools84.2.1/
wheel0.48.0 are build candidates, not build results. The selected manylinux floor
is 2.28; raw Linux Torch/native ELF closure is unresolved (host reports glibc2.44).

Initial filelock3.32.3/Hydra1.3.2/Pillow11.3.0 snapshots report advisories; the
proposal pins 4.2.4/1.3.8/13.0.0 instead, with none reported in those release
snapshots. Original records/IDs remain retained; no existing environment is
updated and no security review is inferred. Supplemental
[Triton source license](https://github.com/triton-lang/triton/blob/cf34004b8a67d290a962da166f5aa2fc66751326/LICENSE)
and [ANTLR source license](https://github.com/antlr/antlr4/blob/e4c1a74c66bd5290364ea2b36c97cd724b247357/LICENSE.txt)
have hashes/bytes in the candidate. All component, bundled-code and redistribution
review selections remain null.

The final [Torch2.5.1 base-release snapshot](https://pypi.org/pypi/torch/2.5.1/json)
reports **22 advisory records**, including aliases of the same issues; torchvision
0.20.1 reports none. These base-release records are separate from CUDA artifact
identity verification. [PyTorch's own advisory](https://github.com/pytorch/pytorch/security/advisories/GHSA-53q9-r3pm-6pq6)
identifies a checkpoint-loading issue through 2.5.1 even with `weights_only=True`,
which the pinned SAM builder calls. **This variant is blocked for provisioning**;
retain its exact metadata but replace/re-lock the pair before activation. The
2.6.0 fix is specific to this issue and does not clear all recorded advisories.

## Commands, failures and preservation

Use `python3 R/run_check.py TAG COMMAND...`; receipts record actual arguments.

- `collect.py`; `collect.py --sam-closure`: exit 0. Initial 15-package subset and
  expanded 33-package publisher selection retained separately.
- `collect.py --sam-closure --revise-vulnerable`: initial exit **1**, Pillow's
  all-release JSON exceeds the 2 MiB cap. Specific release metadata replaces
  that request; `revised-collection-complete-run` exits 0 without widening caps.
- `inspect_archives.py`: initial/retry/mismatch runs exit **1** because the helper
  mistakes vendored setuptools METADATA for the root. Correct root-path matching
  gives `archive-complete-run` exit 0 for **35/35**; prior mismatch evidence is a
  helper error, **not** a publisher mismatch. Successful prior records are rehashed
  and reused. No warnings or product limits are suppressed.
- `freeze.py`, `collect_notices.py`, `finalize_notices.py`: exit 0. The original
  draft is retained before adding supplemental notices and the native ABI caveat.
- `top_level_advisories.py`, `finalize_advisories.py`: exit 0; original pre-advisory
  draft retained and provisioning refusal added without changing selected artifacts.
- `oracle.py`: initial exit **1**, oracle omitted the selected raw Linux wheel
  tag. Correct target tags give `oracle-advisories-final-run` exit 0: exact publisher
  identities, dependencies, notice bytes, source egg-info and 3.12.0/15/99 marker
  checks agree; no patch-dependent edge is hidden.

Documentation links, `git diff --check` and the pinned Black26.5.1 check of all
tracked Python files pass. Dependency inventory default/local/strict checks exit
**0/2/3**: 86 components/seven profiles/257 gaps; local evidence still reports
`Current runtime payload differs`. No runtime receipt is refreshed. Final
preservation passes; exact command receipts are recorded in `R/finish.json`.
Of **987** tracked inputs, **982** are unchanged and five intended Markdown
updates are present; two new documentation/data files are added.
Protected unchanged inputs include all **378** current source/
product hashes (using the intake's explicit sample_2 replacement), **327** old
sample_2 job files and **62** existing environment METADATA files. No build,
selected-package install/import, model download, solve, fit or native GPU run is performed;
native/C++ suites are not rerun for JSON/Markdown changes. The next work is the
source/toolchain/cost and capture-budget handoff in [progress](../progress.md).
