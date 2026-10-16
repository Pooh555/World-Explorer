# World Explorer contributor instructions

## Scope and state
- Read `docs/progress.md` and `docs/requirements.md` before resuming.
- `docs/technical_plan.md` is the specification; `docs/execution_prompt.md` is the runbook.
- Follow M1 → M2 → M3 → A0–A5 → M4 → M5 → M6, then A6 engine packaging (plan §§8/11, ADR 019). M1–M6 keep their relative order; M7–M8 remain deferred by the user.
- Validate the expanded asset goal locally before M4, with the first real visible result in A1. A0–A6 remain NOT_STARTED. Respect documentation-only task limits.
- A partial implementation or synthetic scene never completes a real reconstruction gate.
- Preserve existing user edits. In particular, the execution prompt had an uncommitted user edit at bootstrap.
- Local commits are authorized. Remote pushes and publishing are not.
- The user authorized reconstruction of root `sample.mp4`. Never commit or upload it.
- Generated captures, jobs, screenshots and build products belong in ignored `artifacts/` or `build/`.

## Build and check
- CMake/Ninja; C++26 target, C++23 shared subset. Never silently lower the standard.
- `cmake --preset dev` configures the system-library development profile.
- `cmake --build --preset dev -j 4` builds the native viewer, CLI and core tests.
- `ctest --preset dev` runs CPU contract and worker tests.
- `cpu` omits SDL/Vulkan; `compat-cxx23` explicitly selects compatibility mode.
- `clang`, `asan`, `tsan` are independent presets; TSan must not combine with ASan.
- `release-vcpkg` uses the immutable dependency baseline; qualification remains separate.
- Development archives are SHA-256 pinned in `cmake/dependencies.cmake`.
- Format first-party C++ with clang-format 23.1.1 and `.clang-format`.
- Static analysis uses clang-tidy 23.1.1; do not weaken warnings to pass.
- GPU execution requires native device access; sandbox driver-load failure is not GPU absence.
- Record actual commands/results in `docs/validation/`, not inferred passes.
- Follow `docs/protocols/documentation.md`; use `docs/README.md` for scoped reading.
- Check documentation with `python3 tools/check_docs.py` and `git diff --check`.

## Architecture
- `core` owns typed IDs, math, errors and byte/count limits.
- `capture` and `reconstruction_api` expose project-owned contracts.
- Concrete solvers run in isolated workers, never linked into the viewer.
- `map_store` owns observation/calibration/pose snapshots and revisions.
- `scene_store` validates and publishes immutable assets and transactional revisions.
- `render_core` consumes immutable scene updates; only `render_vulkan` owns Vulkan.
- `navigation` is independent of capture-camera poses and reconstruction.
- `application` exposes commands shared by GUI and CLI.
- Do not expose CUDA, PyTorch, Vulkan or SDL through unrelated public headers.
- Failed admission preserves caller data; wrong base/epoch requires resync.
- Retry operation IDs must be idempotent; collision is invalidated by pose corrections.
- Initial scene load uses the same update path as replacement/deletion.

## Correctness and data handling
- Use RAII, `std::expected`, bounded queues and explicit cancellation.
- Snake case for project types/functions/variables; uppercase enum values and macros.
- Validate sizes, arithmetic, paths, schemas, checksums and finite values before use.
- Avoid shell command construction. Child processes use argument arrays.
- Keep processing local; no implicit model downloads or remote fallback.
- Write immutable assets first, then atomically publish the manifest.
- Keep arbitrary monocular scale explicit. Asset-track completion is labeled separately; generated floors/collision never satisfy measured Q5/N2 or enable measured walking.
- World is right-handed, +Y up; optical cameras +X right/+Y down/+Z forward.
- Store world-from-camera, wxyz quaternions and integer pixel centers.
- Preserve backend color/SH conventions; captured radiance is not relightable albedo.
- Synthetic diagnostics must be labeled as such in UI and evidence.

## Evidence and handoff
- Statuses: NOT_STARTED, IN_PROGRESS, IMPLEMENTED_UNVERIFIED, VERIFIED, BLOCKED, DEFERRED_BY_USER.
- Verify only the hardware/profile actually tested; native CI configuration is not a native pass.
- Do not weaken Q/P/N/E thresholds or change benchmark scope to obtain a pass.
- Maintain a precise next executable step in `docs/progress.md`.
- Keep local data, dependency source caches, credentials and large traces out of Git.
- Commit coherent implementation slices with relevant test evidence.
