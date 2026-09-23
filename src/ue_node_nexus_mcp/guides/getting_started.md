# Getting started with the thin facade

The MCP surface is exactly seven tools. Everything else is an internal operation
you discover, inspect, and run through them.

## The core loop

Configure `--project` or `UE_NEXUS_PROJECT_PATH` with the exact `.uproject`.
Use `bridge_instance_ensure` to reuse it (`reuse_only` by default); explicitly
choose `mode="reuse_or_start"` and `dry_run=False` when startup is intended.
Poll `bridge_instance_status` for a STARTING instance. Read the `instances`
workflow guide for ownership, cleanup, shared repositories and migration.

1. `ue_context_get()` — enabled groups, bound editor instance, facade tools.
2. `ue_capability_get(group="graph", detail="index")` — list operations in a group.
3. `ue_capability_get(operation="node_params_set", detail="schema")` — exact payload schema. Never guess parameters from an operation name.
4. `ue_execute(operation="node_params_set", payload={...})` — run it.
5. `ue_diff_get(since_token="diff_...")` — verify what changed.

`detail="examples"` returns a complete example payload for any operation.
Finish editor work with `bridge_instance_release`. Idle heartbeats and status
queries do not keep an unused editor alive.

## Reads return summaries by default

`ue_execute` defaults to `response.mode="summary"` for read operations: you get a
one-line text summary, not the data payload. To get real data:

- Prefer `ue_read(target=..., format="detail")` for common state.
- Or `ue_execute(..., response={"mode": "full"})` for the raw bridge envelope.
- `response={"format": "full"}` is invalid; `format` belongs in the operation
  payload (read shape), `mode` belongs in `response` (envelope detail).

Responses larger than the inline limit come back as an artifact handle instead
of raw data; fetch with `ue_read(target="artifact", query={"artifact_id": "..."})`.
Pages default to 48 KiB; add `path` (e.g. `"data.conflicts"`) to page a list by
whole items, and follow `next_read` / `page_lists_with`.

## Writes are dry-run first

Write operations default to `dry_run=true`. The flow for any write:

1. Read the schema, build a minimal typed payload.
2. Optionally batch-check with `ue_plan_validate(operations=[...])`.
3. Execute with `dry_run=true`, inspect the reported plan.
4. Re-execute with `dry_run=false`, then verify with `ue_diff_get` or the
   narrowest matching read operation.

## When something looks missing

If an operation named in the docs is absent from `ue_capability_get`, either its
feature group is disabled (check `ue_context_get`), it is flagged `hidden`
(retry with `include_hidden=true`), or the server process is running stale code.
