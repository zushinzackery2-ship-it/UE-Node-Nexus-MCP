---
name: ue-node-nexus-mcp
description: Operate and diagnose the UE Node Nexus MCP bridge for Unreal Editor asset, graph, AutoIndex, VFX (Niagara & Cascade), and thin facade workflows. Use when the user mentions UE Node Nexus MCP, UEMCP Bridge, ue-node-nexus-mcp, ue_execute, ue_read, ue_diff_get, auto_index, asset_list returning empty data, project_context_get, or MCP bridge context issues.
---

# UE Node Nexus MCP

Live MCP results are the source of truth. Judge runtime behavior before trusting source comments or stale docs. Discover exact param schemas at runtime; use the Read cheatsheet below to pick the right operation instead of guessing by name.

## Public surface
Fixed at 6 facade tools: `ue_context_get`, `ue_capability_get`, `ue_execute`, `ue_read`, `ue_diff_get`, `ue_plan_validate`. Low-level operations never appear in `list_tools`: discover them with `ue_capability_get`, run them through `ue_execute`, read common state through `ue_read`. High-risk compatibility ops flagged `hidden` (e.g. `editor_save_all`, `editor_request_exit`, `auto_index_clear`) are omitted from the `ue_capability_get` index unless `include_hidden=true`; querying one by name and executing it still works. `ue_diff_get` pages truncated change lists: pass the returned `next_cursor` back as `cursor`.

Task-level recipes are served in-band: `ue_execute("workflow_guide_get", {})` lists guide categories (getting_started, graph_editing, material_authoring, blueprint_authoring, niagara_authoring, diagnostics_repair, concurrency); `{"category": "..."}` returns one guide body, `{"query": "connect pins"}` keyword-routes to the best guide. Pull the matching guide before starting an unfamiliar multi-step workflow.

> If an operation named in this skill is missing from `ue_capability_get`, your MCP **server process is running stale code** — reinstall the plugin's `MCPServer` Python and restart the MCP client. The bridge plugin (UE editor) and the Python server must BOTH be current; updating one without restarting the other is the most common "feature X doesn't work" cause.

## Instance selection (multi-editor)
Transport is a per-editor Windows named pipe (`\\.\pipe\UeNodeNexusBridge.<pid>`), not a TCP port — rapid editor restarts and multiple concurrent editors no longer collide. Each MCP (Agent) session binds to ONE editor:
- One editor live → auto-bound on first call; nothing to do.
- Two+ live → calls error until you pick: `ue_execute("bridge_instance_list", {})` then `ue_execute("bridge_instance_select", {"pid": <pid>})` (or `{"project": "<name substring>"}`). Both are MCP-local control ops (not forwarded to UE).
- `ue_context_get` reports `active_instance` (pid + auto/explicit mode) and `available_instances`.
- An auto-bound session re-binds transparently across an editor restart; an explicitly-selected instance that exits errors until you re-select. "selected instance is gone" / "no UE editor instance found" → the editor closed or the plugin is not loaded.

## Getting actual data from read operations
`ue_execute` **defaults to `response.mode="summary"` for reads** — returns a one-line text summary, NOT the bridge data payload. To get real data:
- **`ue_execute(..., response={"mode":"full"})`** — requests the raw bridge envelope with full `data` field; large payloads are stored as artifact handles instead of entering tool context
- **`ue_read(target=...)`** — the recommended path for reads; returns an artifact token for the full response
- Do NOT use `ue_execute` without `response.mode="full"` and expect to see data fields
- Do NOT use `response={"format":"full"}`. `response.format` is invalid; use `response={"mode":"full"}` for envelope detail, or put `format` in the operation payload / `ue_read(format="detail")` for read shape.

## Read cheatsheet (intent → call)
Prefer `ue_read(target=...)`; otherwise the operation via `ue_execute`. Query `ue_capability_get(operation, detail="schema")` for exact params; `detail="examples"` for a complete payload example.
- asset metadata → `ue_read(target="asset")` / `asset_get`
- fuzzy path / unknown asset type → `ue_read(target="auto", asset_path="terrain_demo", format="detail")` — resolves through AutoIndex, inspects asset class with `asset_get`, then routes to the narrow read op
- **Material / Material Function / Blueprint node graph** → `ue_read(target="graph")` or `graph_snapshot_get(graph_kind="material"|"material_function"|"blueprint")` — this is the core node-graph reader, NOT a `*_summary` op
- **Material static lint** → `material_lint` via `ue_execute`, a Python-local read operation that does not compile; it reads graph node params + texture summaries to flag sampler/texture compression/sRGB mismatches and placeholder texture risks
- **Material Instance params / parent chain** → `ue_read(target="material_instance")`, `material_instance_params_get`, or `material_interface_resolve`
- There is no `ue_read(target="material")`; use `auto` for unknown material-like assets, `graph` for Material/Material Function nodes, or `material_instance` for instance parameters.
- **Graph params** → `graph_snapshot_get(format="full", include_node_params=true)` returns compact Material / Material Function param values by default (`node_params_format="compact"`), not full enum/metadata schema. Whole-graph full schema (`node_params_format="full"`) is blocked before UE execution unless `response={"allow_heavy": true}` is supplied; use `wires_tiny` first, then `node_params_get` for specific nodes.
- **Large blueprint sub-graph drill** → `graph_snapshot_get(keyword="Damage")` or `graph_node_info_get(keyword="Damage")` for node-name substring, `trace_from="Event BeginPlay", trace_depth=5` for BFS neighborhood, `node_class_filter=["CallFunction"]` for class filtering, `exec_only=true` for exec-pin-only wires. All 5 filter params work on both `graph_snapshot_get` (topology) and `graph_node_info_get` (dense node info). Response includes `filter_stats` (total/matched/included nodes). Blueprint `graph_snapshot_get` responses always include `available_graphs` (name + node count) — use it to pick `graph_name` instead of guessing
- Blueprint vars / defaults / components → `blueprint_details_get(include_components=true, include_inherited_components=true)`
- AnimBlueprint graph nodes → `anim_blueprint_summary_get`
- AnimBlueprint state machines (states / entry_state / transitions with from/to/rule/blend) → `anim_state_machine_summary_get` / `ue_read(target="anim_state_machine")` — graph_snapshot_get does NOT descend the state-machine sub-graph
- AnimMontage sections/slots/segments/notifies → `anim_montage_summary_get`
- BlendSpace axes/samples → `blend_space_summary_get`
- Cascade (`UParticleSystem`) emitters/modules (+ normalized module param values: Spawn/Lifetime/Size/Color/Velocity/Location/Rotation/Light, in `format="full"`) → `cascade_system_summary_get`
- Niagara → `ue_read(target="niagara_system"|"niagara_stack")` / `niagara_*`
- level actors / component materials → `level_*` / `component_*`
- asset dependency graph → `asset_dependencies_get` / `asset_referencers_get` / `ue_read(target="asset_dependencies"|"asset_referencers")` — AssetRegistry package links as `[package_name, hard|soft]` rows; engine/script packages excluded unless `include_engine=true`
- **UE log tail** → `log_tail_get` / `ue_read(target="log")` — MCP-local read of the newest project log (`tail_kb`, `match` substring filter, `max_lines`); use for raw log lines when `diagnostics_get.related_log_items` is too narrow
- **Viewport screenshot** → `viewport_capture` (returns the target PNG path immediately; the file is written asynchronously after the next viewport redraw) then poll `viewport_capture_status {"file_path": ...}` (MCP-local file check). `filename` must be a bare letters/digits/underscore/dash name; output always lands in the project `Saved/Screenshots/` dir
- Landscape Paint layer info binding → `landscape_layer_info_set` after reading `object_properties_get(TargetLayers)`; this is a narrow write for `LandscapeLayerInfoObject`, not a generic UObject property setter.
- SoundCue internal USoundNode tree → `sound_cue_summary_get` / `ue_read(target="sound_cue")`
- Texture2D dimensions / source+pixel format / compression / sRGB / LOD group → `texture_summary_get` / `ue_read(target="texture")`
- **Project diagnostics / current error items** → `diagnostics_get` / `ue_read(target="diagnostics")`. Global diagnostics report `data.error_count`, `data.warning_count`, and `data.items`; `remaining_errors` is reserved for concrete asset responses that carry `asset_path`. Python also adds `data.related_log_items` for UE log material compile fallback / sampler mismatch clues; these are historical and carry `stale_possible=true`, so do not treat them as current global errors.

## Patch operations cheatsheet
All patch ops use `operations: [{"op": "<verb>", ...}]`. Always run `ue_capability_get(operation, detail="examples")` first. All default to `dry_run=true`.

**blueprint_components_patch** — `op`: `add_component` | `remove_component` | `set_component_defaults` | `set_component_properties`. `defaults` and `properties` are aliases for component template defaults; reflected fields accept UE property names, plus convenience `RelativeTransform` and `material`.
```json
{"op":"add_component","component_class":"/Script/Niagara.NiagaraComponent","name":"AimVFX","parent":"Mesh","defaults":{"Asset":"/Game/FX/NS_AimMagicCircle.NS_AimMagicCircle","bAutoActivate":false,"RelativeTransform":{"location":{"x":0,"y":0,"z":50},"rotation":{"pitch":0,"yaw":0,"roll":0},"scale":{"x":1,"y":1,"z":1}}}}
{"op":"set_component_defaults","name":"AimVFX","defaults":{"bAutoActivate":false}}
{"op":"remove_component","name":"OldComponent"}
```

**graph_patch_apply** — `op`: `connect_pins` | `disconnect_pins` | `set_node_param` | `create_node` | `delete_node` | `set_node_position`. Pins accept **GUID** (`from_pin_id`/`to_pin_id`) OR **name** (`from_pin`/`to_pin`) — name is easier, GUID is unambiguous when a node has duplicate pin names. Blueprint supports same-batch `create_node.client_id` with dry-run transient nodes. Material/MaterialFunction same-batch `create_node.client_id` is expanded by the Python MCP layer only when `dry_run=false`; for dry-run, split create/read/connect or expect a client-side unsupported dry-run error.
```json
{"op": "create_node", "client_id": "branch", "node_class": "Branch", "position": {"x": 300, "y": 0}}
{"op": "connect_pins", "from_node_id": "<GUID>", "from_pin": "Then", "to_node_id": "branch", "to_pin": "execute"}
{"op": "connect_pins", "from_node_id": "<GUID>", "from_pin": "ReturnValue", "to_node_id": "<GUID>", "to_pin": "NewParam"}
{"op": "connect_pins", "from_node_id": "<GUID>", "from_pin_id": "<PIN_GUID>", "to_node_id": "<GUID>", "to_pin_id": "<PIN_GUID>"}
{"op": "set_node_param", "node_id": "<GUID>", "name": "PinName", "value": "string_or_number"}
```

**project_input_mappings_patch** — `op`: `add_action_mapping` | `remove_action_mapping` | `add_axis_mapping` | `remove_axis_mapping`
```json
{"op": "add_action_mapping", "action_name": "Aim", "key": "RightMouseButton"}
{"op": "add_axis_mapping", "axis_name": "MoveForward", "key": "W", "scale": 1.0}
```

**landscape_layer_info_set** — binds or creates `ULandscapeLayerInfoObject` assets for named Landscape Paint target layers. Layer `name` must match the material layer name exactly. Defaults to `dry_run=true`.
```json
{"actor_path":"/Game/Maps/Demo.Demo:PersistentLevel.Landscape_0","layers":[{"name":"Cliff","layer_info_asset_path":"/Game/Maps/Demo_sharedassets/Cliff_LayerInfo.Cliff_LayerInfo","create_if_missing":true,"no_weight_blend":false}],"dry_run":true,"save":false}
```

**node_params_set** — `params` is `{"pin_name": value}` object (NOT array). Pin names must match exact UE pin names; use `node_params_get` to discover them.

## Response modes
`ue_execute.response.mode` ∈ `silent | brief | ids_only | delta | summary | full | debug`. Read ops default to `summary` (text-only); write ops default to `delta`. Use `full`/`debug` only when you need the raw bridge envelope; large payloads return `artifact.id`, byte size, estimated tokens, and summary instead of raw `data`. `ue_execute.response` only supports `mode` and `allow_heavy`; if `invalid_response_field` reports `format`, move `format` into the operation payload or change it to `response.mode`. `detail` is NOT an execute mode — it is `ue_read.format`.

## First probe (any bridge/context question)
1. `ue_context_get(include_counts=true)`
2. `ue_capability_get(detail="index")`
3. `ue_execute("bridge_capabilities_get", {}, response={"mode":"full"})`
4. `ue_execute("project_context_get", {}, response={"mode":"full"})`
5. `ue_execute("diagnostics_get", {"severity":"all"}, response={"mode":"full"})` — reads UE MessageLog and project asset compile diagnostics; use `data.error_count` for global count, and inspect `data.related_log_items` for stale-but-useful material log clues

## Empty asset / AutoIndex diagnosis
Never conclude "no active project" from one empty `assets`/`items`. Cross-check `project_context_get` (path, content dir, `/Game` mount), `asset_list` (`package_paths=["/Game"]`, small limit), `auto_index_status`/`auto_index_overview`, `diagnostics_get`. Read as:
- context empty/mismatched → wrong editor instance or context.
- assets present but AutoIndex empty → index lifecycle/persistence issue.
- both empty + valid context → AssetRegistry scan/mount/filter issue.

Gotcha: `asset_list(format="indexed")` does not populate row `items`; use `format="compact"` (rows `[object_path, class, loaded, redirector]`) or `"full"` to read rows. Filter with `class_names` + `package_paths`.

## Safe writes
`ue_capability_get(operation, detail="schema")` → minimal typed payload → `ue_plan_validate` for high-risk/batch → `ue_execute` (default `delta`) → verify with `ue_diff_get` or the narrowest readback. Keep capabilities as generic primitives: no scenario template tools (e.g. `create_fire_effect`), no arbitrary Python/console execution, no broad `object_properties_set`-style reflection writes. Level-actor lifecycle uses narrow typed ops instead: `level_actor_spawn` (`class_path` accepts engine short names, `/Script/` paths, or Blueprint asset paths), `level_actor_delete`, `level_actor_transform_set` (at least one of location/rotation/scale) — all default `dry_run=true`. `level_open` switches maps and refuses to drop a dirty map unless `discard_changes=true`; after it, all previously read actor paths are stale — re-list before further writes.

## Concurrency & batching
The bridge runs each request on the UE game thread, one at a time per editor instance — parallel MCP calls queue, they do not overlap UE work. Classes:
- **Parallel-safe**: all read ops. Overlap freely.
- **Per-asset safe**: write ops on *different* assets. Never two in-flight writes on the same asset.
- **Sequential only**: `bridge_instance_select`, `auto_index_rebuild`, deletes/moves/renames of paths other calls will use, hidden editor-lifecycle ops. Run alone, re-read state after.

`batch_execute` (MCP-local) runs a short ordered list of operations in one call: `ue_execute("batch_execute", {"operations": [{"operation": ..., "payload": {...}}, ...]})`. The whole batch is validated first (invalid batch executes nothing); execution stops at the first failure unless `continue_on_error=true`; a bridge connection failure aborts the remainder; per-item `dry_run` keeps its meaning. It is a roundtrip saver, NOT a transaction — applied items stay applied. Prefer one `graph_patch_apply`/`graph_build_apply` over a batch when a single op covers the edit, and `ue_plan_validate` first for high-risk batches.

Background tasks (MCP-local): `task_submit {"operation": ..., "payload": ...}` validates like a batch item, queues the call on one background worker (strict submission order, no interleaved writes), and returns a `task_id` immediately — use it for long calls you do not want to block on (big compiles, a whole `batch_execute`). Poll `task_status`, fetch the stored full response with `task_result`, cancel a still-queued task with `task_cancel`. `task_*` ops cannot wrap each other; task ids are in-memory and die with the server. The wrapped call still honors `UE_NEXUS_TIMEOUT_SECONDS`.

## Don't guess calls
Read the schema (`ue_capability_get(operation, detail="schema")`) before invoking — do not infer params from the name. Use `detail="examples"` for a valid payload example. A `*_patch`/`*_set` op never reads: to read use the matching `*_get`/`*_details` op (e.g. Blueprint components via `blueprint_details_get(include_components=true)`, NOT `blueprint_components_patch`). Read ops whose args are all optional still need at least one target (e.g. `material_interface_resolve` needs EXACTLY one of `asset_path` / `material_path` / `component_path`+`slot_index`); an empty call is a request error (`invalid_request`), passing more than one is `target_conflict`, and an out-of-range slot is `invalid_slot` — none of these are `material_not_found`.

## Evidence order
live op result > bridge diagnostics > project context/mounts > AssetRegistry > AutoIndex state/index path > repo source & README > historical notes.
