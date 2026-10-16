# Resident-copy lifetime completion — 2026-10-16 Asia/Hong_Kong

Scope/status: **M2/N1/P4 IN_PROGRESS**; the local Linux/NVIDIA two-hour
full/full/empty ownership diagnostic passes. This does not certify physical
device loss, other platforms, reference hardware or full Q/P/N/E gates.
Cwd: repository root. Implementation: `13e94bf`; launch recorded parent
`3c120ce` with dirty-source hashes, all seven of which match `13e94bf`.
Profile: GCC 16.2.1 Debug C++26, Vulkan validation, NVIDIA RTX 4060 Laptop,
driver 615.78.08, native Wayland. Evidence root **R**:
`artifacts/validation/m2-completion-20261016`.

The supervisor's actual process exit is **0**, with the executable unchanged
through termination. The child reports **7,200.746116231 s**, **480 visible
updates**, final revision **481**, **359 resizes**, **304,047 submitted frames**
and **zero Vulkan errors**. The independent audit checks the bound launch/log/
binary/archive/source/manifest hashes, stdout-summary equality, 119 samples,
consecutive periodic revisions 2–480 and final revision 481, the full/full/empty
sequence, all resize extents, monotonic counters and allocation bounds.

All **13 readbacks** match the original full-scene SHA-256
`107ceeb0c2cb975041af83db2283e0d6f3673c06d8503f1109c9cd3955036613`.
The run observes **12,197,960,192 host-copy bytes** and
**12,122,193,920 resident-copy bytes**, exactly matching 161 full host uploads,
160 empty 16-byte uploads and 160 full resident copies. Peak tracked owned
allocations are **520,936,256 bytes**, below 536,870,912; process RSS high-water
is separately **535,883,776 bytes**. These are lifetime observations, without a
performance warmup, percentile or reference-device throughput claim. Normal
desktop activity and project CPU/build checks were not isolated throughout.

Actual commands, both exit **0** (R expanded to the path above):

```sh
python3 artifacts/validation/m2-completion-20261016/supervise_lifetime.py
python3 artifacts/validation/m2-completion-20261016/audit_resident_lifetime.py
```

The supervisor launches `build/dev/we_gpu_lifetime_stress` with the unchanged
`artifacts/sample_1/scene.wexplore`, output `R/resident-full`, duration `7200`
and `--resident-reuse`. It records actual child termination in
`resident-full-exit.json`; the auditor publishes `resident-full-audit.json`.
The saved scene retains **295,952 splats**, arbitrary scale and no collision.
Manifest/content identities are unchanged from the
[earlier staged-run audit](2026-10-16-m2-staged-lifetime.md).

| Bound artifact | SHA-256 |
|---|---|
| Executable | `d56dcde43cb8653d3796db23629dfa17980f03e63db003cc5d983db0a2749946` |
| Launch receipt | `edfd61971f1b31c66d6b95f6474553a465fea22469efde3f1b23bbdf35f88df0` |
| Summary | `be9623b988a921522080fcb8fdde77c9b7be4eaf8bfdbd1fe24ea83ec0f4c707` |
| Events | `8e3e0c06eb47aced0029e9202d007fedf5e08bf141de1d2986386f24e82ad142` |
| Stdout | `e8d72937f30486a33fb93b6eefdd5d55d0bd1cf1bc5ab16cec1200dd7da16ada` |
| Auditor | `c64ff87d870da12d18cd2512abe484792634021756a1518bd4539aa84c28634a` |

No production code, source data, reconstruction, environment or gate threshold
changes in this evidence slice. The earlier staged run's missing original exit
is not retroactively supplied by this separate run. Q6 packing comparison and
fresh current-byte runtime receipts remain due.
