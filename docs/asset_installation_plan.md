# Asset-worker installation proposal — updated 27 October 2026

**PROPOSED; provisioning BLOCKED; A0/AP0 remain IN_PROGRESS.** This is the explicit
installation plan for plan §§11.2/11.7's initial control pipeline. No installer
consumes this document, no executable asset recipe has been frozen, and no
component is approved by its inclusion here. The
[current A0 audit](validation/2026-10-27-a0-exit-audit.md) owns current local
observations; the [26 October audit](validation/2026-10-26-a0-exit-audit.md)
retains the original reference collection and its historical contract gaps.

## Proposed components and missing bindings

Reference commits below identify the upstream documents examined on 26 October.
They are proposed source references, not verified source archives, built binaries
or installed inventories. Raw document bytes/hashes are retained under ignored
`artifacts/validation/a0-exit-audit-20261026/primary/`; the receipt is
`primary-snapshots.json`. Changing the selected implementation requires a new
recipe identity and checks under the existing [recipe contract](protocols/asset_recipes.md).

| Stage | Proposed reference | Bind before installation/use |
|---|---|---|
| Calibrated views, depth, observed fusion | Preserve existing native M3 interfaces, [dense identity](../workers/identity/dense-development-20261020.json) and [fusion lock](../workers/fusion/environment-lock.txt) at project parent `10dc6b140b49fbce591dbf8ce5151a3db817a91c` | Independently select actual capture/pose/depth/geometry receipts. Existing worker environments are retained; their local evidence does not close asset-worker/runtime/rights qualification |
| Instance proposals/propagation | [SAM 2 code](https://github.com/facebookresearch/sam2/tree/2b90b9f5ceec907a1c18123530e92e794ad901a4), proposed **2.1 hiera tiny** checkpoint/config below | Code archive/hash/size; complete new Python/torch/torchvision/CUDA-build lock and file costs; component reviews; automatic proposal/3D association adapter; measured stage limits |
| Structure/completion control | First-party bounded plane/support fitting and constrained extrusion/symmetry; optional independently licensed local retrieval, as specified in §11.2 | Worker implementation and broader correspondence/quality coverage; bounded native patch/anchor/completion contracts now exist. Bind any library asset identities/terms. No library or generative checkpoint is selected; unknown regions stay explicit on rejection |
| UVs and mesh production | [xatlas `f700c7790aaa030e794b52ba7791a05c085faf0c`](https://github.com/jpcy/xatlas/tree/f700c7790aaa030e794b52ba7791a05c085faf0c), [meshoptimizer `f0a684f11ead064e176775c9b796f82a55910e92`](https://github.com/zeux/meshoptimizer/tree/f0a684f11ead064e176775c9b796f82a55910e92) | Source/archive identities and complete build closure, installed byte inventory, reviews, seam/correspondence/bake adapter. Neither is currently a project dependency |
| Shared material/light fit | [Mitsuba `aa01500f57770a4ef0b067693cae08b3e8a2ac73`](https://github.com/mitsuba-renderer/mitsuba3/tree/aa01500f57770a4ef0b067693cae08b3e8a2ac73) with first-party objective; proposed `llvm_ad_rgb` control | Exact release/wheel or source build plus matching Dr.Jit/LLVM closure, costs/reviews, objective/BRDF/color/glTF adapter and measured limits. Documented CPU differentiation is not a room-fit result |
| Headless bake and ordinary engine import | Blender and Godot remain proposed external tools | Select exact versions/builds/platform archives, hashes, all notices/terms and installed costs; bind an actual bake worker to the existing bounded native PBR/export path and verify ordinary import. No tool version or package is selected here |
| Convex prop production | CoACD remains a specification candidate, without a selected component here | Select implementation/archive/environment/terms/costs and enforce per-prop hull/vertex limits before use. Existing Jolt static structural triangles and ray diagnostics do not decompose props or qualify contact dynamics |

SAM 2's examined [installation instructions](https://github.com/facebookresearch/sam2/blob/2b90b9f5ceec907a1c18123530e92e794ad901a4/README.md#installation)
require Python ≥3.10, torch ≥2.5.1 and torchvision ≥0.20.1. The retained training
environment is torch 2.4.1+cu124 / torchvision 0.19.1+cu124. Use a **new isolated
Python 3.12 environment** and an exact resolved dependency lock; preserve the
training, pose, dense, fusion and metric environments. Offload options and the
tiny variant do not establish an 8 GiB device or host-memory fit.

### Proposed single-checkpoint acquisition

The public author repository [tree metadata](https://huggingface.co/api/models/facebook/sam2.1-hiera-tiny/tree/de431c4043854a71d8101e17995dfe596bf101a5?recursive=true&expand=false)
was read without credentials or model payloads. Proposed selection:

| Field | Expected identity, not a locally verified payload |
|---|---|
| Repository/revision | `facebook/sam2.1-hiera-tiny` / `de431c4043854a71d8101e17995dfe596bf101a5` |
| File | `sam2.1_hiera_tiny.pt` only |
| Download bytes | `156008466`, author LFS metadata |
| Expected SHA-256 | `7402e0d864fa82708a20fbd15bc84245c2f26dff0eb43a4b5b93452deb34be69`, author LFS object ID |
| Code config | `sam2/configs/sam2.1/sam2.1_hiera_t.yaml` at the code commit above; SHA-256 `f932eac1c6241e910031b2f000a81cd9f8a8d4896e2277ab5ffb721f378b188d` |

The safetensors file is a separate artifact; it is not selected or substituted.
No checkpoint bytes were downloaded, hashed or executed. Installed model/tree
bytes remain unverified; this expected file size cannot bound the environment,
temporary copies or whole installation. A future worker must consume the explicit
installed file, with no runtime hub fetch or fallback to another model.

The examined SAM source [Apache terms](https://github.com/facebookresearch/sam2/blob/2b90b9f5ceec907a1c18123530e92e794ad901a4/LICENSE)
have SHA-256 `c71d239df91726fc519c6eb72d318ec65820627232b2f796219e87dcf35d0ab4`;
the optional connected-component code has [separate BSD terms](https://github.com/facebookresearch/sam2/blob/2b90b9f5ceec907a1c18123530e92e794ad901a4/LICENSE_cctorch).
Model-card declarations and permissive top-level licenses do not clear the full
dependency/input/output graph. Local-use and distribution review selections remain
**UNREVIEWED**, with no selected review record. Source/config document hashes
cannot substitute for archive, environment or checkpoint payload admission.

## Artifact verification boundary before acquisition

The declared checkpoint above is **156,008,466 bytes (148.78 MiB)**. Current
[component evidence v1](protocols/asset_component_evidence.md) requires every
nonempty file/archive inside an owned receipt, with **16 MiB/file**;
[receipt admission](protocols/asset_receipts.md) and
[installed-tree inspection](protocols/asset_installed_components.md) each cap the
relevant total at **64 MiB**. The proposed single checkpoint exceeds all three
bounds. These implemented small-fixture profiles cannot presently verify this
model or a complete production environment. This is a source-bound/declared-size
comparison, without model payload verification or an installation attempt.

Before acquisition, implement a separately versioned, explicitly selected
large-artifact observation boundary with streamed hashing, finite per-file/total
budgets, exact byte/path/review pins, cancellation and caller-data preservation.
Keep mutable-file observations distinct from privately owned immutable bytes and
execution authority. Preserve all v1 receipt/evidence limits and encodings; raising
their constants or treating an external hash declaration as admitted payload
would not close this gap. Full native environment inventories/runtime closure and
actual resource enforcement remain separate requirements after this boundary.

## Reservation and activation sequence

For every selected code/environment/model/tool component, freeze artifact URLs,
exact revisions, SHA-256, download bytes, installed files/bytes, build/extraction
scratch, terms and independent review pins. Account for duplicated cache/install
copies conservatively. Preserve all prior installations and outputs.

Current whole-plan `download_bytes`, `installed_bytes`, `build_peak_extra_bytes`
and `reserved_disk_bytes` are **null/unknown**. No zero-cost cache credit is taken.
Required disk is all acquisition + installed + build/staging costs + job scratch;
any unknown blocks provisioning. Current free space is an observation, not a
reservation or evidence that the recipe fits.

1. Resolve the complete component/variant/toolchain lock and these missing costs;
   bind independent terms/reviews through the existing
   [component evidence](protocols/asset_component_evidence.md) contract. Stop on
   unknown costs, missing rights or an unsupported device/profile.
2. Freeze per-stage input/view/pixel/resolution/batch limits, RSS/VRAM/scratch/time
   ceilings and retry/cancellation policy. Proposed scheduling is one queued job,
   one heavy worker at a time, four CPU threads and at most three completion
   candidates/object. These are proposed caps, not measured fit or AP8 passes;
   unspecified numeric limits keep the plan blocked.
3. Acquire only the explicitly selected artifacts into a new ignored
   `artifacts/install/assets/<lock>/` tree after explicit provisioning. Verify
   sizes/hashes before extraction/loading; reject path escapes and excess expanded
   sizes. Retain acquisition records. Install into new lock-specific environments;
   use the complete hash lock with network disabled during installation/run.
4. Reconcile closed installed inventories and selected reviews; capture actual
   import/runtime mappings and notices, then exercise worker limits and failure/
   cancellation/recovery. The existing [installed-tree inspector](protocols/asset_installed_components.md)
   is one check, not execution authorization. Publish immutable receipts last;
   failed staging preserves all committed/input bytes.
5. Bind the qualified implementation/config/seed and original capture/role pins
   into the pilot/evaluation freeze before tuning. Run the actual A1 pipeline and
   native completed/PBR viewer plus ordinary Godot import under the unchanged
   product gates. Retain rejects/partials and all assistance; no installed package
   or synthetic admission substitutes for those results.

## Advanced candidates and device scope

Current host observations are 8,188 MiB reported GPU memory and 15.24 GiB RAM.
It remains contracts-only for the asset track. Individual bounded CPU/control
experiments still need an explicitly installed and measured recipe. H_AP24/H_AP48
remain proposed capacities in §11.7; this plan authorizes no hardware purchase or
remote inference.

[SAM 3D setup](https://github.com/facebookresearch/sam-3d-objects/blob/f91db411c50efee93d8db7aeb323885650f6f722/doc/setup.md)
requires ≥32 GB NVIDIA memory and gated checkpoint access;
[TRELLIS.2](https://github.com/microsoft/TRELLIS.2/blob/75fbf0183001ed9876c8dbb35de6b68552ee08bd/README.md)
requires ≥24 GB. These stock recipes do not fit this host by their published
requirements. The 24 GiB H_AP24 capacity is below SAM 3D's documented minimum.

The examined [TRELLIS setup](https://github.com/microsoft/TRELLIS.2/blob/75fbf0183001ed9876c8dbb35de6b68552ee08bd/setup.sh)
still selects nvdiffrast v0.4.0 and the renderutils fork. Their exact
[nvdiffrast terms](https://github.com/NVlabs/nvdiffrast/blob/253ac4fcea7de5f396371124af597e6cc957bfae/LICENSE.txt)
and [fork terms](https://github.com/JeffreyXiang/nvdiffrec/blob/b296927cc7fd01c2ac1087c8065c4d7248f72da4/LICENSE.txt)
retain §3.3 noncommercial limitations. A permissive wrapper is insufficient for
the intended distributable recipe: affected paths require permission or a
replacement and requalification. TRELLIS/SAM 3D/GenRecon and the other advanced
challengers stay outside this proposed initial installation set; the
[dated candidate assessment](research/offline_asset_pipeline.md) retains their
individual unresolved rights/resource/adapter gaps. No advanced checkpoint size,
complete environment cost or successful low-memory port is inferred here.
