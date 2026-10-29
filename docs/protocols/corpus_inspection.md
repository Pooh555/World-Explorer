# Read-only asset-corpus inspection

[`inspect_asset_corpus_request`](../../include/world_explorer/application/corpus_inspection.hpp)
is the shared application command behind:

```sh
build/dev/we_process inspect-corpus REQUEST.json REQUEST_SHA256
```

It loads independently selected original receipts, capture/annotation and typed
evidence selections, then reopens the selected committed corpus through the
[existing admission path](evaluation_corpus.md). The command prints the declared
SYNTHETIC/PILOT/RELEASE track, corpus identity/counts/hash and every derived scoped
blocker. Exit 0 means metadata inspection succeeded, including when blockers are
present; exit 2 means usage/admission failed. It supplies no execution/ready/AP
token. No worker, model download, source-video decode, publication or output file
is invoked. No GUI panel is added; native callers can invoke the same function.

## Request v1 and independent authority

The entire request is externally SHA-256 pinned. Exact root keys are
`schema_version=1`, `corpus`, `captures`, `recipes`. All paths are caller-selected,
absolute or relative to the request's directory. They are never loaded from the
inspected corpus. A path is nonempty, at most 4,096 bytes and has no embedded NUL.
The caller is responsible for independently selecting the request and its pins;
hashing a self-discovered manifest does not establish that authority.

`corpus` has exactly `directory`, `commit`, `selected`. `commit` is
`{generation,manifest_sha256}` with positive generation. `selected` is
`{corpus,revision,sha256}`. Each capture row has exactly:

```text
label, receipt, context, registry, commit, selection, annotations,
measurement, inventory, paths, references, envelope, protocol
```

`receipt` is `{directory,sha256}` for the original asset receipt; `context` is
`{world_epoch,pose_revision}`. The independent capture `commit` must match the
current `registry`, and must also equal the capture commit in the admitted corpus.
This second comparison prevents a later registry read from substituting the
corpus's pin for the request's pin. Existing cooperative-filesystem limits apply;
there is no cross-directory transaction or hostile-filesystem guarantee.

`selection` has `product`, `selection_id`, `revision`, `sources`, `frames`,
`measurement`. Sources are `{capture_id,source_sha256,rights}`. Frames are
`{view,role,annotations}`. Roles are INFERENCE/HELD_OUT/EVALUATION_ONLY; annotations
are product refs. Measurement is a ref or null. Refs use exactly
`{product_id,revision,content_sha256}`; views use
`{capture_id,frame_id,calibration_id,frame_pose_revision,image,pose_asset}`.
These full independent selections are compared against owned record/pose bytes;
the loader never fills them from the corpus or capture record.

`annotations` contains independent `ObjectObservationSelection` rows:
`{product,observation_id,revision,view,mask,grid,evidence}`. Grid keys are
`width,height,image_to_mask` (four finite values); evidence is a ref array. Native
[observation admission](object_observations.md) runs before capture admission.

Recipe rows are exactly `{label,receipt,context}`. The original corpus independently
pins the actual recipe product/hash/host profile, which existing corpus admission
rechecks against these owned receipts. No installed/review/runtime pin is invented.

## Typed evidence selections

Optional fields are explicit null when absent; `references` is always an array.
Non-null fields go through the existing typed inspectors against the selected
original capture/receipt. Existing corpus version flags and evidence refs decide
which handles are required; omitted handles never fall back to serialized pins.
Earlier corpus schemas retain their existing behavior and encodings.

| Field | Exact selection shape |
|---|---|
| `measurement` | `{selection,bundle,manifest_sha256}`. Selection is `{product,view,scene,alignment}`; scene is `{scene_id,world_epoch,revision,sha256}`. The native bundle manifest is independently pinned, then capture/scene/anchor admission repeats |
| `inventory` | `{inventory,visibility,items,cells}`. Items: `{track_id,kind,label,required}`. Cells: `{track_id,capture_id,frame_id,state,annotation}`; annotation is ref/null |
| `paths` | `{temporal,inspection,blocks,path}`. Blocks: `{id,capture_id,role,frames}` (frame IDs). Path entries: `{capture_id,frame_id,confidence}` |
| `references` | Up to three `{product,kind,pairs}` selections. Pairs: `{id,subject,baseline,reference,baseline_condition,reference_condition,reference_state,baseline_lighting,reference_lighting}`. Lighting is ref/null; inventory is required |
| `envelope` | `{product,sources,views}`. Sources: `{source,source_frame_count,condition,scene_motion,exposure_control}`; source has the capture-source shape above, frame count is integer/null. Views: `{view,role}` |
| `protocol` | `{product,artifacts,masks,lights,geometry_samples,geometry_seed,uv_resolution,reviewers_min}`. Artifact keys and mask/light rows retain the [frozen protocol](evaluation_protocol.md) selection shapes |

Enums and semantic constraints remain owned by the
[measurement](capture_measurements.md), [inventory](evaluation_inventory.md),
[path](evaluation_paths.md), [controlled reference](controlled_references.md),
[envelope](capture_envelope.md) and [protocol](evaluation_protocol.md) contracts.
All booleans are strict; integers reject negative/floating/bool values and uint32
sampling fields reject overflow. Unknown fields/enums and stale/foreign refs fail.

## Bounds, preservation and missing inputs

Requests are at most 2 MiB, depth 16, 16 captures/16 recipes and 256 selected frames.
Per-capture tables retain native caps: 32 sources/64 frames/128 annotation pins
(16/frame), 128 supplied annotation selections (64 evidence refs each), inventory
64 items/4,096 cells, paths 64 blocks/64 total frames/64 path entries, 3 reference
selections (64-pair parsing budget, native admission ≤32 each), envelopes 32
sources/64 views and protocols 32 masks/8 lights.
Request v1 retains 64 MiB per original receipt and 256 MiB across all receipt
blob snapshots, counting repeated selections. The loader now supplies the
remaining allowance to native receipt admission, which checks the complete exact
inventory before any payload read. The next receipt cannot transiently exceed
that allowance. Counting uses admitted payload sizes without copying blobs.
Native typed handles have their own additional bounded allocations.

Measurement bundles use at most 64 chunks, 32 MiB scene payload, 262,144 vertices,
524,288 faces and no splats/textures. Existing bundle limits also apply. Bundle I/O
has no stop-token API; cancellation is checked before/after it and within receipt,
registry and typed admission. CLI SIGINT/SIGTERM use the shared cancellation path.

Raw NUL, duplicate keys, excess nesting/bytes, invalid hashes and nonfinite values
fail. Input/registry/seal files are only read; cancellation/errors never publish or
repair them. Admitted results own their snapshots under existing corpus lifetime
rules. A missing corpus pin or empty capture/recipe table refuses before resolving
input paths, naming independent corpus/seal, capture/commit and recipe prerequisites.
Existing reconstruction bundles are not accepted as substitutes. Physical capture,
rights, metric execution, runtime/resource closure and all AP gates stay open.


## Explicit request v2

The same shared API and CLI accept schema 2 with exact root keys
`schema_version`, `receipt_budget_bytes`, `corpus`, `captures`, `recipes`.
`receipt_budget_bytes` is required and strictly integral, from **1 byte to 1 GiB**;
it covers all capture **and** recipe receipt payload allocations, counting every
repeated selection. Version 1 keeps its original fields/256 MiB ceiling and
rejects the new field. No request changes a per-receipt or typed profile ceiling.

Each v2 receipt selection (including recipe receipts) adds required
`schema_version=1|2` to `directory,sha256`; it independently chooses SMALL_V1 or
CAPTURE_V2 and must match the actual pinned receipt. Each capture `selection` and
each nonnull `protocol` likewise adds a required `schema_version=1|2` matching
its pinned native bytes. Request 2 may select native v1 inputs; it never infers
an upgrade from request version, receipt size or inspected corpus bytes.
The native v2 [capture](capture_selections.md#explicit-native-image-profile-v2) /
[evaluation](evaluation_protocol.md#explicit-full-native-grid-accounting-v2)
prerequisites and unchanged image/grid/role/metric limits still apply. All other
keys, tables, commits, pins, null semantics and canonical corpus bytes are unchanged.

`AssetReceiptSelection::m_payload_budget` is a caller allowance independent of
manifest identity. Native admission takes its minimum with the selected hard
profile ceiling before reading blobs. `payload_bytes()` reports the exact admitted
deduplicated inventory sum. Sequential request admission passes the remaining
allowance, then charges that actual sum only on success; failed reads expose no
partial handle. There is no whole-process RSS or aggregate typed-allocation limit:
request/manifest JSON, decoded poses and other bounded native handles have their
own overhead. Shared image ownership avoids retaining second copies of receipt
images. No schema converts to an execution/rights/measurement/AP token.

The [validation](../validation/2026-10-29-corpus-v2.md) checks exact budgets, repeated
receipt charging, refusal before absent payload directories, strict version pins,
full synthetic 12-view/24-mask replay and actual 60-image receipt preflight. Real
pose/capture/corpus/recipe qualification remains open.
