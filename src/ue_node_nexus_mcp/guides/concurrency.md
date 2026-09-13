# Concurrency and batching

For concurrent edits to the same asset, give each agent an independent
`ue_sync("checkout")` workspace in the same project repository. Local commits
retain their fork history; push merges them with current UE memory and returns
persistent conflicts when necessary. Load `workflow_guide_get(category="collaboration")`
for stage/commit, conflict resolution, history and recovery. The operation
rules below apply to direct bridge calls.

The bridge executes each request on the UE game thread, one at a time per
editor instance. Parallel MCP calls do not run UE work in parallel — they
queue. Plan accordingly.

## Operation classes

- **Parallel-safe (read-only)**: all `kind="read"` operations
  (`asset_get`, `graph_snapshot_get`, `auto_index_query`,
  `blueprint_details_get`, `niagara_*_get`, `diagnostics_get`, ...). Issuing
  several concurrently is safe; they serialize on the game thread but cannot
  conflict.
- **Per-asset safe (writes)**: write operations on *different* assets do not
  conflict (`node_params_set` on two different materials, `asset_move` of two
  unrelated assets). Never target the same asset from two in-flight writes —
  order is not guaranteed once queued.
- **Sequential only**: operations that change what other calls resolve against:
  `bridge_instance_select` (rebinds the session), `level_open` (invalidates
  every previously read actor path), `asset_delete` / `asset_move` /
  `asset_rename` for paths other calls are about to use, `level_actor_delete`,
  `auto_index_rebuild`, and the hidden editor-lifecycle ops. Run these alone
  and re-read state afterwards.

## Practical rules

1. Front-load reads; they are cheap and safe to overlap.
2. One writer per asset. Batch a multi-step edit on one asset into a single
   `graph_patch_apply` / `graph_build_apply` call instead of many small writes.
3. After a rename/move/delete, re-resolve paths (AutoIndex short names via
   `ue_read(target="auto")`) before further writes.
4. Long-running requests (big compiles, whole-graph reads) hold the game thread;
   raise `UE_NEXUS_TIMEOUT_SECONDS` rather than firing the call repeatedly.

## batch_execute

`batch_execute` is an MCP-local operation that runs a short list of internal
operations sequentially in one tool call:

- Every item is validated first (unknown operation, disabled group, missing
  required fields); an invalid batch executes nothing.
- Items run in order; by default the batch stops at the first failure and marks
  the rest as skipped (`continue_on_error=true` keeps going). A bridge
  connection failure always aborts the remainder.
- Each item reports `ok`, a compact summary, and diagnostic counts. Nesting
  `batch_execute` inside itself is rejected.

Use it for short, ordered sequences on the same workflow (create → wire →
compile). Prefer `ue_plan_validate` first for high-risk batches, and prefer a
single patch/build op over a batch when one exists.

## Background task queue

For a long-running call you do not want to block on (a big `asset_compile`,
a heavy `graph_build_apply`, a whole `batch_execute`), submit it instead:

- `task_submit {"operation": ..., "payload": ...}` validates upfront exactly
  like a batch item, queues the call, and returns a `task_id` immediately.
- One background worker runs tasks strictly in submission order, so queued
  writes never interleave with each other over the bridge.
- Poll `task_status {"task_id": ...}` (or with no id to list all tasks), then
  fetch the stored full response with `task_result`. `task_cancel` only works
  while a task is still queued.
- A task may wrap `batch_execute`, but `task_*` operations cannot wrap each
  other. Task state is in-memory: ids do not survive a server restart.
- The wrapped call still honors `UE_NEXUS_TIMEOUT_SECONDS`; raise it for big
  compiles — the queue does not remove the per-call timeout.
