# Native collider batch diagnostic and CLI replay

[`inspect_native_colliders`](../../include/world_explorer/application/native_collider_diagnostic.hpp)
joins current privately admitted native assets and a private schema-4
[collider package](collider_exports.md), then runs actual
[static-shape queries](native_static_shapes.md). It returns owned request/report
JSON. It installs no physics body, support ledger, navigation session or walking
token. All positions/errors remain arbitrary units and results are labeled
`SYNTHETIC_GAMEPLAY_COLLIDER_DIAGNOSTIC`.

## Private selection and batch execution

The command selects exact receipt/context, canonical catalog/instance hashes,
definition/collider/base references, original canonical cook SHA and pivots from
private admission. Each instance frame composes its current placement with the
compensated asset pivot. The selected package must match those exact catalog and
instance bytes, references and companion frames, even when its render LOD differs.
An old package cannot service newly admitted moved/revised instances. Visibility,
definition identity and `world_from_asset` also join exactly to canonical scene
metadata; a change smaller than a loss tolerance still invalidates that selection.

The batch permits at most two collider definitions, 64 collider instances and
256 explicit uniquely identified nonzero queries. Render-only definitions may
remain in the package; querying their instances refuses. Queries identify an
instance and world-space start/end. All original vertices and origin/unit-axis
probes independently compare composed versus sequential placement against
`world_position_units` (default `1e-8`, finite [0, 1]). The shape's separate
`local_vertex_units` and `world_endpoint_units` limits default to `1e-4` each.
No bound is relaxed automatically.

One local Jolt shape serves all copies of its definition within this batch.
Every query uses its checked frame; shapes/placements do not survive as a live
scene cache. Source corrections require a newly selected snapshot on the next
call. Failures/cancellation return no partial report and preserve caller data.

## Frozen request replay

Save `m_request_json` returned by the shared command, export its package with the
existing explicit writer, then run:

```sh
build/dev/we_process inspect-colliders REQUEST.json REQUEST_SHA256
```

The command writes one compact JSON report to stdout and exits 0 on completed
inspection, including legitimate misses. Usage, malformed/stale input and query
failures exit 2 with an error on stderr. Signal cancellation uses the existing
CLI stop token. No output file, model, worker, network call or scene update is
created by this command.

Request schema 1 has exactly `schema_version`, `kind`
(`NATIVE_COLLIDER_DIAGNOSTIC_REQUEST`), `package_directory` and `selection`.
The whole original request is independently SHA-pinned, at most 1 MiB, parsed
strictly for duplicate/unknown fields, depth, NUL and numeric/count bounds. The
directory resolves relative to the request's parent, independently of cwd.
Its path is selected by the request; the inspector discovers no current source
pin from unselected package data.

`selection` contains package manifest, receipt, canonical instance/catalog SHAs,
epoch/pose, the three-field independent policy, collider definitions/pivots/
placements and query tables. Every row is exact-field checked. The reader owns
all selected package bytes: ≤64 files, ≤1 MiB/file, ≤8 MiB aggregate, including
the selected manifest. Its closed inventory checks sizes, hashes, portable
relative paths, every parent directory and all entries; duplicate/unlisted
files, extra directories, special files and symlinks refuse. Original admission
and package format/bytes remain unchanged.

Replay checks the pinned catalog/scene and companion references/frames, decodes
the owned neutral triangle input, cooks Jolt and runs the same batch path.
It does **not** reopen or re-admit the original capture, receipt DAG, provenance
or rights evidence. Request/manifest hashes are integrity bindings, not
authentication or real production evidence. Replaying an old pinned request with
its old package inspects that frozen snapshot successfully; it cannot assert
that this snapshot remains the external world's current scene. The shared typed
command is the boundary that selects fresh private asset/instance handles.

## Report and limits

The bounded ≤8 MiB report records the selection/package/receipt/instance hashes,
context, limits, definition/backend/cook/build/decoded-geometry identities,
decoded source-face triangles and observed local displacement. It includes every
checked instance frame and composition residual, then each query's original
segment, optional fraction/base-face/world-point hit, endpoint conversion loss
and scaled local surface loss. Those separate diagnostics provide neither an
overall hit-distance nor clearance certificate.

Shared and replay reports are identical for the same selection and tested backend;
they are not a portable Jolt cache ABI or binary reproducibility guarantee.
Real production/scale/support, contact dynamics, convex props, live instance
updates, measured walking and AP8 runtime qualification remain separate.
