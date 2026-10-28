# Replacement SAM artifact candidate — 28 October 2026

**A0/AP0 IN_PROGRESS; provisioning BLOCKED; A1 NOT_STARTED.** A separate
[41-artifact candidate](../research/sam2_control_cu126_artifacts_20261028.json)
selects Linux x86_64/CPython3.12/torch2.13.0+cu126/torchvision0.28.0+cu126. The
[publisher's versioned listing](https://pytorch.org/get-started/previous-versions/#v2-13-0)
and exact wheel indexes bind the pair. Its base releases are outside the prior
loader advisory's affected range and report no advisories in retained snapshots;
this is not a security review or executed SAM compatibility/fit result. The
old blocked lock is unchanged. No installer, model or reconstruction is run.

Cwd `/home/Pooh555/programming/World-Explorer`; timezone Asia/Hong_Kong;
clean parent `012a5e50ae8434822d81e145103f6536e042d652`.
Ignored `R=artifacts/validation/a0-sam2-patched-20261028` retains scripts, raw
metadata/notice bytes, HTTP identities and argv/cwd/exit/time receipts.
Candidate SHA-256:
`b550c33e6e7118f542a7f79014bc193fa14e005146246dcaace885e51591824d`.

## Checked selection

| Scope | Actual result |
|---|---|
| Exact graph | **41 artifacts /55 selected edges /307 inactive requirements**; **19** unchanged artifacts reuse exact prior evidence; all selected release snapshots report no advisories |
| Required extras | Torch requires `cuda-toolkit==12.6.3` with eleven explicitly recorded extras. Optional SAM/Torch extras are excluded; the metapackage does not select `nvcc`. CUDA bindings12.9.10 and SymPy1.14.0 satisfy actual requirements |
| Download subtotal | Python artifacts **3,594,832,382 bytes**; plus expected single checkpoint **3,750,840,848**. Excludes source/interpreter/compiler/tools/other stages; installed/build/staging/scratch and whole-plan costs remain null |
| Archive evidence | **158 notice records /12 vendored metadata records**; root wheel METADATA agrees with hash-valid publisher sidecars. **3,148,899** successful new range bytes, excluding retained failed/timeout attempts; 19 reused observations make no new network/installed-cache credit |
| Whole hashes | **8** small artifacts verified: prior iopath/ANTLR sdists and four small wheels, plus CUDA toolkit/pathfinder wheels read wholly within the bounded initial range. Large wheel/model SHA verification remains pending |
| Missing notices | ANTLR and CUDA toolkit metapackage; prior exact-source ANTLR notice retained. All independent/bundled-component/redistribution review and complete native runtime closure remain open |

Range checks require 206, exact length/Content-Range/ETag/If-Match, bounded inflate
and ZIP CRC. Network budget remains **3 MiB/artifact/pass**, central directory ≤2 MiB,
decoded selected records ≤2 MiB, individual record ≤512 KiB. This audit permits
128 notice/metadata records instead of the old helper's 64; it changes no product
admission limit. Nearby records are grouped into bounded ranges.

The source-tree API reports **655 entries /569 blobs /64,563,748 blob bytes**, four
symlink blobs and no gitlinks at the pinned SAM commit. Five retained fragments
match declared Git blob SHA-1 and byte counts. `R/source-inventory.json` binds the
response and fragment SHA-256 values. No source archive is downloaded; its HEAD
has no Content-Length. Blob totals are not compressed download, installed or
staging costs, and tree metadata is not complete source-content verification.

## Commands and retained failures

Commands use `python3 R/run_check.py TAG COMMAND...`.

- `resolve.py`: initial exit **1** on a legacy non-PEP440 `0.5.13-hg` release
  label; invalid versions are excluded. `resolution-complete-run` exits 0.
- `select_pair.py`: initial exit **1**, Torch has **100** selected records, above
  the helper's 64-record bound. Subsequent bounded attempts retain range timeouts.
  `check_alias.py` verifies the alternate official `download.pytorch.org` path
  against the index artifact/hash and HEAD size; `pair-official-endpoint-run`
  exits 0. No full-wheel fallback occurs. Exact range length reads avoid requesting
  beyond a declared range; timeouts permit at most two attempts charged against
  the same byte budget. Unknown bytes received in timed-out attempts stay explicit.
- `inspect_dependencies.py`: exit 0 for **39/39** supporting artifacts; successful
  prior observations are rehashed. `freeze.py` and `oracle.py`: exit 0 for the
  final candidate, original publisher pins, independently derived required-extra
  closure, source egg-info versus setup literals and 3.12.0/15/99 marker checks.
- `source_inventory.py`: exit 0; read-only tree/fragment/HEAD audit above.

Documentation links (**1,805 targets /334 files**), whitespace and Black26.5.1
checks on all tracked Python files pass. Inventory default/local/strict checks
exit **0/2/3**: 86 components/seven profiles/257 gaps; local evidence reports
`Current runtime payload differs`. No runtime receipt is refreshed.
Preservation checks pass for **378** current source/product hashes, **327** old
sample_2 job files, **62** environment METADATA files and **383** prior audit files.
Of **989** tracked inputs, **984** are unchanged, five Markdown updates are
intended and two documentation/data files are new. `R/finish.json` retains exact
results and counts. Production/specification/runbook and private media are preserved.
No C++/native rebuild is needed for JSON/Markdown changes.
Source archive/toolchain/cost and capture-budget tasks remain in
[progress](../progress.md); no AP/Q/P/N/E or platform qualification is upgraded.
