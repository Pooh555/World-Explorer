# Offline asset production: research assessment

Checked 17 October 2026, Asia/Hong_Kong. This is the dated evidence supplement to
[technical plan §11](../technical_plan.md#11-offline-asset-production-track).
The specification owns scope, contracts, gates and milestones; this file owns
candidate comparisons and external sources. Nothing here is an installed lock,
independent reproduction, product qualification or permission to provision models.

[ADR 019](../adr/019-assets-before-platforms.md) changes execution timing to local
A0–A5 after M3/before M4, with A6 later. Candidate/source observations below retain
their original research scope; rescheduling is not a new model evaluation.

## Recommendation and evidence limits

Use calibrated original video views as the common evidence, with separate observed
geometry, instance masks and appearance products. Build objects and structural
surfaces in that frame; complete unsupported regions using constrained geometry
and a licensed local asset library first. Evaluate generative completion as a
replaceable worker. Fit materials and scene lighting jointly against the original
views; validate under lighting withheld from fitting. Compile static meshes, UVs,
bakes, collision and LODs into reusable definitions and scene instances.

This is a justified project baseline, **D**, not a globally optimal model claim.
The established Phase 1 SfM/MVS path minimizes new dependencies and preserves
measurement provenance. It cannot recover hidden truth or guarantee intrinsic
materials from arbitrary video. The advanced default must automatically return
success, partial output or rejection; human repair cannot be a hidden stage.

Notation follows plan §1: **S** means one primary source family, **R** research,
**D/T** our decisions/targets and **U** unverified. Papers, author repositories,
model cards and demos from the same authors are one family. All external results
below are **S/R**, not independently reproduced here. No independently reproduced
benchmark retrieved in this review establishes the complete requested product.
Repository visibility and linked checkpoints verify release availability only;
payload bytes, hashes, compatibility and successful offline execution remain U.
Current branch pages are not immutable dependency pins. Dates below identify a
paper or explicit release notice, not an invented latest commit date.

Commercial screens distinguish permissive candidates, conditional custom terms,
explicit noncommercial blockers and unresolved weight/dependency scope. They are
not completed distribution clearance. In particular, SAM terms are not a blanket
noncommercial license: their grants remain subject to redistribution notices and
restricted uses. Source/model rights and generated-asset/input rights stay separate;
an MIT wrapper does not clear a transitive restricted implementation.

## Capture, observed geometry and decomposition

Unless stated otherwise, exact minimum VRAM, RAM, installed storage and W1 runtime
are **U** for every candidate below. They depend on resolution, frame/object count,
native extensions and retained stages. An API that accepts CPU tensors is not
evidence of CPU inference. Lower memory operation is U unless the upstream source
explicitly documents it. Plan §11.7 supplies proposed capacity profiles, not new
upstream minimums. Code, weights, dependencies and input/output asset rights must
be screened separately before adoption.

| Candidate / role | Inputs → outputs; evidence and consistency | Availability, terms and integration limits | Platform / resource evidence |
|---|---|---|---|
| COLMAP + Open3D; observed baseline | Calibrated overlapping RGB → poses, consistency-filtered depths → TSDF/mesh. Shared cameras support multiview checks; low texture, reflections, occlusion and wrong intrinsics leave holes. Fusion does not turn filled holes into observations | Existing Phase 1 selection, not a new solver. BSD-3 COLMAP / MIT Open3D cores; optional binary dependencies separate. Keep raw image identities, optical Z/range conversion and valid masks [R01, R02]; exact project pins remain in existing receipts | CPU sparse solve/fusion; qualified dense device required. Bound image/thread/volume budgets. Current installed pycolmap without CUDA cannot execute the planned dense worker; do not confuse that with missing hardware |
| SAM 2.1; baseline mask propagation | Images + points/boxes/masks → image masks/video masklets. Automatic image mask proposals can seed propagation. Does not discover semantic inventory or provide enduring 3D identities; occlusion/re-entry and part/object ambiguity need geometric association | Released checkpoints and code; Apache-2 plus optional connected-component BSD notice. Original models dated 2024-07-29; 2.1 dated 2024-09-29 in model table / 09-30 announcement. Preserve both date contexts [R03] | GPU/PyTorch, Linux-oriented instructions; Windows recommends WSL. Tiny/small are official model variants; CPU frame/state offload is exposed in predictor source, not an 8 GiB qualification [R04] |
| SAM 3 / 3.1; semantic discovery challenger | RGB + fixed text/exemplar prompts → masks, detections, video IDs. Better automatic inventory interface; vocabulary omissions, duplicate parts and 2D identity switches remain. Fuse projected depth and tracks; never use tracker IDs as asset IDs | SAM 3 launched 2025-11-19; 3.1 release 2026-03-27. Code/checkpoints available with access gate; custom SAM License, not SAM 2's Apache license. Redistribution/use restrictions require product review [R05–R07] | Python ≥3.12, torch ≥2.7, CUDA ≥12.6 in README; isolate from existing worker envs. 3.1 benchmark uses H100; no minimum VRAM or supported 8 GiB video configuration established |
| Grounding DINO + SAM 2; detection fallback | Fixed category phrases + RGB → boxes → SAM masks/propagation. Does not remove vocabulary dependence or solve 3D association. Automatic proposals plus geometry clustering must account for unrecognized objects | Official ECCV 2024 implementation and linked checkpoints; Apache-2 repository; checkpoint/transitive artifact audit still due [R08]. Extra detector/threshold adapter versus unified SAM 3 | PyTorch/CUDA and documented CPU mode; CPU throughput and total detector+tracker peak memory U. No matched room-video benchmark |
| VGGT; optional geometry initializer | RGB views → camera/depth/point estimates; align/refine against canonical SfM and holdout geometry. Priors can improve weak surfaces but do not supply observed metric scale | CVPR 2025. Code custom VGGT License/AUP; only `VGGT-1B-Commercial` has commercial checkpoint terms; original weights remain NC. 2026-05-15 memory fix is not a new rights grant [R09] | GPU/PyTorch; view count drives memory. CPU/offload consumer recipes unqualified. Existing plan §6.3 retains DA3 and other depth alternatives; no checkpoint substitution by family name |

Our association algorithm is new engineering: lift masks only through valid
depth/triangulated tracks; accumulate cross-view overlap, reprojection, appearance
and exclusion evidence; maintain split/merge lineage and uncertain assignments.
Track each chair separately even when visually identical. A movable object can
contain disconnected pieces; connected components alone are insufficient. Freeze
an automatic category/proposal policy before evaluation and report unassigned
foreground. A local language-model agent is not required to enumerate the pilot.

## Structure and completion

| Candidate / decision | Inputs → outputs; observed versus generated; failure modes | Availability and commercial screen | Platforms, memory, runtime and burden |
|---|---|---|---|
| Plane/support fitting + constrained local retrieval; baseline **D** | Observed mesh/depth/masks → bounded planes, openings, support graph and completed props. RANSAC/region fitting and reprojection constraints; symmetry/extrusion/library matches are inferred/generated. Wrong symmetry, occluded openings and library mismatch can fail | First-party orchestration with existing Open3D; use only locally installed, authored or redistribution-cleared assets with per-asset terms. No general model weights required. Separate fallback retains unknown regions and returns partial | CPU geometry/optimization; GPU optional. Implementation, room benchmarks and time U. Small initial library reduces coverage rather than justifying invented certainty |
| SceneScript; structural comparator | Semi-dense point clouds → structured scene description. Useful walls/doors/windows/object language; input distribution is Aria MPS, not our MVS clouds. Hallucinated layout and domain shift require validation | ECCV 2024; inference code and models available, CC-BY-NC-4.0. Exclude commercial default [R10]. Architectural vocabulary informs contracts, not copied implementation | Linux-only tested, torchsparse/CUDA; no established minimum memory or matched runtime. Adapting MVS clouds and model rights adds burden |
| SAM 3D Objects; object completion challenger | Single image + instance mask → complete object shape/texture/layout, mesh/splat products. Strong occlusion prior; far side is generated. Independent per-view generation is not multiview fusion; anchor candidates then constrain to all observations | Released 2025-11-19; paper submitted 2025-11-20; code and gated checkpoints available. SAM License covers both; dependency/checkpoint/output rights review remains required [R11–R13]. Not a floor/room-shell solver or proof of PBR disentanglement | Official setup: Linux 64-bit, NVIDIA ≥32 GB VRAM. Kaolin/PyTorch3D/CUDA and environment patches raise burden. Smaller community ports do not establish official lower-memory support; runtime/RAM/disk U |
| TRELLIS.2; shape/PBR generation challenger | Single object image → O-Voxel shape/material attributes → mesh/GLB; shape-conditioned texture example also released. Generated hidden shape/materials; normalization loses scene scale/pivot. No guaranteed multiview conditioning or game-topology validity | Paper 2025-12-16; released code, 4B weights and training code. MIT code/model card, **but default setup includes restricted nvdiffrast v0.4.0 and nvdiffrec renderutils** [R14–R18]. Obtain rights or replace those paths and requalify; MIT wrapper alone is insufficient | Official README: Linux tested, NVIDIA ≥24 GB. H100 author time only. CUDA/Triton/CuMesh burden. Lower-memory community wrappers U; setup's HIP branch is not documented end-to-end Radeon support |
| GenRecon; scene completion challenger | Posed multiview RGB → jointly generated overlapping scene chunks with PBR mesh export. More directly matches video/scene consistency than single-image generators. Chunk meshes do not themselves establish semantic object separation or stable reusable assets | Paper 2026-05-22; author reports code/checkpoint release 2026-06-29; source and three checkpoint links present. MIT code/model statement, separate weights-host terms not retrieved. Setup inherits restricted renderer dependencies; quarantine until cleared [R19, R20] | CUDA/PyTorch/TRELLIS.2 extensions, Linux setup; numeric minimum VRAM/RAM/runtime U, do not inherit 24 GB as a proven GenRecon bound. Projection batching documented; no verified low-memory recipe. Need object/shell decomposition after generation |
| 3D-RE-GEN; architecture reference, reject stock pipeline | Single image → segmented generated objects/background → ground-plane constrained assembly. Good example of completion plus layout. Single-view fidelity does not establish consistency with a video or true hidden geometry | Paper 2025-12-19; source MIT with separate VGGT/SAM/Hunyuan terms. Installation documents nano-Banana/Google Gemini/Banana.dev stages: stock recipe fails local-only requirement [R21, R22]. Replacing them is a new method, not a verified upstream recipe | Python 3.10/CUDA 12.1, multiple model/Blender stages. No dependable full-pipeline memory/time bound. Remote operations must be removed rather than merely hiding API keys |
| RecGen; RGB-D multiview comparator | RGB-D, intrinsics, masks → textured object/splat + camera-frame 6-DoF pose. Multiview anchor fusion improves conditioning; requires aligned depth. Generated completion and baked color are not measured hidden shape/PBR | 2026 paper (arXiv 2604.27106), ECCV 2026 repository; training/inference/checkpoint/evaluation links available. TRI NC license plus upstream restrictions excludes commercial baseline [R23, R24] | CUDA/spconv/rasterizer stack; exact minimum VRAM/RAM/time U. Adapter must convert xyzw pose to wxyz and explicit depth units; disable fitted post-hoc test color corrections |
| Hunyuan3D 2.1; alternative object generator | Image → shape + PBR texture generation. Completion rather than measured reconstruction; requires world registration and multiview validation | Weights dated 2025-06-14. Custom terms exclude EU/UK/South Korea territory, condition large-user commercial use, require notices and restrict using outputs to improve other models. Not a worldwide default; dependencies/Real-ESRGAN checkpoint separate [R25, R26] | Author lists 10 GB shape / 21 GB texture / 29 GB combined, supports Windows/Linux/macOS at project level; exact acceleration tuple must be probed. Serial-stage numbers do not prove total 8 GB support. Runtime/RAM/disk U |
| LaMa; limited background texture challenger | RGB + hole mask → completed image. Apply to one structural UV atlas and reproject, not independent frame fills. Does not create consistent 3D geometry, BRDFs or correct shadows | WACV 2022; source/checkpoint links present, code Apache-2. Old weight links failed per README; separate selected checkpoint terms/provenance unresolved; no commercial checkpoint clearance assumed [R27] | Legacy PyTorch environment; local inference, exact supported device tuple/peak memory/time U. Small texture holes only; scene-level completion needs geometry/lighting constraints |

SceneReGen (paper submitted 2026-08-25) also separates generated object orientation
from scene translation/scale. Its primary abstract reports 3D-FUTURE results; code,
weights, licenses and resources were not established in this review. Keep it on
the watch list, not in an executable baseline [R28]. PolyFit assumes a closed
model with necessary planes; that is a poor default for incomplete rooms and
openings [R29]. Full-room Manhattan assumptions likewise require evidence, not
automatic enforcement on every room.

The released 3D-RE-GEN configuration explicitly enables `use_banana`, names a
Gemini image model and sets `use_baked_image_only=true` with global material
defaults; its runner selects the nano-Banana inpainting script. Those are concrete
locality/material-integration gaps, not evidence of automatic intrinsic recovery.
Changing a flag alone does not qualify a replacement completion/relighting method
[R22].

## Geometry production, material recovery and export

| Candidate / role | Input → output; strengths and limits | Availability / terms | Resources and integration |
|---|---|---|---|
| gsplat 2DGS; surface-aware challenger | Calibrated original images/SfM → trained oriented surface splats/depth → TSDF mesh. Requires retraining its representation; arbitrary legacy 3D splat PLY is not a valid drop-in. Better surface bias still leaves missing sides and baked appearance | Current example code available, Apache-2 gsplat; compare with original SIGGRAPH 2024 2DGS, whose license/dependencies are restricted. Audit implementation separately [R30, R31] | PyTorch/CUDA; numeric minimum/our runtime U; lower-resolution experiments are changed recipes. Retain direct MVS as control |
| MILo / SuGaR / GOF family; research alternatives | Surface-aware splat fitting/extraction from calibrated views. MILo couples mesh and splat optimization; not semantic separation, UV/material recovery or completion | MILo SIGGRAPH Asia 2025 code available; original 3DGS and submodule terms block assumed commercial use [R32]. Existing plan §6.4 retains SuGaR/GOF comparison context; no new deployment selection | CUDA, trained representation/config-specific extraction; low/high-resolution modes are documented but no device minimum established. Extra fit/mesh alignment burden; production superiority U |
| xatlas + meshoptimizer; production baseline | Valid triangle meshes → charts/atlas, seam-aware simplification/cache optimization and LOD inputs. Does not repair arbitrary topology or perform semantic retopology | Available MIT libraries; exact release locks still due [R33, R34]. No weights. Portable native components suitable for screening into distributable recipe | CPU; RAM/time grow with mesh size. Numeric bounds U. Preserve sharp/material/UV seams and observed geometry; bake detail after low mesh/UV freeze |
| PaMO; repair challenger | Mesh → remeshed/simplified/projected surface. Can help intersection quality but closing surfaces may erase openings/thin sheets; never transfer observed labels automatically | PG 2025, paper 2025-09; source available, **AGPL-3.0**. Not a permissive drop-in; review distribution obligations before use [R35] | CUDA/NVIDIA; author example RTX 4090; numeric VRAM/RAM bounds U. Recompute correspondences/provenance after remeshing |
| CoACD; prop collider candidate | Triangle mesh → bounded convex hull collection. Preserve concavity/openings where possible; collision approximation separate from render mesh | SIGGRAPH 2022, MIT source; 2026-04 metric-threshold mode. Current V-HACD README declares end-of-life and recommends CoACD [R36, R37]. No weights | Official Linux x86-64, Windows amd64, macOS x86/ARM support. CPU baseline; time/memory U. Prefer static triangles for architecture; generation-derived collider never qualifies measured walking |
| Blender headless baking; production tool | High-detail and low UV mesh → tangent normals, isolated material passes and optional AO. Combined-light bake remains captured appearance. Cage/ray/margin validation needed | GPL Blender; output artwork is not automatically GPL. Review distributed worker/scripts/dependencies separately; no linking into viewer [R38, R39] | CPU bake baseline; platform/GPU tuple and exact supported version pinned later. Numeric memory/time U; process isolation with bounded job timeout |
| nvdiffrec; inverse-rendering research control | Posed multiview RGB/masks → optimized topology, BRDF/materials and lighting. Good explicit decomposition experiment; object/environment assumptions and geometry/lighting ambiguity limit cluttered interiors | CVPR 2022 code available; per-scene optimization needs no learned checkpoint. NVIDIA license §3.3 limits use to NC research/evaluation; dependency rights separate [R17, R40] | Windows/Linux NVIDIA CUDA; high-end recommended. Officially suggests reduced batch for mid-range GPUs, without a numeric minimum or W1 runtime guarantee |
| Mitsuba 3 + first-party material/light optimization; baseline **D** | Fixed observed/completed mesh + cameras/masks/linear RGB → shared UV materials and lighting estimate. Handles light transport; optimizer, regularizers, emitter model and glTF translation are our work, not turnkey video reconstruction | BSD-3 renderer, separate dependency audit; no required learned weights. Use original implementation of our objective rather than copying restricted nvdiffrec code [R41, R42] | CPU LLVM and NVIDIA CUDA differentiable variants documented; CPU fallback exists but interactive/full-room time U. Joint scene visibility/indirect illumination expensive; progressive texture/light resolution |
| TensoIR; inverse-rendering fallback experiment | Posed multiview views → tensor field, normals/albedo/roughness/lighting; mesh export supplied. Object-centered baseline, not full-room solved asset pipeline; baking/metallic BRDF translation and unsupported interiors need tests | CVPR 2023, MIT repository; linked checkpoints/results and separate dataset rights. Can optimize our scene without pretrained weights; transitive audit remains due [R43] | Tested Ubuntu 20.04/PyTorch 1.10.1; GPU workflow; relighting batch reduction officially documented. VRAM/RAM/time bounds U; old environment must be isolated |
| GS-IR; splat inverse-rendering comparator | Calibrated RGB → geometry initialization, occlusion baking, BRDF/illumination optimization. Captured SH cannot simply be renamed albedo; mesh/UV export adds another approximation | CVPR 2024; top-level MIT but install uses restricted original rasterizer/nvdiffrast and acknowledges nvdiffrec. Full graph not commercially cleared [R44] | CUDA, 30k initial training plus subsequent decomposition in examples; geometry/gamma/indirect-light settings matter. No proven minimum or full-room time |
| Material Anything; material prior challenger | Existing mesh (untextured/generated/scanned) → generated albedo/roughness/metallic/bump and refined UV texture. Plausibility prior, not source-illumination recovery; multiview constraints still required | CVPR 2025; code MIT; estimator/refiner released 2025-06, cards mark Apache-2 but have empty substantive README. Underlying diffusion/dependencies and weight terms need full audit [R45, R46] | PyTorch3D and Blender 3.2.2-oriented Linux instructions. VRAM/RAM/disk/runtime and supported offload U; do not infer MPS support from autogenerated model-card usage |
| IntrinsicAnything; image prior only | RGB → albedo/specular-shading proposals. Single-view release does not supply multiview inverse-rendering integration; per-frame intrinsic maps can disagree | Paper 2024-04; inference code 2024-05-31. Current code PRL v1.0 requires organizational project registration; checkpoint terms separately unresolved. Multiview release still marked forthcoming [R47, R48] | Python diffusion stack, batch/tiling options; numeric resource/minimum support U. Optional prior experiment only |
| glTF 2.0 + validator + Godot import; output baseline | Triangle/UV/material definitions + transforms → portable assets/assembled scene and validation. glTF core has no general external-scene referencing, physics or interoperable LOD policy; use a companion catalog and deterministic compilation | Khronos normative spec/validator; engine official import guidance. No weights; pin validator and one Godot stable version at pilot, preserve its license notices [R49–R51] | CPU export/validation; native engine import/render on chosen profile. Runtime/package budgets are our proposed gates; validators do not test visual/material/identity equivalence |

## How the components connect

The following are **D** adapter obligations to be proven in A0/A1, not claims that
the upstream repositories already interoperate.

| Boundary | Required conversion / evidence | Reject or limit |
|---|---|---|
| Video → calibration/depth/masks | One source frame ID; decoded color metadata; original-to-undistorted pixel transform. Rasterize masks in the exact depth/image grid; check round-trip landmarks | Cropped masks without inverse crop/intrinsics; mixed optical Z and ray range; unknown scale labeled metres |
| 2D masks → object inventory | Depth/track projection, occlusion tests and bounded association; map mask IDs to stable project instance IDs. Keep competing assignments | Mask propagation alone cannot establish complete 3D object inventory |
| Observation → generator | Select visible anchor(s), exact crop, alpha and camera metadata; preserve metric/relative extent. Generator's normalized mesh gets an explicit `world_from_asset` fit against all observed silhouettes/depth | No implicit alignment by bounding box alone; independent generations from every video frame create incompatible objects |
| GenRecon → asset decomposition | Feed canonical COLMAP-compatible cameras; record chunks, overlap ownership and generated support. Transfer instance constraints through 3D correspondence, rebuild shell/objects | A single scene GLB does not satisfy individually reusable assets or move/remove tests |
| Completed geometry → material estimation | Freeze a geometry revision; share one UV/material per asset, joint scene occlusion/light solve, robust fixed training exposure model; optimize only training appearance | Do not bake source shadows/reflections into base color or reuse evaluation images to correct colors |
| High mesh → runtime asset | Repair with displacement limits; freeze topology/UV/tangents; bake isolated channels, build error-bounded LOD and separate collision; carry face/texel provenance via correspondence | Unknown correspondence becomes inferred/generated, never observed merely by nearest neighbor |
| Catalog → glTF/engine | Compile scene nodes from exact asset revisions; hash-equivalence check individual and assembled meshes/materials. Companion manifest owns identity, provenance, collider/LOD mapping | glTF URI to another GLB is not core scene instancing. Engine import need not preserve custom extras; retain authoritative sidecar |

## Comparisons and unresolved experiments

Published numbers here are context, not a leaderboard. Different tasks, GPUs,
resolutions, metrics, prompts and occlusion levels make cross-paper rankings
unreliable. Neither view-synthesis scores nor material-map names certify asset
editability, background completion or relighting.

| Primary report | What was actually reported | Reproducibility limit / project use |
|---|---|---|
| SAM 2.1 model table [R03] | Tiny 91.2 FPS / large 39.5 FPS; SA-V J&F 76.5 / 79.5, A100, torch 2.5.1/CUDA 12.4 | Author benchmark script exists. Prompted segmentation, not automatic room inventory or end-to-end time |
| SAM 3.1 release [R06] | About 7× at 128 objects on H100 versus original SAM 3; SA-Co video results mixed | Official checkpoints/evaluation references available; no extrapolation to 8 GiB host or geometric association |
| TRELLIS.2 README [R14] | H100 generation approximately 3/17/60 s at 512³/1024³/1536³ | Author shape+material timing; excludes our detection, registration, repair, bake, scene/export and validation; consumer timing U |
| GenRecon paper/project [R19] | Author claims 16% improvement and demonstrates sparse posed-view scene completion | Exact task-specific reproduction needed; no independent replication or object-separation/changed-light pass established. Do not promote a percentage into product quality |
| TensoIR release [R43] | Evaluation data supplies held-out cameras, albedo/normals and several withheld illuminations; relighting scripts provided | Useful distinct material/light test; synthetic scenes cannot replace real video or measured room tests. Verify dataset/environment-map rights before use |

Run comparisons only after explicit model installation and rights screening:

1. **Observed surfaces:** direct MVS/TSDF versus gsplat 2DGS with the same original
   calibration/training frames/masks and separately reported per-method costs.
   Existing 3D splats are an appearance control, not a conversion shortcut. Apply
   Q5 only on measured observed surfaces; use asset-track gates separately.
2. **Identity:** SAM 2 automatic proposals/propagation versus fixed-category SAM
   3.1, with the same geometry association. Score masks, missed objects, split/merge
   errors, ID switches across occlusion/re-entry, runtime and memory. Hand clicks
   are a separately labeled assisted run. Grounding DINO is the fallback detector.
3. **Completion:** constrained geometry/local-library baseline versus SAM 3D
   Objects and TRELLIS.2 object proposals; GenRecon as a scene-level challenger.
   Use same visible input, object inventory and measured frame; three fixed seeds
   and selection rule based only on training constraints. Withhold backsides and
   empty-background inspection. Test every required object removal and move.
4. **Materials:** fixed geometry and shared UVs, Mitsuba fit versus TensoIR;
   nvdiffrec/GS-IR only where research usage rights allow. Compare optional
   Material Anything priors against a no-prior run. Fix training lights/exposure;
   no held-out color correction. Evaluate novel light directions/intensities,
   shadows/specular changes and albedo/roughness consistency separately.
5. **Repair/export:** baseline meshoptimizer/xatlas/bake against a cleared repair
   challenger; measure thin surfaces/openings, topology, UVs, bake error, LOD
   error, import stability and peak resources. A pleasant turntable is insufficient.

Owned fixtures are the default. Hypersim provides synthetic instance/camera and
intrinsic labels (ICCV 2021), but full geometry requires separately purchased
source assets; its image/data license does not grant every mesh right [R52].
OpenRooms is a relevant synthetic lighting/material comparator, with separate
source-asset/data dependencies [R53]. Neither dataset is downloaded here. Define
the selected subset/rights and frozen evaluation manifest before collecting
scores; use independently captured held-out real views for actual release claims.

Promote a challenger only for a measured gate improvement without a regression in
other required gates, within declared resources and cleared terms. A failed
generator leaves a valid partial observed scene; inability to complete or relight
is a rejected full-asset result. No currently reviewed evidence resolves generic
room inverse rendering, hidden authenticity, automatic animation topology or
commercial readiness of every generative dependency.

## Primary source register

Primary pages and controlling-file references below support the dated assessment;
unavailable pages are noted explicitly. Ranges in tables refer to these IDs. Exact
dependency/checkpoint byte identities will be frozen at A0, before installation.
No links below authorize contacting services or uploading media.

| ID | Primary source and date/availability evidence |
|---|---|
| R01 | [COLMAP FAQ](https://colmap.github.io/faq.html): calibration, dense inputs, masks and pose-change rebuild rules; ongoing docs, exact snapshot pin due |
| R02 | [Open3D fusion tutorial](https://www.open3d.org/docs/release/tutorial/pipelines/rgbd_integration.html): RGB-D/pose → TSDF; retrieved page identifies 0.19, unlike older plan's 0.20 announcement; not an installed-version change |
| R03 | [SAM 2 repository](https://github.com/facebookresearch/sam2), [license](https://github.com/facebookresearch/sam2/blob/main/LICENSE): dated checkpoint table/news and benchmark scope |
| R04 | [SAM 2 video predictor](https://github.com/facebookresearch/sam2/blob/main/sam2/sam2_video_predictor.py): frame/state offload flags; exact recipe/device qualification pending |
| R05 | [SAM 3 repository](https://github.com/facebookresearch/sam3): code, environment and access instructions; [paper](https://arxiv.org/abs/2511.16719) |
| R06 | [SAM 3.1 release notes](https://github.com/facebookresearch/sam3/blob/main/RELEASE_SAM3p1.md): 2026-03-27, H100 multi-object timing and mixed benchmark outcomes |
| R07 | [SAM 3 license](https://github.com/facebookresearch/sam3/blob/main/LICENSE): custom terms dated 2025-11-19, redistribution and use restrictions |
| R08 | [Grounding DINO repository](https://github.com/IDEA-Research/GroundingDINO): code/checkpoint links, CPU mode, Apache-2 license and ECCV 2024 citation |
| R09 | [VGGT repository](https://github.com/facebookresearch/vggt), [license](https://github.com/facebookresearch/vggt/blob/main/LICENSE.txt): commercial checkpoint distinction; inherited survey in plan §6.3 |
| R10 | [SceneScript repository](https://github.com/facebookresearch/scenescript), [license](https://github.com/facebookresearch/scenescript/blob/main/LICENSE): ECCV 2024, Aria clouds, Linux/CUDA, NC |
| R11 | [SAM 3D Objects repository](https://github.com/facebookresearch/sam-3d-objects), [model card](https://huggingface.co/facebook/sam-3d-objects): 2025-11-19 launch; [paper](https://arxiv.org/abs/2511.16624) submission 2025-11-20 |
| R12 | [SAM 3D setup](https://github.com/facebookresearch/sam-3d-objects/blob/main/doc/setup.md): ≥32 GB NVIDIA, Linux-64, access gate |
| R13 | [SAM 3D license](https://github.com/facebookresearch/sam-3d-objects/blob/main/LICENSE): custom SAM terms, code/weights |
| R14 | [TRELLIS.2 repository](https://github.com/microsoft/TRELLIS.2), [paper](https://arxiv.org/abs/2512.14692): 2025-12-16; official GPU/time/output conditions |
| R15 | [TRELLIS.2 model card](https://huggingface.co/microsoft/TRELLIS.2-4B), [license](https://github.com/microsoft/TRELLIS.2/blob/main/LICENSE): MIT code/model statement |
| R16 | [TRELLIS.2 setup](https://github.com/microsoft/TRELLIS.2/blob/main/setup.sh): actual v0.4.0 nvdiffrast and renderutils-fork installation |
| R17 | [nvdiffrec license](https://github.com/NVlabs/nvdiffrec/blob/main/LICENSE.txt), [installed-fork branch license](https://github.com/JeffreyXiang/nvdiffrec/blob/renderutils/LICENSE.txt): §3.3 NC research/evaluation |
| R18 | [nvdiffrast v0.4.0 license](https://github.com/NVlabs/nvdiffrast/blob/v0.4.0/LICENSE.txt): despite title “1-Way Commercial”, §3.3 limits non-NVIDIA use to NC research/evaluation |
| R19 | [GenRecon paper](https://arxiv.org/abs/2605.23888), [project](https://kasothaphie.github.io/GenRecon/): 2026-05-22, posed multiview/chunk completion |
| R20 | [GenRecon repository](https://github.com/kasothaphie/GenRecon), [license](https://github.com/kasothaphie/GenRecon/blob/main/LICENSE): code/checkpoint release 2026-06-29; checkpoint URLs present but host README unavailable to browser; complete weight terms/hash U |
| R21 | [3D-RE-GEN paper](https://arxiv.org/abs/2512.17459), [repository](https://github.com/cgtuebingen/3D-RE-GEN): 2025-12-19; MIT wrapper and separate model terms |
| R22 | [3D-RE-GEN installation](https://github.com/cgtuebingen/3D-RE-GEN/blob/main/INSTALLATION.md), [configuration](https://github.com/cgtuebingen/3D-RE-GEN/blob/main/src/config.yaml), [runner](https://github.com/cgtuebingen/3D-RE-GEN/blob/main/run.py): remote stage selection and material/baked-image defaults |
| R23 | [RecGen repository](https://github.com/TRI-ML/recgen): RGB-D/multiview API, checkpoints, evaluation and ECCV 2026 citation |
| R24 | [RecGen license](https://github.com/TRI-ML/recgen/blob/main/LICENSE): TRI NC and separate upstream components |
| R25 | [Hunyuan3D 2.1 repository](https://github.com/Tencent-Hunyuan/Hunyuan3D-2.1): 2025-06-14 model date, memory and platform claims |
| R26 | [Hunyuan3D 2.1 license](https://github.com/Tencent-Hunyuan/Hunyuan3D-2.1/blob/main/LICENSE): custom agreement/AUP; exact terms archived before use |
| R27 | [LaMa repository](https://github.com/advimman/lama), [license](https://github.com/advimman/lama/blob/main/LICENSE): WACV 2022; author-linked weights, old-link failures and Apache source |
| R28 | [SceneReGen paper](https://arxiv.org/abs/2608.23930): 2026-08-25; repository/checkpoint access not established |
| R29 | [PolyFit assumptions](https://github.com/LiangliangNan/PolyFit/blob/main/ReadMe.md): closed model/all necessary planes, solver licensing separate |
| R30 | [gsplat 2DGS trainer](https://github.com/nerfstudio-project/gsplat/blob/main/examples/simple_trainer_2dgs.py), [license](https://github.com/nerfstudio-project/gsplat/blob/main/LICENSE): available example, not this project's qualified recipe |
| R31 | [Original 2DGS](https://github.com/hbb1/2d-gaussian-splatting), [license](https://github.com/hbb1/2d-gaussian-splatting/blob/main/LICENSE.md): SIGGRAPH 2024 research implementation |
| R32 | [MILo repository](https://github.com/Anttwo/MILo), [license](https://github.com/Anttwo/MILo/blob/master/LICENSE.md): SIGGRAPH Asia 2025, source/extraction/config and upstream restrictions |
| R33 | [xatlas](https://github.com/jpcy/xatlas), [license](https://github.com/jpcy/xatlas/blob/master/LICENSE): UV library, MIT; exact current release date U |
| R34 | [meshoptimizer](https://github.com/zeux/meshoptimizer), [license](https://github.com/zeux/meshoptimizer/blob/master/LICENSE.md): simplification/optimization, MIT; exact current release date U |
| R35 | [PaMO](https://github.com/SarahWeiii/pamo), [license](https://github.com/SarahWeiii/pamo/blob/master/LICENSE): PG 2025, CUDA, AGPL-3.0 |
| R36 | [CoACD](https://github.com/SarahWeiii/CoACD), [license](https://github.com/SarahWeiii/CoACD/blob/main/LICENSE): MIT; SIGGRAPH 2022 and 2026-04 metric-mode notice |
| R37 | [V-HACD](https://github.com/kmammou/v-hacd): upstream end-of-life notice; not chosen for new baseline |
| R38 | [Blender baking manual](https://docs.blender.org/manual/en/4.4/render/cycles/baking.html): verified 4.4 documentation, UV/cage/tangent/margin behavior; latest/4.5 pages inaccessible during review |
| R39 | [Blender license guidance](https://www.blender.org/about/license/?wvideo=akzb57a5z3), [COPYING](https://github.com/blender/blender/blob/main/COPYING): GPL software versus output artwork |
| R40 | [nvdiffrec repository](https://github.com/NVlabs/nvdiffrec): CVPR 2022, sample datasets/configs, GPU/batch guidance |
| R41 | [Mitsuba 3](https://github.com/mitsuba-renderer/mitsuba3), [license](https://github.com/mitsuba-renderer/mitsuba3/blob/master/LICENSE): BSD-3 renderer, CPU LLVM/CUDA variants; 2022 foundation, current exact release date U |
| R42 | [Mitsuba optimization tutorial](https://mitsuba.readthedocs.io/en/stable/src/inverse_rendering/gradient_based_opt.html): differentiable parameters/optimization; no turnkey room claim |
| R43 | [TensoIR](https://github.com/Haian-Jin/TensoIR), [license](https://github.com/Haian-Jin/TensoIR/blob/main/LICENSE): CVPR 2023, public evaluation/checkpoint links and limited-memory relighting guidance |
| R44 | [GS-IR](https://github.com/lzhnb/GS-IR), [top-level license](https://github.com/lzhnb/GS-IR/blob/main/LICENSE): CVPR 2024, dependency/stage/evaluation source |
| R45 | [Material Anything](https://github.com/3DTopia/MaterialAnything), [license](https://github.com/3DTopia/MaterialAnything/blob/main/LICENSE): CVPR 2025, June 2025 model release |
| R46 | [Material estimator card](https://huggingface.co/xanderhuang/material_estimator), [refiner card](https://huggingface.co/xanderhuang/material_refiner): Apache-2 metadata, empty substantive cards; unresolved transitive/checkpoint scope |
| R47 | [IntrinsicAnything](https://github.com/zju3dv/IntrinsicAnything): single-view release 2024-05-31, multiview forthcoming |
| R48 | [IntrinsicAnything license](https://github.com/zju3dv/IntrinsicAnything/blob/master/LICENSE): current 2026 PRL registration requirement; not presumed permissive |
| R49 | [glTF 2.0 specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html): normative geometry/material/node/coordinate rules |
| R50 | [glTF Validator](https://github.com/KhronosGroup/glTF-Validator): local schema/binary checks; Apache-2 tool |
| R51 | [Godot 3D format guidance](https://docs.godotengine.org/en/stable/tutorials/assets_pipeline/importing_3d_scenes/available_formats.html): glTF/GLB import; chosen engine is a project evaluation decision |
| R52 | [Hypersim](https://github.com/apple-aiml-research/ml-hypersim): ICCV 2021 labels, CC-BY-SA-3.0 dataset and separately purchased mesh sources |
| R53 | [OpenRooms](https://github.com/ViLab-UCSD/OpenRooms): CVPR 2021 synthetic interior data/code release; component/data rights remain separate |
