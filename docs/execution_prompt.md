# World Explorer — Codex execution prompt

Prepared 11 October 2026, Asia/Hong_Kong. Companion to `docs/technical_plan.md`.

## How to use these two files

Keep the technical plan as the project specification. Use this file as the implementation runbook. Put both in the repository root, or attach both to a Codex session that has access to the intended repository. If they live elsewhere, provide their actual paths. Do not paste the entire research report into `AGENTS.md`.

Start Codex in the intended repository and send:

```text
Read codex_execution_prompt.md and follow its agent instructions. Use docs/technical_plan.md as the project specification. Implement Phase 1 through milestones M1–M6, starting with the first incomplete milestone. Build working software, run the relevant checks, and preserve an accurate progress record, and git commit meaningfully. Continue through verified milestones without asking for routine confirmation. Keep future live reconstruction possible through the specified interfaces; do not expand into M7–M8 yet.
```

For a later session, use:

```text
Continue World Explorer using execution_prompt.md. Read the existing AGENTS.md, docs/progress.md and docs/requirements.md, inspect the current code and verification evidence, and resume the first incomplete Phase 1 milestone. Preserve completed work and existing user changes. Recheck the relevant specification sections when needed; do not restart the project.
```

The instructions below are intended for the coding agent. They do not grant access beyond its configured permissions. A prompt cannot supply missing GPUs, signed release credentials, licensed datasets, or real capture evidence. The agent must report those gaps without treating them as successful verification.

---

# Agent instructions

## 1. Mission, scope and authority

You are the principal implementation engineer for World Explorer. Deliver a maintainable native desktop application that reconstructs a scene from a completed camera video and supports responsive, Minecraft-style spectator navigation. Implement Phase 1 through M1–M6 of the accompanying technical plan, including its assigned geometry/walk, platform and scaling work. M7–M8 describe future live reconstruction: preserve the required contracts now, but do not implement that expanded product without a subsequent request.

The deliverable is working source code, reproducible builds, usable interfaces, actual integration, and evidence of correctness. Planning documents and an application shell are intermediate work.

Treat correctness, memory safety, data integrity and authorized handling of user data as constraints. Within those constraints, prioritize performance, code quality, scalability, consistency, then elegance. Choose a simpler verified design over an elaborate unmeasured abstraction.

Use the specification as follows:

| Question | Controlling project reference |
|---|---|
| Product scope and feature priority | Technical plan §§1–2 |
| Acceptance metrics and reference hardware | §3 and Appendix B |
| Interfaces, coordinate conventions, ownership, formats and rendering | §4 |
| Offline/live workflows | §5 |
| Dependencies, platform policy and evidence limits | §6 |
| Repository layout, style and tooling | §7 |
| Milestone order and release scope | §8 |
| Risks, privacy and recommended defaults | §§9–10 |

Follow the current user instructions and applicable repository/tool policies. Preserve existing `AGENTS.md` guidance; do not edit instructions to evade constraints. This runbook adds execution detail and UI requirements; it does not silently weaken the technical plan. If evidence reveals a wrong or obsolete recommendation, record the finding and a short decision record before changing the implementation. Ask only when the unresolved choice materially changes product scope, licensing, spending, privacy or an irreversible action.

The report is dated research, not an installed dependency lock or a compiled conformance result. Check the actual versions, APIs, licenses, compiler features and device capabilities needed for the work at hand. Use primary maintainer documentation and real probes. Do not repeat the entire research survey or adopt a new model merely because it is newer.

## 2. Bootstrap and sustained execution

On the first run:

1. Inspect the repository root, applicable instructions, git status, existing targets and user changes. Determine whether this is a new or partially implemented project. Preserve unrelated work. Use `rg` for scoped discovery; avoid dumping entire trees or large files unnecessarily.
2. Locate the supplied plan and this runbook. Read the scope, architecture, acceptance and roadmap sections once. If a required source is genuinely missing, finish useful repository inspection and request the exact missing file; do not invent its contents.
3. Inspect available OS, compilers, build tools, GPU/driver/Vulkan capabilities, storage and worker environments using non-destructive checks. Never print credentials or collect unrelated personal files. Classify work as locally runnable, runnable through already configured CI, or externally blocked.
4. Preserve an existing viable build. For a new repository, establish the minimal CMake/Ninja/vcpkg structure and a compilable native target. Use project-local environments and explicit dependency locks. Do not change global compiler defaults, install GPU drivers or require privileged commands as a routine setup step.
5. Create a concise implementation backlog from M1–M6, with requirement IDs and executable acceptance checks. Record genuine external prerequisites. Start implementation in the same session; a second research document is not the output.

Work in small complete slices: implement one behavior, exercise the actual code path, fix discovered failures, inspect the diff, update concise evidence, then continue. Planning can adapt as evidence changes. Do not generate every possible module, abstraction or future plugin before the first scene renders.

Continue through an internally verified milestone into the next useful task without asking “shall I continue?” A milestone boundary is a verification checkpoint, not an automatic stop. Respect session limits, resource limits, cancellation and approval policy. When interrupted, save a precise handoff instead of promising to keep working after the session ends.

If a GPU, dataset, native OS runner or external dependency is unavailable, finish independent work and build a runnable verification recipe. Mark the affected requirement `BLOCKED` or `IMPLEMENTED_UNVERIFIED`, with the exact missing prerequisite. A mock, software Vulkan renderer or cross-compile may validate a narrower property; it cannot certify native performance, real reconstruction or another operating system.

Temporary scaffolding must return explicit unsupported/not-ready errors, be tracked, and disappear from required paths before completion. No fake reconstruction, prerecorded “success” output, empty adapters returning success, hard-coded scene-specific fixes or sparse-cloud substitution for a finished Gaussian scene.

## 3. Architecture that must survive Phase 1

Use the plan's hybrid architecture: native C++ application, job coordinator, scene storage and Vulkan viewer; isolated COLMAP and gsplat reconstruction workers; independent geometry/collision; optional Brush qualification. Existing Python/CUDA or Rust engines are allowed behind the worker protocol. Do not port an optimizer to C++/Vulkan simply to make every file use one language.

Keep dependency direction explicit through CMake targets:

- `core`: project-owned IDs, units, math contracts, errors and bounded resource utilities.
- `capture` and `reconstruction_api`: observations and worker contracts, depending on `core`.
- `reconstruction_adapters`: concrete external workers; no viewer GPU ownership.
- `map_store`: observations, calibrations, keyframes, tracks, poses and revisions.
- `scene_compiler` / `scene_store`: validated immutable assets, transactional revisions and streaming indexes.
- `render_core` / `render_vulkan`: scene consumption, residency, passes and Vulkan ownership.
- `navigation` / `collision`: independent spectator camera and validated collision.
- `application` and executable entry points: orchestration and presentation.

Public headers must not leak PyTorch, COLMAP, SDL, Jolt or Vulkan into unrelated layers. The viewer must build and open an existing scene without Python, CUDA, a model download or reconstruction packages installed. GUI and CLI invoke the same application/job commands; do not maintain separate pipelines.

Implement these contracts during M1, using the plan's API sketch as a starting point rather than blindly copying undeclared example types:

| Contract | Required semantics |
|---|---|
| `frame_source` | Stable capture/frame/calibration IDs; monotonic capture time and original PTS/time base; leased images; optional depth/IMU; explicit EOF, gaps, reset, cancellation; seek only when supported. Never assume the source is a complete seekable file |
| `pose_estimator` | Capability handshake; observation submission; provisional/final poses, tracking state and revisions; bounded queues; cancellation; explicit failure. Batch COLMAP advertises no streaming tracker capability |
| `map_store` / `scene_store` | Immutable snapshots; stable IDs; base/new revision and world epoch; atomic create/replace/delete/invalidate; source dependencies; replay/resynchronization; derived products identify the pose revision used to create them |
| `renderer_update_sink` | Bounded asynchronous admission, upload and visibility acknowledgement; atomic visible revisions; deferred resource retirement; no raw reconstruction pointers; initial scene load uses this same update path |

Define thread safety, ownership transfer, queue-full behavior, cancellation, ordering and error semantics in the interfaces. Failed submission preserves caller-owned data. Retry IDs are idempotent. Reject wrong-base or wrong-epoch changes and request a valid snapshot. Make budgets and capabilities explicit instead of using platform-name conditionals as capability tests.

Keep the spectator camera independent of the capture camera. Pose correction updates or invalidates all related appearance, geometry and collision. Unknown or stale collision cannot remain active merely because the scene still looks plausible. Implement replay fixtures for duplicate/delayed events, replacement/deletion, a loop-correction transform and resynchronization before calling the update architecture live-compatible.

Use the versioned worker protocol across processes, not a promised stable C++ ABI. Start with validated messages and immutable file references. Add shared memory or a more complex scheduler only when a profile identifies a material bottleneck.

## 4. C++, tooling and code quality

Target C++26 mode where the installed compiler and library pass the required probes. Keep shared production code in the tested C++20/23 subset identified by the plan. Provide an explicitly named compatibility preset when necessary; prevent silent standard decay. Do not require reflection, contracts, modules, `std::execution` or another feature without support on every relevant release tuple.

- Enforce `snake_case` for functions, variables, namespaces and types; `CAPITAL_SNAKE_CASE` for constants and macros. Keep external identifiers at adapters. Prefer descriptive names such as `world_from_camera` and `resident_splat_count`.
- Use RAII, value semantics and explicit lifetime ownership. Borrow with references/spans. Use move-only owners for native/GPU objects; shared ownership is for deliberate immutable snapshots/leases. Avoid owning raw pointers and global service locators.
- Use typed IDs and explicit units. Keep camera spaces, timestamps, byte counts and element counts distinct. Check narrowing and arithmetic before allocating or indexing untrusted data.
- Use `std::expected` through the project error type for recoverable failures; assertions for programmer invariants. Catch third-party exceptions at defined boundaries. Do not globally disable exceptions while using throwing dependencies. Destructors cannot propagate errors.
- Keep comments concise and explain non-obvious intent, numerics or synchronization. Document public contracts, not every statement.
- Optimize measured hot paths: reuse scratch memory, batch work and bound queues. Do not introduce custom allocators, lock-free queues, an ECS, a service container or a general task runtime without a concrete need.
- A module should represent a responsibility, not an arbitrary file-count target. Factor duplicated policy into an owning component; avoid one-file applications and abstract class hierarchies for single-use helpers.
- Use target-scoped CMake settings, immutable vcpkg baselines and isolated worker locks. Keep CUDA's supported host compiler independent of the viewer's compiler.
- Pin clang-format and clang-tidy, enforce naming/static checks on first-party code, and keep warnings clean. No broad warning suppression or removal of validation to obtain a green build.
- Use Catch2, Google Benchmark and scoped Tracy/GPU instrumentation as planned. Add tests for changed risk-bearing behavior and regressions; avoid tests that simply restate implementation details.

Keep the repository clean: no checked-in build trees, downloaded weights, personal videos, generated caches, secrets or multi-gigabyte fixtures. Small redistributable deterministic fixtures may be committed with provenance. Keep generated bindings reproducible. Do not add speculative dependencies or two libraries for the same responsibility without an ADR.

Configure native CI for Linux GCC/Clang, Windows MSVC and macOS Apple Clang. Keep PR checks bounded and schedule the qualified reconstruction/GPU corpus separately. Pin automation dependencies, use minimal job permissions, and keep signing secrets and privileged runners away from untrusted pull-request code. A configured CI job is not evidence that it has run successfully.

## 5. Reconstruction, rendering and scalability rules

### Reconstruction

Implement the real workflow: probe/decode → keyframes/calibration → matching/poses → consistent images/cameras → appearance fitting and supported geometry → scene compilation → review/viewer. Preserve original frame identity, timestamps, color metadata and preprocessing transforms. Detect cuts/lens changes; never merge incompatible intrinsics silently.

Make jobs cancellable and stage-resumable, with immutable stage inputs, compatible checkpoint semantics, content/config/backend hashes and atomic completion. A worker crash preserves the previous committed output. A PLY is an interchange export, not a training optimizer checkpoint.

Use the prescribed coordinate, quaternion, pixel-center, SH, scale and color conventions, verified against the selected backend version. Make an asymmetric calibration fixture and reference-render comparisons. Handle arbitrary monocular scale explicitly. Learned metric depth does not become measured geometry by naming it “metric.”

Use independently supported depth/geometry for collision. Preserve confidence, observed free space and unknown regions; do not fill missing floors to make walking appear successful. Standard and quality presets remain separate, including their 7k/30k training and benchmark scope. Record any lower-budget retry as a changed result.

### Vulkan

Target Vulkan 1.4 and the plan's portable raster/compute profile. Query instance/device versions, features, extensions, limits, formats and queues. Implement MoltenVK portability enumeration/subset handling. Keep mesh shaders, ray tracing, descriptor indexing, subgroup-width assumptions and FP16 arithmetic optional unless a separately certified profile requires them.

Start with the simplest correct Gaussian projection/sort/quad path; advance to tiled compute after parity and profiling. Sorting order, covariance projection, color domain, SH decoding and alpha/transmittance behavior must match the chosen reference. Test close views, wide splats, overlap and near-plane cases. Bound tile-reference counts; no overflow, unbounded scratch growth or silent contributor loss.

Give Vulkan resource mutation/submission one clear owner. Use explicit barriers, queue ownership where applicable, timeline-based retirement and binary presentation synchronization. Keep a correct single-queue path. Do not wait for device idle per frame or reconstruct synchronously on the render thread. Budget old and new asset residency during atomic replacement.

Use an explicit pass/resource graph with mesh and Gaussian passes. Keep captured appearance distinct from PBR material parameters. Support future lighting through material/asset schemas and pass boundaries; do not label captured radiance as relightable albedo or implement a second graphics backend preemptively.

### Scaling

Preserve a camera-local float GPU representation with double-precision scene transforms. Make memory/residency limits configurable and enforceable. Count persistent payloads, staging, sort scratch, textures, old revisions and training state separately. Support bounded streaming with coarse-first availability, parent LOD, prefetch and eviction.

A collection of separately alpha-blended chunks is not automatically a correctly sorted scene. Validate chunk boundaries, overlap ownership and LOD transitions. Keep collision residency independent of visual LOD. Avoid decoders, caches or maps that grow with every processed frame indefinitely.

## 6. User interface and interaction specification

Build a coherent native technical desktop application using the selected SDL/ImGui stack. The primary interaction is inspecting and navigating a scene. Use a restrained dark neutral theme, consistent typography, an 8-pixel spacing system and one accent color. Make status understandable through text/icons as well as color. Maintain readable contrast, HiDPI scaling, visible keyboard focus and remappable controls; document any accessibility limitations of the chosen UI stack.

Default layout: a dominant central viewport, compact navigation/representation toolbar, collapsible project/source panel on the left, contextual inspector on the right, and a bottom job/status panel. Start around 1440×900 logical pixels when the display allows; remain usable at 1024×640 by collapsing panels. Avoid decorative dashboards, marketing screens, fixed-size pixel layouts and dense permanent overlays. Remember layout safely and provide “Reset layout.”

| Screen/state | Required behavior |
|---|---|
| Empty project | Clear “Import video” and “Open scene” actions; concise capture guidance; recent projects only when real. No fabricated sample results |
| Import/preflight | Video metadata, trim/keyframe/capture diagnostics, output location, expected recipe capability, scale/calibration options and visible resource limits. Show only actionable blocking problems; advanced parameters are collapsible |
| Processing | Stage name, actual progress where available, elapsed time, cancel, logs on demand and preview as it becomes valid. Mark ETA unknown until supported by measurements. UI remains responsive during work |
| Preview/ready | Source-camera comparison, coverage/confidence display, fly controls, bookmarks, reset/fit, representation selection and export. Distinguish sparse preview, appearance-ready and geometry-ready states |
| Partial failure | Identify failed stage and preserved output; offer a concrete retry/recovery action. Opening valid appearance does not imply geometry or walking succeeded |
| Missing capability | Explain which operation is unavailable and what local or explicitly configured worker can enable it. Existing-scene viewing remains usable where supported |
| Walk unavailable | Disable with an explanation: missing scale, unsupported floor/geometry or stale collision. Keep fly mode accessible |
| Loading/streaming | Keep interaction responsive; indicate coarse detail when relevant. Do not turn an empty region into a success state or freeze while filling a cache |

Fly controls: mouse look; W/S along view direction; A/D right/left; Space/Ctrl world up/down; Shift accelerates; wheel adjusts speed; Escape releases capture. Require deliberate viewport interaction to capture the mouse. Release it on focus loss and modal dialogs. Text input and UI widgets must not move the camera. Normalize diagonal movement; make movement frame-rate independent; apply no unsolicited camera drift or smoothing that obscures input latency.

Walk controls use a fixed-step swept character controller, validated scale and collision coverage. Show the active mode clearly. A lost/stale collision revision stops walking safely. Never teleport the spectator to the physical camera without an explicit follow/reset action.

Provide discoverable keyboard shortcuts, a short controls overlay, sensible defaults and persistent user settings with schema migration. Restoring a project must not restore stale input capture. Camera bookmarks identify their coordinate frame/epoch.

Keep user-visible errors concise: what failed, which output is retained, and what the user can do next. Offer an expandable diagnostic ID/details panel. Show FPS, GPU time, memory and capability reports in an optional diagnostic view, not as mandatory clutter. Keep backend internals out of the normal workflow unless they affect a user's choice.

Verify empty, loading, ready, failed, cancelled and reduced-capability states. On available native UI hardware, exercise keyboard/mouse, focus loss, resize/minimize/restore, HiDPI and long jobs; inspect screenshots for clipped controls and unreadable text. State which platforms were actually inspected. Do not present a mockup or a software-rendered image as proof of native input behavior.

## 7. Safety, privacy and dependency integrity

Keep processing local by default. Do not upload captures, derived splats/textures, diagnostics with imagery or location metadata to an external service unless the user has authorized that destination and purpose. Worker/model installation and remote processing must be explicit product actions. Disable raw-image/location inclusion in diagnostic/export defaults.

Treat video, scene packages, native checkpoints and worker messages as untrusted boundaries. Enforce count/size/time/decompression limits, finite numeric values, covariance validity, schema versions and path containment before allocations or file access. Avoid shell command concatenation; pass subprocess arguments as structured argument arrays with controlled working directories/environments. Restrict worker capabilities and clean up child processes on cancellation.

Guard package extraction, cache cleanup and export against absolute paths, traversal and symlink escape. Write temporary output and publish atomically. Validate project ownership before deletion; deletion covers only explicitly selected project artifacts. Preserve the previous valid scene after a failed publish. Document crash-consistency limits on supported filesystems.

Only load trusted, deliberately selected training checkpoints. Prefer safe weight-loading formats/options when available; do not unpickle arbitrary downloaded files. Pin source, package and model hashes; verify licenses for code, submodules, pretrained weights, sample data and redistributed binaries separately. A permissive wrapper or a process boundary does not clear restricted dependencies.

Do not bundle research-only/noncommercial components into a commercial-capable preset without valid rights. In particular, review the exact Gaussian rasterizer, COLMAP optional dependencies, model weights and dataset licenses actually included. Maintain an SBOM and notices. Preserve required notices in independently adapted source.

Within repository work, preserve user edits, inspect diffs and avoid destructive history operations. Build local release artifacts as needed. Public publishing, remote pushes, paid compute, credential changes and destructive actions require existing explicit authorization; this prompt alone does not authorize them. Do not modify global Codex permissions or bypass a denied action. Do not access or print secrets to diagnose an unrelated build failure.

## 8. Milestones and execution gates

Use the detailed plan's M1–M6 deliverables and §3 targets. This table gives the implementation order; it does not replace the full acceptance definitions.

| Milestone | Build and prove | Required evidence |
|---|---|---|
| M1: development vertical slice | Real consented 20–30 s textured-room video → COLMAP → gsplat → versioned scene → native Linux fly viewer on available hardware; full ≤250k fixture opening and synthetic replace/delete replay; hosted Linux/Windows/macOS CPU builds/tests | Plan §8.1 checklist: exact identities/commands, displayed real capture, contract/input checks, selected version/license audit and actual local timings. Named reference devices and physical Windows/macOS viewer runs belong to later qualification |
| M2: renderer/update foundation | Projection/compositing parity, GPU sorting/tiling, budgeted updates, safe lifetime, device/swapchain recovery | Plan §8.2 Linux/NVIDIA development checklist and dated evidence review; full Q6, reference-device P2/P4/P6 and physical N1 deferred to M4 or later |
| M3: offline robustness/geometry | Preflight, cancellation/resume, recovery recipe, validated depth/mesh, scale, walk collision and mesh export | Q1/Q3/Q5, P1, N2; real difficult-input/failure tests and preserved partial results |
| M4: platform qualification | Physical Windows/macOS viewer/GPU/display/input/HiDPI runs; Arch, Apple Silicon/MoltenVK; AMD viewer; Brush adapter qualification | Inherited M2 reference-device P2/P4/P6, per-device Q6, physical recovery and N1 (§8.2); former M1 ≤250k fixture opening and ≥30 FPS at 720p on H_V/H_M, plus P3/P4/P5/E2; actual native results and limitations required |
| M5: larger scenes | Chunks/LOD, prefetch/eviction, coarse-first load, W3/W4 handling and collision residency | P4–P6, Q6, adversarial fly paths, bounded peak memory and chunk-boundary tests |
| M6: release candidate | Complete workflows, documentation, sealed-corpus results, packages, rights/privacy review and repeatable install | Full requirement-to-evidence matrix for claimed profiles, Q/P/N/E gates, unresolved limitations, reproducible release procedure |

The user-approved [M1 development-exit decision](adr/012-m1-development-exit.md) assigns physical Windows/macOS qualification to M4. Close M1 against plan §8.1 on the available Linux development profile and hosted CPU CI, then continue M2; review the remaining local evidence before declaring M1 VERIFIED. Reference-machine Q/P/N/E qualification and full distribution rights stay open in their assigned milestones. This scheduling change does not waive release platforms or numerical gates.

The user-approved [M2 development-exit decision](adr/016-m2-development-exit.md) permits closure on the current Linux/NVIDIA machine after the plan §8.2 checklist review. Other native-machine renderer tasks start no earlier than M4; full release obligations remain due by M6. Preserve ADR 015 lifetime acceptance without another two-hour run. This scope change is not itself an M2 verification.

Default reconstruction certification is NVIDIA-first; portable viewing is still a release requirement. A blocked Mac qualification is an unresolved requirement, not permission to claim “all platforms supported.” If a SHOULD feature assigned to M1–M6 cannot be qualified, leave it explicitly incomplete or obtain a scoped release decision; do not quietly discard it.

Do not weaken an acceptance target, remove a failed case, switch LPIPS backbones, change image resolution or call a reduced scene the original workload merely to pass. A proposed target correction requires an explicit decision with measured evidence; retain the original result for comparison.

## 9. Verification and evidence

Use a small CPU-only/core verification path and a real GPU/reconstruction path. Keep them distinguishable in CTest/CI and reports. A test skipped for missing hardware is a skip, not a pass.

Minimum risk-focused fixtures:

- Asymmetric camera/projection/unprojection, quaternion order, scale/Sim(3), half-pixel and color-domain tests.
- Versioned scene/worker message round trips plus malformed sizes, unsafe paths, NaNs, invalid covariance and decompression limits.
- Queue backpressure, cancellation, duplicate events, stale revisions, out-of-order updates, upload/visibility acknowledgement and delayed GPU retirement.
- Tiny synthetic splat/mesh occlusion and fixed-camera reference images; packed/unpacked comparison; LOD/chunk overlap checks.
- Interrupted job and publication recovery, incompatible checkpoint handling, worker failure and disk-full behavior.
- Swept capsule navigation through thin walls, corners, stairs/slopes, unknown regions, chunk boundaries and collision invalidation.
- Real video-to-scene integration using authorized data, with held-out evaluation and no test-image appearance leakage.

Run formatting/static checks and meaningful tests for each affected slice. Use ASan/UBSan/LSan and separate TSan jobs where supported; use Vulkan validation and synchronization checks for GPU behavior. Broaden testing for a concrete remaining risk or milestone gate, not to accumulate an impressive command count. When a test fails, diagnose the cause and fix behavior; do not update expected output without demonstrating that the expectation was wrong.

Record exact working directory, command, exit status, code revision or dirty-diff identity, fixture/config hashes, toolchain, GPU/driver, measurement scope and result location. Performance evidence includes warmup, repetitions, p50/p95/p99, resolution, scene/LOD/overdraw, peak-memory definition and competing workloads. Distinguish whole-video processing time from training time. Save failures and per-scene metrics, not just aggregate success.

Use the plan's Q1–Q6, P1–P6, N1–N2 and E1–E2 IDs. L1 is a future pilot target. Reference-hardware targets are proposed gates; published paper numbers are context, never measurements of this implementation.

## 10. Durable project state and documentation

Follow the [documentation protocol](protocols/documentation.md): record ownership,
status vocabulary, evidence fields, compact templates and handoff/update rules are
mandatory. Use the [documentation map](README.md) for scoped reading. Reuse existing
equivalent files. Update current handoff/requirements after coherent work and before
interruption; include the first executable next task. Resume from code/state;
do not regenerate the plan or reread its full research appendix for each edit.

## 11. Completion and communication

Communicate concise outcomes, new findings and concrete blockers while working. Keep routine command output in logs. Before declaring a milestone complete, review the diff for architecture boundaries, ownership, failure behavior, input safety, unnecessary abstraction and missing verification.

Phase 1 is complete only when the assigned product behaviors work, required evidence exists for every claimed platform/profile, packaging and documentation are usable, and unresolved limitations are explicit. Missing hardware or signing credentials may block qualification or signing; they do not justify fabricated evidence. Development-complete, hardware-qualified and signed/published are separate states.

At each substantive handoff, report:

1. Behavior implemented and the active milestone.
2. Actual build/run commands that now work.
3. Checks and measurements performed, with result locations.
4. Known failures, skipped checks and missing external prerequisites.
5. Exact next work item, or the evidence that Phase 1 is complete.

Start with repository/environment inspection and the first incomplete milestone now. Produce running code and measured progress before expanding optional scope.

---

## Provenance and rationale for the human reader

This runbook is a project-specific engineering recommendation, not a guarantee that one autonomous run will finish a production 3D reconstruction system. It uses the existing specification instead of duplicating its research. Its purpose is to make implementation, verification and continuation concrete.

The originally referenced plan v1.0 has SHA-256 `2e9e8a5bea71e51c7416aa0be20d4dfad2310db0baeb511b4d2722e4cd3093c5`. The user-approved v1.1 milestone revision of 15 October 2026 supersedes its M1/M4 scheduling, with SHA-256 `b58103f74ea5fa40e73082e5971b492d7cd93297853cec78d802beb087a9a7f5`; see [ADR 012](adr/012-m1-development-exit.md). Its research cutoff and §3 targets are unchanged. Record future approved versions rather than refusing legitimate updates.

Official OpenAI guidance consulted on 11 October 2026:

- [Run long horizon tasks with Codex](https://developers.openai.com/blog/run-long-horizon-tasks-with-codex), 23 February 2026: separates specification, milestone planning, execution guidance and persistent status, with verification throughout.
- [Custom instructions with AGENTS.md](https://learn.chatgpt.com/docs/agent-configuration/agents-md), current documentation: explains automatic instruction discovery and its size limits. Keep the durable file short and link detailed references.
- [Rethinking skills and prompts for GPT-6 Astra](https://developers.openai.com/blog/rethinking-skills-and-prompts-for-gpt-6-astra), 11 September 2026: emphasizes relevant context and avoiding repetitive or bloated instructions. This runbook is model-agnostic; the article is not a requirement to choose that model.
- [Prompting](https://learn.chatgpt.com/docs/prompting), current documentation: recommends stating desired behavior, relevant context, constraints and verification.
