---
name: ue-node-nexus-mcp
description: Operate and diagnose the UE Node Nexus MCP bridge for Unreal Editor asset, graph, AutoIndex, Niagara, and thin facade workflows. Use when the user mentions UE Node Nexus MCP, UEMCP Bridge, ue-node-nexus-mcp, ue_execute, ue_read, ue_diff_get, auto_index, asset_list returning empty data, project_context_get, or MCP bridge context issues.
---

# UE Node Nexus MCP

Live MCP results are the source of truth. Judge runtime behavior before trusting source comments or stale docs. This skill holds only the judgment the tool schemas do not — discover names, schemas, groups, and targets at runtime, don't memorize them.

## Public surface
Fixed at 6 facade tools: `ue_context_get`, `ue_capability_get`, `ue_execute`, `ue_read`, `ue_diff_get`, `ue_plan_validate`. Low-level operations never appear in `list_tools`: discover them with `ue_capability_get`, run them through `ue_execute`, read common state through `ue_read`.

## Response modes
`ue_execute.response.mode` ∈ `silent | brief | ids_only | delta | summary | full | debug`. Use `full`/`debug` only for the raw bridge envelope. `detail` is NOT an execute mode — it is `ue_read.format`.

## First probe (any bridge/context question)
1. `ue_context_get(include_counts=true)`
2. `ue_capability_get(detail="index")`
3. `ue_execute("bridge_capabilities_get", {}, response={"mode":"full"})`
4. `ue_execute("project_context_get", {}, response={"mode":"full"})`
5. `ue_execute("diagnostics_get", {"severity":"all"}, response={"mode":"full"})`

## Empty asset / AutoIndex diagnosis
Never conclude "no active project" from one empty `assets`/`items`. Cross-check `project_context_get` (path, content dir, `/Game` mount), `asset_list` (`package_paths=["/Game"]`, small limit), `auto_index_status`/`auto_index_overview`, `diagnostics_get`. Read as:
- context empty/mismatched → wrong editor instance or context.
- assets present but AutoIndex empty → index lifecycle/persistence issue.
- both empty + valid context → AssetRegistry scan/mount/filter issue.

Gotcha: `asset_list(format="indexed")` does not populate row `items`; use `format="compact"` (rows `[object_path, class, loaded, redirector]`) or `"full"` to read rows. Filter with `class_names` + `package_paths`.

## Safe writes
`ue_capability_get(operation, detail="schema")` → minimal typed payload → `ue_plan_validate` for high-risk/batch → `ue_execute` (default `delta`) → verify with `ue_diff_get` or the narrowest readback. Keep capabilities as generic primitives: no scenario template tools (e.g. `create_fire_effect`), no arbitrary Python, no broad UObject or level-instance writes.

## Reading
Route common state through `ue_read`; default returns summary/index, request `format="detail"`/`"debug"` only when raw fields are needed, and fetch stored payloads via `target="artifact"`. Get the current target list from `ue_context_get`/`ue_capability_get` rather than memorizing it.

## Don't guess calls
Read the schema (`ue_capability_get(operation, detail="schema")`) before invoking — do not infer params from the name. A `*_patch`/`*_set` op never reads: to read use the matching `*_get`/`*_details` op (e.g. Blueprint components via `blueprint_details_get(include_components=true)`, NOT `blueprint_components_patch`). Read ops whose args are all optional still need at least one target (e.g. `material_interface_resolve` needs `asset_path`/`material_path`/`component_path`); an empty call is a request error, not a missing asset.

## Evidence order
live op result > bridge diagnostics > project context/mounts > AssetRegistry > AutoIndex state/index path > repo source & README > historical notes.
