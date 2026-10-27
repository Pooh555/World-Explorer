# External MODEL observation requests

The shared [`external_model_inspection.hpp`](../../include/world_explorer/application/external_model_inspection.hpp)
command reopens independently selected small receipt snapshots and invokes the
[external MODEL v2 observer](asset_external_models.md). CLI:

```sh
build/dev/we_process inspect-external-models REQUEST.json REQUEST_SHA256
```

Success prints one JSON report to stdout and exits 0; refusal prints an error to
stderr, produces no report and exits 2. SIGINT/SIGTERM use the existing cooperative
cancellation token. No worker, installer, model loader, download, cache or output
writer is started. A0/AP0 remain **IN_PROGRESS**.

## Independently frozen request

The caller must select the lowercase SHA-256 of the exact request bytes outside
the request. Reading that checksum from the request or an inspected receipt would
not provide an independent selection. This binds bytes; it authenticates neither
the caller nor a reviewer. Requests are at most 2 MiB, strict UTF-8 JSON with no
duplicate keys/raw NUL and at most 16 nesting levels. Fields are exactly:

| Field | Contract |
|---|---|
| `schema_version`, `kind` | Integer `1`, `EXTERNAL_MODEL_OBSERVATION` |
| `source_kind` | Required caller declaration: `SYNTHETIC` or `UNQUALIFIED`; neither certifies real provenance |
| `recipe_receipt`, `evidence_receipt` | Each exactly `directory`, `sha256`, independently selecting the original small receipt |
| `product_id` | Nonzero original recipe product ID |
| `context` | Exactly `world_epoch`, nonzero `pose_revision`; original expected context |
| `host` | Exactly boolean `contracts_only`, and unsigned `host_ram_bytes`, `device_vram_bytes`, `free_disk_bytes`, `cpu_threads`, `queued_jobs` |
| `limits` | Exactly independently chosen `file_bytes`, `total_bytes`; positive, file ≤total, ≤4 GiB/file and ≤8 GiB/pass, file ceiling fits `size_t` |
| `models` | 1–16 rows, exactly `component_sha256`, `files`; 1–64 files/model, ≤256 total |
| `reviews` | 0–16 rows, exactly `role` (`MODEL`), `component_sha256`, `review_sha256`, `reviewer` |

Each selected file has exactly `path` (logical name ≤240 bytes), lowercase
`sha256`, positive unsigned `bytes`, and `file` (explicit native path). Every
declared size counts against the independent file/total ceilings before receipts
are opened. Review labels are ≤128 bytes; the lower-level observer checks their
ASCII repertoire, independent joins, duplicate coverage, logical-path rules and
all original manifest/recipe/terms/status identities.

Native path strings are UTF-8, nonempty, no NUL, ≤4,096 bytes and ≤64 resolved path
segments. Relative paths resolve from the canonical request's parent, never cwd,
an inspected receipt or a MODEL manifest. Dot/dot-dot segments and partial native
roots are rejected; use explicit absolute paths for locations outside that
parent. Request parents and selected receipt roots/ancestors must be nonsymlink
directories; the request must be a regular nonsymlink file. MODEL leaves/parents
retain the stronger observer's canonical/single-link checks. Explicit locations
may contain native spaces/Unicode; logical MODEL names retain portable ASCII.

All request tables/types/path strings and count/byte ceilings are parsed before
either receipt or external payload is opened. Each receipt retains its original
64 MiB aggregate/16 MiB file limit; there are exactly two snapshots (≤128 MiB
payload bytes together). Their graphs/context and the original recipe/input pins
are revalidated. No selection is discovered from the model manifest being tested.

## Report, failure and authority

The deterministic report pins the request, both receipts, original recipe/graph,
product/context, supplied host/ceilings, per-model terms/review/reviewer and exact
resolved file identities. It includes observed bytes, twice-hashed bytes, two
passes, optional known installation/reserved costs, per-model review selection
and every remaining blocker by name. Host capacities and `source_kind` are
caller declarations, not hardware/provenance measurements. Always:

```json
{"source_kind_origin":"CALLER_DECLARED","qualification":"UNQUALIFIED",
 "observation_only":true,"immutable_payloads":false,"execution_authorized":false}
```

Wrong independently selected request hash is `RESYNC_REQUIRED`; invalid hash
syntax is `INVALID_ARGUMENT`; malformed records/types/paths are `INVALID_DATA`;
unsupported schema/kind/source labels are `UNSUPPORTED_OPERATION`; count/byte
excess is `RESOURCE_EXHAUSTED`; invalid ceilings/zero product or pose are
`INVALID_ARGUMENT`. Receipt/observer errors propagate, including stale joins,
changed files, missing data and cancellation. Filesystem exceptions are
`IO_ERROR`; other callback/backend exceptions are `BACKEND_FAILURE`.

The public optional progress callback is forwarded unchanged. No partial report
or publication occurs; the command writes no caller bytes, while callback effects
remain caller-owned. Small receipts/request are inspected as pinned owned bytes;
the result asserts no later live filesystem state. External payload observations
retain cooperative-filesystem race/timestamp/post-return limitations and no lock.
Global component/installation/review/runtime/worker-limit and original rights/
host/unknown-cost blockers remain. Exit 0 grants no acquisition, execution,
legal approval, AP qualification or platform milestone.
