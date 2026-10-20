# Owned mesh postprocessing

M3's `MESH_POSTPROCESS_PIPELINE_V1` runs explicit cleanup, captured-color baking
and GLB export over an immutable native scene. GUI and CLI use the same
`run_geometry_job` application command. Retained/fresh solver request profiles
remain compatible; this profile launches no solver, model download or network
fallback. It does not complete geometry, infer materials, align scale, create
collision or enable walking. [Local evidence](../validation/2026-10-20-mesh-job.md).

```sh
build/dev/we_process run-geometry REQUEST.json SHA256 NEW_JOB
build/dev/we_viewer --scene PRIOR.wexplore \
  --geometry-request REQUEST.json --geometry-request-sha SHA256 \
  --geometry-output NEW_JOB --frames 90
```

The returned path is the final native bundle. The viewer admits it through its
ordinary asynchronous scene-update path; failed processing retains the prior
scene. GLB is a separate product under the export stage, not the viewer handoff.
Existing startup geometry controls expose this profile; a new interactive stage
configuration editor is not implemented. Linux development is validated; other
native platforms remain unqualified. CPU-only dispatch is separate from the
Linux-only retained/fresh solver implementation. Missing unsupported-platform
solver requests still return `BACKEND_FAILURE` without opening caller input.

## Request and dependencies

An externally pinned request has exactly these eight fields; paths resolve
relative to its containing directory:

```json
{
  "schema_version": 1,
  "kind": "MESH_POSTPROCESS_PIPELINE_V1",
  "source_bundle": "/absolute/or/relative/source.wexplore",
  "source_manifest_sha256": "LOWERCASE_SHA256",
  "cleanup_request": null,
  "bake_request": null,
  "reuse_baked_bundle": {
    "path": "/absolute/or/relative/original-bake.wexplore",
    "sha256": "LOWERCASE_MANIFEST_SHA256"
  },
  "export_glb": true
}
```

Each nullable reference is either null or exactly `{ "path", "sha256" }`.
`cleanup_request` selects an original eight-field
[fusion scene request](scene.md#observed-mesh-runtime), admitted against its
explicit canonical poses and original asset. Its geometry, IDs, revision and
source documents must match the source bundle. The fixed
[certified cleanup policy](mesh_cleanup.md) produces a separate certificate and
scene request; it never edits raw geometry.

`bake_request` selects the existing five-field
[color request](mesh_color.md#bounded-native-baked-appearance), including its
explicit legacy/sequence profile and cache pin. Its original input geometry must
match the source. The job creates an absolute derived request pointing at the
cleanup result when selected, and runs the existing baker. Every cache and source
dependency receives ordinary admission. Fixed numerical policies belong to these
versioned contracts; changing stage semantics requires a new job profile.

`reuse_baked_bundle` selects an externally pinned, validated original native bake
instead of running the baker. Its complete untextured geometry/dependencies must
match the selected raw or cleaned scene. If a bake request is also supplied, its
inputs are still admitted. This is explicit reuse of a selected artifact, not
photometric re-evaluation. The bundle retains source pose/cache hashes and exact
authoring documents. Original baked sources can be copied/exported directly
with all three references null. Partially baked scenes and implicit replacement
of a source bake reject. Lossy imported previews cannot be source/reuse bakes.

Sources must be nonempty, current, mesh-only and arbitrary-scale. GLB export
requires complete original appearance and follows [GLB V1/V2](gltf.md), including
all candidate evidence and declared quantization/filtering losses. No chunks are
dropped. Paths/digests, duplicate keys, raw NUL, schema types and all input/output
overlaps are checked. Request/receipt JSON is bounded to 1 MiB and nesting to 8;
owned publication metadata retains the existing 64-KiB cap. Canonical paths reject
symlinks. Inputs are re-admitted before terminal publication. Frozen native-bundle
selection is not a claim about an unrelated current map revision.

## Ownership, stages and failure

`owner.json` pins request identity and source scene content under the existing
lifetime writer lock. Changed selection/configuration requires a fresh directory.
The destination must be outside all selected inputs, underlying asset/pose paths
and admitted cache roots. Root inventory permits only ownership/lock, `stages`,
selected completion receipts and the terminal `manifest.json`.

Stages run in order: optional `cleanup`, mandatory `native`, optional `bake`,
optional `export`. Each reserves a new `stages/NAME-N` directory, with at most 128
attempts per stage; interrupted attempts are retained. Actual products live under
its `output` directory. A bake may additionally retain its generated `color.json`.
Inventories reject links/nonregular files, unknown directories and invalid attempt
names. Each attempt permits at most 40 entries, depth 2 and 3 GiB of file bytes;
the ordinary scene/GLB budgets apply independently. These are bounded development
limits, not performance or larger-scene qualification.

A stage receipt records its attempt, dependency hash and exact relative
file/byte/SHA-256 inventory. The dependency chains the owner and preceding receipt.
Replay verifies strict metadata types, every file and the stage's semantic scene
or export binding. Completion never repairs changed/missing files. A terminal
job missing a selected stage receipt refuses before starting another attempt.
Checksums provide integrity, not artifact authorship or geometry-quality proof.

Cancellation before ownership creates no job. A stopped/failed attempt remains;
resume selects another directory and reuses only verified completed stages.
After a producer commits its output, its completion receipt finishes as one
boundary even if cancellation arrives late. The terminal manifest is published
only after all selected stages and dependency rechecks, and pins the final bundle,
scene and receipts. Late terminal publication is likewise a commit boundary.
A failed ownership/receipt publication may retain pending metadata requiring a
fresh directory; nothing is deleted automatically. Power-loss durability, process
kill/crash injection and distributed/multi-user filesystem races are unqualified.
