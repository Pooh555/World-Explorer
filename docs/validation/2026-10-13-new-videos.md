# New local videos — 2026-10-13, Asia/Hong_Kong

Scope/status: user-requested local runs using existing development binaries at
commit `1847a64`; M1 remains IN_PROGRESS. No qualification gate is certified.
Working directory: repository root. No code, dependencies or previous outputs
were changed; private inputs and generated jobs remain ignored and local.

`data/local/sample_1.mp4` matches the original video SHA-256
`51263d9991dcbc2136d0809ecba1a8ba1b261896f3b222b7afba19784a153ef6`;
its completed reconstruction was preserved without rerunning.

| Input | Inspected format | Sampling / decoded frames | Observed result |
|---|---|---|---|
| `data/local/sample_2.avi` | MPEG-4, 640×480, 45.4 s | 2 FPS / 91 | Exit 2 after 81.273 s: POSE_DISCONNECTED, three COLMAP models with 47/10/10 registered frames; no fit or scene publication |
| `data/local/sample_3.mp4` | H.264, 1920×1080, 32.111922 s | 3 FPS / 92 | Exit 2 after 241.313 s: POSE_DISCONNECTED, two COLMAP models with 22/43 registered frames; no fit or scene publication |

The AVI sampling covers the whole sequence within the existing 120-frame cap.
The MP4 sampler retains original timestamps; actual count is 92. Both requests
use explicit local workers, exhaustive guided poses, four CPU threads, a 2 GiB
RGB journal, and the unchanged 7,000-step fit recipe with 1280 maximum training
image dimension and two-million transient splat limit.

Commands actually launched with native device access:

```sh
python3 artifacts/new-videos-20261013-sample_2/run.py
python3 artifacts/new-videos-20261013-sample_3/run.py
```

Each supervisor invokes `build/dev/we_process reconstruct --request` with its
same-directory `request.json` using an argument array. `run.log` retains worker
events; `run-result.json` records both terminal exits and elapsed times.
Request hashes: AVI `ead7ffc665ae015024fa41a7eb1c3f75fa784588bea0270017a3fc8e84ae6e57`;
MP4 `b0cfb68514b455231a91df867405410a3bf4aacb74acda0294ff7fb170edea42`.
Input hashes: AVI `904f2c932e82e1aa0acf0682800993803b5089b25e424421074ef4f27df7721a`;
MP4 `31fa05e1c483228f8ac7b1f8ab0958e889625ae89b1e177276b57566ece28632`.

The disconnected AVI solve is retained under
`artifacts/new-videos-20261013-sample_2/poses/stages/.poses-owgcy_bj`, including
backend log and all three models. No partial model was accepted as a full scene.
The MP4 failed solve is retained under
`artifacts/new-videos-20261013-sample_3/poses/stages/.poses-5h4vilre`; its backend
log records both models. Both processes have terminated. Next: inspect model/frame
coverage and capture continuity before choosing separate coherent segments or
recapturing; do not merge disconnected models or silently accept a partial scene.
No new Gaussian scene or viewer result exists.
