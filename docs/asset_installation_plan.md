# Asset-worker installation proposal — updated 29 October 2026

**PROPOSED; provisioning BLOCKED; A0/AP0 remain IN_PROGRESS.** This is the explicit
installation plan for plan §§11.2/11.7's initial control pipeline. No installer
consumes this document, no executable asset recipe has been frozen, and no
component is approved by its inclusion here. The
[current readiness audit](validation/2026-10-28-a0-readiness.md) owns current local
observations; the [27 October audit](validation/2026-10-27-a0-exit-audit.md)
retains the original large-file blocker, and the [26 October audit](validation/2026-10-26-a0-exit-audit.md)
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

### Replacement Linux control variant — 28 October

The [replacement audit](validation/2026-10-28-sam2-patched-lock.md) selects
**Linux x86_64 /CPython3.12 /torch2.13.0+cu126 /torchvision0.28.0+cu126** from the
[publisher's versioned pair listing](https://pytorch.org/get-started/previous-versions/#v2-13-0).
The separate [41-artifact candidate](research/sam2_control_cu126_artifacts_20261028.json)
retains the old blocked lock unchanged. Both base-release snapshots report no
advisories; 2.13.0 lies outside the previously identified loader issue's affected
range. This is a metadata selection, not a security review, executed SAM
compatibility test, installation approval or host-fit result. Provisioning remains
**BLOCKED** on the remaining source/build/runtime/rights/cost prerequisites.

All **55** selected edges close, including explicit runtime/build roots. Torch now
requires `cuda-toolkit==12.6.3` with **eleven transitive extras**: cublas, cudart,
cufft, cufile, cupti, curand, cusolver, cusparse, nvjitlink, nvrtc and nvtx.
These required runtime selections do not select an `nvcc` compiler. SAM/Torch
optional extras remain unselected. CUDA bindings12.9.10 satisfy Torch's declared
range alongside that CUDA12.6 runtime; do not infer a compiler version from the
bindings version. SymPy1.14.0 satisfies Torch's raised minimum.

The candidate pins exact URLs, hashes and sizes for all **41** Python artifacts;
**19** unchanged selections reuse prior exact metadata/notice observations.
Bounded inspections retain **158** notice records and **12** vendored setuptools
metadata records. ANTLR and the CUDA toolkit metapackage have no separate archive
notice record; ANTLR's prior supplemental source notice is retained. All independent
reviews and complete source-to-binary/native runtime correspondence remain open.
Eight small artifacts have whole-payload hash verification; large wheels/model
remain unverified. No selected package is installed/imported or executed.

The Python-artifact download subtotal is **3,594,832,382 bytes**, or
**3,750,840,848 bytes** including the previously proposed single checkpoint.
The [source/build-input addendum](research/sam2_build_inputs_20261028.json) and
[source audit](validation/2026-10-28-sam2-source-inputs.md) extend the known subtotal
to **3,893,049,064 bytes**, including the verified SAM archive, a proposed CPython
archive and three compiler/header archives. Host compiler/sysroot/other tools and
other asset stages remain excluded; whole-plan installed/build/staging/scratch/
reservation costs stay null, with no cache credit.

The pinned SAM archive is **55,645,345 bytes**, SHA-256
`1f2fbfad3ffa38110368abac76c6ef9df9c282a66d5c2807bc94abf4d2fb30f8`.
Its bounded expanded tar is **65,054,720 bytes**. All **569** Git blobs agree in
bytes/modes: **565** regular files and **four** relative links targeting verified
regular files inside the source root. An independent GNU tar stream reproduces
all regular-file hashes and link targets. No members are extracted to build/import
paths or executed. This verifies this source archive, not a built/installed worker,
independently signed source, rights or resource fit. Controlled build staging and
source-to-installed correspondence still need implementation/verification.

The addendum proposes **CPython3.12.16**, standalone build **20261026**, with an
exact **34,505,639-byte** archive/advertised hash; existing 3.12.15 stays unchanged.
The 41-artifact graph is rechecked against 3.12.16 metadata. Proposed NVIDIA
redistribution components are **nvcc12.6.85 /cudart12.6.77 /CCCL12.6.77** with exact
publisher URLs/hashes/sizes, totaling **52,057,232 bytes**. None of these binary
archives is acquired, installed or executed; this is not complete build closure.

[CUDA12.6's host compiler policy](https://docs.nvidia.com/cuda/archive/12.6.3/cuda-installation-guide-linux/index.html#host-compiler-support-policy)
lists GCC6.x–13.2 and Clang7.x–18.0, allowing newer minor releases within supported
majors. Local GCC16.2.1/Clang23.1.4 fall outside those ranges. The scoped PATH
lookup finds no GCC12/13 or Clang18 commands; this is not an exhaustive machine
search. Select a compatible isolated host compiler/libstdc++/sysroot and exact
build tools; do not suppress version checks or change global compilers. Existing
CUDA-build flags, local-checkpoint requirement and no-download/no-null-checkpoint
policy below remain required. The 8 GiB development host is still contracts-only.

The source audit encountered **ENOSPC** during validation receipt writes; only its
reproducible expanded tar cache was removed. The user subsequently freed space on
the same volume. The [host-toolchain audit](validation/2026-10-28-sam2-host-toolchain.md)
observes **314,470,408,192 bytes** available and unchanged protected inputs.
The immediate capacity blocker is cleared; no recipe reservation/fit is claimed.
Preserve existing jobs/environments and complete the full reservation before staging.

### Proposed isolated host toolchain — 28–29 October

The [separate candidate](research/sam2_host_toolchain_20261028.json) pins **19**
conda-forge artifacts: GCC/G++ **13.4.0**, binutils **2.46.1**, matching GNU13.4
runtime/development packages, glibc **2.28** sysroot/kernel headers **4.18.0** and
support libraries. The registry reports GCC13.5 unavailable in these package
routes; 13.4 lies within NVIDIA's supported-major/minor policy. [GNU13 upstream
maintenance has ended](https://gcc.gnu.org/gcc-13/); downstream patch/advisory review
is required before acquisition/activation, alongside all existing terms/reviews.
This candidate is not a conda solver result, supported-OS or native ABI/build pass.

All **35** advertised dependency edges and **11** selected virtual requirements
reconcile against exact registry pins and local observations. No optional LLVM
OpenMP variant is selected. Bounded ZIP/ZSTD/tar inspection reads only `info`
members; all embedded dependency/constraint records agree. The tables declare
**4,152** disjoint payload paths/**950,786,485** logical bytes, **102** links and
**124** prefix relocations; **17** notice records are retained. Compiler payload
members/full archives remain unread/unverified; activation scripts do not run.
These declarations do not establish installed bytes, relocation correctness,
complete notices, source correspondence, patched channel metadata or runtime closure.

The toolchain download subtotal is **150,510,658 bytes**; the combined known
subtotal is **4,043,559,722 bytes**, still excluding other tools/stages and complete
installed/build/staging/scratch costs. All such whole-plan costs remain null;
provisioning is **BLOCKED**. No solver is installed and no global compiler or
worker environment is changed. Resolve the outstanding reviews/correspondence,
remaining tools and enforced budgets before preparing an activation proposal.

### Patched channel and recipe-source observations — 29 October

The [additive audit](validation/2026-10-29-sam2-channel-source.md) observes the
same **19** selections in patched channel shards, with exact identity, size,
dependency and constraint agreement. Eight commit-addressed feedstock archives
match **565** embedded recipe files. **57** rendered/configuration/helper-license
files remain unmatched; they are retained without asserting source equivalence.
The [separate observation](research/sam2_toolchain_review_20261029.json) leaves
all parent candidates unchanged. Activation templates modify compiler flags,
link search/rpath and sysroot/CMake paths; source-template matches do not validate
rendered payload scripts or actual Torch/NVIDIA/libstdc++ bindings. Upstream
compiler source archives, reproducible build, complete notices and independent
advisory/rights review remain open. No payload is installed or activated.
All whole-plan costs remain null/blocking, and the known download subtotal is
unchanged at **4,043,559,722 bytes**.

### Proposed Linux control variant — 28 October

**Do not provision this variant.** The completed metadata audit finds **22**
advisory records on the Torch2.5.1 base release (some alias the same issue).
[Upstream GHSA-53q9-r3pm-6pq6](https://github.com/pytorch/pytorch/security/advisories/GHSA-53q9-r3pm-6pq6)
affects `torch.load(..., weights_only=True)` through 2.5.1, the loader used by the
pinned SAM source. Its 2.6.0 fix addresses that issue, not every recorded advisory.
Retain this exact candidate as evidence. The replacement above has a separate
lock; it does not retroactively qualify this variant. No model was loaded.

The retained blocked candidate is **Linux x86_64 / CPython 3.12 /
torch 2.5.1+cu124 / torchvision 0.20.1+cu124**, matching the
[publisher's 2.5.1 CUDA 12.4 pair](https://pytorch.org/get-started/previous-versions/#v2-5-1).
This is a concrete candidate, not a complete dependency lock, installation action
or host-fit claim. The interpreter patch/build/hash and CUDA compiler/build
closure still need selection. No existing worker environment is upgraded or
credited as a reusable installation.

The [official Torch index](https://download.pytorch.org/whl/cu124/torch/) and
[torchvision index](https://download.pytorch.org/whl/cu124/torchvision/) advertise
the following exact CPython 3.12 Linux wheel identities. HEAD gives their expected
download sizes; complete payload SHA-256 verification has **not** occurred.

| Proposed file | Declared bytes | Publisher SHA-256 |
|---|---:|---|
| `torch-2.5.1+cu124-cp312-cp312-linux_x86_64.whl` | 908,202,405 | `bf6484bfe5bc4f92a4a1a1bf553041505e19a911f717065330eb061afe0e14d7` |
| `torchvision-0.20.1+cu124-cp312-cp312-linux_x86_64.whl` | 7,287,066 | `d1053ec5054549e7dac2613b151bffe323f3c924939d296df4d7d34925aaf3ad` |

Separate `.metadata` requests returned 404. Bounded HTTP ZIP ranges inspected only
the selected wheels' directories and dependency records: 1,404,199 and 85,289
network bytes respectively, under 3 MiB/wheel and 128 KiB/decoded record. The
[audit](validation/2026-10-28-a0-readiness.md) retains HTTPS/ETag/If-Match/range/CRC
and metadata SHA receipts. These are publisher-metadata observations, not full
wheel verification, full package extraction, installation or execution.

The exact Torch record requires **12 NVIDIA packages**, Triton **3.1.0** and,
for Python 3.12, SymPy **1.13.1** and setuptools, alongside other direct runtime
requirements. Torchvision selects torch **2.5.1**, NumPy and Pillow. Optional
extras are unselected. The [35-artifact candidate](research/sam2_control_artifacts_20261028.json)
now pins the Python dependency graph; the [transitive audit](validation/2026-10-28-sam2-artifact-lock.md)
owns its metadata/notice observations. Source/build/native runtime closure,
independent reviews and installed/build costs remain unresolved.

The candidate selects exact Linux x86_64/cp312 or compatible universal wheels,
plus source-only **iopath 0.1.10** and **antlr4-python3-runtime 4.9.3**. All **47**
declared edges, including SAM's seven requirements, resolve; **262** inactive
marker/extra requirements are excluded. Build candidates are setuptools **84.2.1**
and wheel **0.48.0**; their execution/backend compatibility is untested. Selected
manylinux tags extend to glibc **2.28**; raw Linux Torch wheels still require native
ELF/runtime inspection, not an inferred ABI pass. The interpreter patch/build
and CUDA compiler remain unselected.

Initial local-version candidates filelock **3.32.3**, Hydra **1.3.2** and Pillow
**11.3.0** have publisher-listed advisories. Retained snapshots preserve them;
the proposal instead pins **4.2.4 /1.3.8 /13.0.0**, with none reported in their
release snapshots. This is not a security review or permission to update existing
environments. **56** archive notice records and **12** vendored setuptools
metadata records are pinned. Triton/ANTLR archives lack separate notice records;
supplemental exact-commit source licenses are pinned, with archive correspondence
and all independent reviews still open. No broad permissive-license conclusion
follows from the top-level package declarations.

Propose `SAM2_BUILD_CUDA=1` and `SAM2_BUILD_ALLOW_ERRORS=0` for this explicit
extension-enabled variant. The pinned [setup source](https://github.com/facebookresearch/sam2/blob/2b90b9f5ceec907a1c18123530e92e794ad901a4/setup.py)
otherwise tolerates extension failures. A separately chosen extension-free variant
would need a different recipe/feature identity, not a silent build downgrade.
The [build-system requirements](https://github.com/facebookresearch/sam2/blob/2b90b9f5ceec907a1c18123530e92e794ad901a4/pyproject.toml)
also leave torch/setuptools open-ended: freeze build dependencies and prevent
build isolation/resolution from fetching another stack.

A future adapter must require the selected local config and checkpoint path
before calling the direct builder. The pinned
[builder](https://github.com/facebookresearch/sam2/blob/2b90b9f5ceec907a1c18123530e92e794ad901a4/sam2/build_sam.py)
allows a null checkpoint and provides hub-download helpers; neither is an allowed
fallback for this proposal. Freeze precision, postprocessing, frame/batch/pixel
limits and the automatic proposal/association method separately. No adapter or
resource enforcement is supplied by this document.

The earlier two-wheel/checkpoint subtotal was **1,071,497,937 bytes**. The new
35-artifact Python candidate totals **3,041,954,835 bytes**, or
**3,197,963,301 bytes** including that checkpoint. This is only a known download
subtotal; SAM source, interpreter/compiler/tools and other asset stages are excluded.
Six small artifacts have whole-payload hash verification from bounded reads;
the large wheels and checkpoint do not. No package is installed or executed.
Whole-plan download, installed, build/staging and scratch/reservation costs stay
**null/unknown**, with
no cache credit. Code/source inventory, the complete environment/build lock,
terms/independent reviews and enforced/measured stage limits still block any
installation/use proposal. Physical pilot preparation can proceed independently
using the [capture checklist](a1_capture_checklist.md).

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
bounds. These small-fixture profiles cannot verify this model or a complete
production environment. This remains a source-bound/declared-size comparison,
without model payload verification or an installation attempt.

The separate [external MODEL observation v2](protocols/asset_external_models.md)
now streams explicitly selected no-archive MODEL files, joining exact
path/size/hash and independently selected reviews under finite ceilings. Its
[synthetic validation](validation/2026-10-27-external-models.md) exercises files
at and twice the proposed size with bounded process memory. All v1 limits and
encodings remain unchanged. Mutable-file observations are neither privately owned
immutable payloads nor execution authority. The actual proposed checkpoint has
not passed this profile, and its reviews remain unselected. The
[shared frozen request/CLI](protocols/external_model_inspection.md) now replays
independently selected small snapshots and MODEL files with all global blockers.
Full native environment inventories/runtime closure,
whole-installation costs and actual resource enforcement remain open before
acquisition/activation can be concretely proposed.

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
