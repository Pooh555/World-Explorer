# SAM compiler channel and recipe-source audit — 29 October 2026

**A0/AP0 IN_PROGRESS; provisioning BLOCKED; A1 NOT_STARTED.** All 19 selected
host-toolchain artifacts agree with the patched channel snapshot. Eight
commit-addressed feedstock archives match 565 embedded recipe files; 57 other
recipe metadata/helper files remain unmatched. The
[additive observation](../research/sam2_toolchain_review_20261029.json) preserves
all prior candidates. No compiler payload is installed, activated or executed;
no AP/Q/P/N/E or native platform gate is upgraded.

Cwd `/home/Pooh555/programming/World-Explorer`; 29 October 2026, Asia/Hong_Kong;
clean parent `05e88ca8cbe9c1192eec03f850e4dab0fa6a019c`.
Ignored `R=artifacts/validation/a0-sam2-channel-20261029` retains raw responses,
compressed shards/source archives, selected embedded metadata, scripts and
argv/cwd/exit/time receipts. Observation SHA-256:
`5bc52bb180898321d95f42b0f327920129b7dcc4c0f8d119fb6f2305c0774172`.

## Actual observations

| Check | Result and scope |
|---|---|
| Patched channel | 19 selected filenames present, none removed; version/build/subdir/SHA-256/size/dependencies/constraints agree with the unchanged parent. Two indexes plus 19 content-hashed shards: **1,781,650 bytes** |
| Embedded records | 818 metadata/recipe/notice files retained from 19 `info` members; **653,195 successful HTTP range bytes**. Previously selected metadata/notice hashes remain identical. Compiler payload members remain unread |
| Feedstock source | Eight exact commit-addressed archives, **808,011 bytes**; **565** embedded recipe files match source bytes |
| Unmatched files | **57** entries named `meta.yaml`, `conda_build_config.yaml`, `rendered_recipe.yaml`, `variant_config.yaml` or `recipe-scripts-license.txt`. All are retained and listed, without asserting correspondence. No script/patch mismatch is hidden in this count |
| Costs | Known proposed downloads unchanged at **4,043,559,722 bytes**; no cache credit, complete installed/build/staging/scratch costs still null/blocking |

[CEP16](https://conda.org/learn/ceps/cep-0016/) defines the compressed index/shards
and content-addressed hashes. Audit bounds: 4 MiB compressed channel object,
16 MiB decoded index/64 MiB decoded shard. The installed pip-vendored msgpack1.1.2
decoder is recorded, not installed by this audit; verification reparses with
rejection of duplicate map keys. Package inspection retains the previous 2 MiB
network/package/pass, 1 MiB compressed info member, 32 MiB decoded tar,
8 MiB record and 1,024-entry limits. Feedstock archives have 8 MiB compressed/
32 MiB decoded/4,096-entry/8 MiB member bounds; no files are installed or executed.

GCC recipes select GCC13.4.0 plus zlib1.3.1; matching downstream patches do not
establish incorporation of every GCC13.5 fix or current advisory clearance.
[GNU13 maintenance has ended](https://gcc.gnu.org/gcc-13/). The sysroot recipe
selects AlmaLinux8.9 glibc2.28-236.el8_9.13 RPMs, separately from the running host
libc. Activation templates alter compiler flags, rpath/search paths, CMake tools
and sysroot; their source matches do not validate rendered payloads or actual
Torch/NVIDIA/libstdc++ bindings. The source archive URLs, commits and hashes are
in the additive observation. Full upstream compiler source/archive/build/notice
correspondence, independent advisory/rights review and payload/runtime tests
remain open; this audit clears no execution authority.

## Commands and verification

Use `python3 R/run_check.py TAG COMMAND...`.

- `snapshot.py`: exit 0; clean parent, 995 tracked inputs, 378 protected source/
  product identities, 327 old sample_2 job files, 62 environment metadata files
  and 945 preceding audit files. Available bytes: **312,487,481,344**; not reserved.
- `channel.py`, `inspect_recipes.py`: exit 0; exact channel and embedded records
  retained without a full compiler archive or package-payload read.
- `feedstocks.py`: exit **1**, public Git tree API HTTP403 rate limit. No API result
  is used. `source_archives.py`: exit 0 through the same recorded exact public
  source commits, without credentials or source execution.
- `freeze_review.py`, `oracle.py`: exit 0; parent, snapshots, selected identities,
  dependencies/constraints, source matches and unchanged blocked costs reconcile.

The companion [native-view check](2026-10-29-sample2-native-views.md) runs in this
slice. Final documentation/style/preservation command results are recorded there
and in `R/finish.json`. Production code, specification, runbook, parent candidates,
media, old jobs and environments remain unchanged. These JSON/Markdown changes
need no native rebuild. The next concrete ownership/producer and toolchain tasks
are in [progress](../progress.md).
