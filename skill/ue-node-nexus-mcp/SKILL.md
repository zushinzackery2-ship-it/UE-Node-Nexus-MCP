---
name: ue-node-nexus-mcp
description: Operate the UE Node Nexus MCP bridge to inspect and edit a running Unreal Editor — assets, Material/Blueprint graphs, Niagara, levels, diagnostics — and to author assets through the Content_Transcoded text mirror (ue_sync pull/push of .nexus files). Use when the user mentions UE Node Nexus MCP, UEMCP Bridge, ue-node-nexus-mcp, ue_sync, .nexus files, Content_Transcoded, L2U/U2L, ue_execute, ue_read, ue_diff_get, ue_capability_get, auto_index, project_context_get, empty asset lists, or any MCP-to-Unreal bridge question.
---

# UE Node Nexus MCP

Seven MCP tools in front of one Unreal Editor. The editor stays the compiler; this skill tells you which door to use and what the bridge guarantees.

**Ground rules**

1. Live results beat everything: a fresh tool result outranks source comments, README text and this file.
2. Never guess a payload. `ue_capability_get(operation, detail="schema")` first, `detail="examples"` for a valid body.
3. Author assets through the text mirror (`ue_sync`); use graph ops only for what the mirror marks `@opaque`, for levels, and for asset management.
4. Writes default to `dry_run=true`. Read the plan, then apply.

## 1. Public surface

| Tool | Purpose | Default response |
|:-----|:--------|:-----------------|
| `ue_context_get(include_counts)` | Enabled groups, bound editor instance, recommended next call | summary |
| `ue_capability_get(group, operation, detail)` | Operation index (`detail="index"`), one op's `schema` / `examples` | summary |
| `ue_execute(operation, payload, response)` | Run any registry operation | `delta` for writes, `summary` for reads |
| `ue_read(target, asset_path, format, query)` | Typed reads with artifact handles for big payloads | summary |
| `ue_diff_get(since_token, cursor, limit)` | Changes since a diff token, cursor-paged | — |
| `ue_plan_validate(operations)` | Validate a batch without touching UE | — |
| `ue_sync(action, paths, options)` | Independent workspaces, local history, semantic merge, UE publication/recovery and schema | summary + artifacts |

- 138 registry operations sit behind `ue_execute`, including internal scene transport and `transcode_recover`. Hidden ops still run by name; list them with `include_hidden=true`.
- Task recipes are served in-band: `ue_execute("workflow_guide_get", {})` lists categories (`getting_started`, `text_mirror`, `graph_editing`, `material_authoring`, `blueprint_authoring`, `niagara_authoring`, `diagnostics_repair`, `concurrency`); `{"category": ...}` returns one guide, `{"query": "connect pins"}` routes by keyword.
- If an operation named here is missing from `ue_capability_get`, the MCP server process runs stale code: reinstall the Python package and restart the client. Plugin (editor) and server must both be current.

## 2. First probe

Run this before answering any "is the bridge working / what project is this" question:

1. `ue_context_get(include_counts=true)` — bound instance, enabled groups.
2. `ue_execute("project_context_get", {}, response={"mode": "full"})` — `.uproject`, content dir, `/Game` mount.
3. `ue_execute("bridge_capabilities_get", {}, response={"mode": "full"})` — modules, `vfx_available`, `schema_key`.
4. `ue_execute("diagnostics_get", {"severity": "all"}, response={"mode": "full"})` — `data.error_count`, `data.items`.
5. `ue_sync("status")` when the task involves asset content.

## 3. Choose the path

| Task | Use |
|:-----|:----|
| Edit Material / MaterialFunction / MaterialInstance / Blueprint / Niagara / DataAsset content | `ue_sync` loop (section 4) |
| Create one of those from scratch | Write the `.nexus` file, `ue_sync("push")` — the asset is created |
| Node the mirror shows as `@opaque` | `graph_patch_apply` on that node only |
| Inspect a graph, instance parameters, an asset's metadata | `ue_read` (section 5) |
| Create / delete / move / duplicate assets, fix redirectors | `asset_*` ops |
| Level actors, components, material slots, landscape layer info | `level_*`, `component_*`, `landscape_layer_info_set` |
| Loaded Actor/Blueprint/ISM/HISM scene groups | `ue_sync` with `.scene.nexus`; load `workflow_guide_get(category="scene_mirror")` |
| Paged instance reads and batch edits | `component_instances_get/patch`; use the read revision and persistent IDs |
| Compile, validate, save one asset | `asset_compile`, `asset_validate`, `asset_save` |
| Project error list, UE log lines | `diagnostics_get`, `log_tail_get` |
| Screenshot | `viewport_capture` (level viewport, drawn synchronously; `data.exists`, absolute `file_path`); `viewport_capture_status` only if `exists=false` |
| Look at something / second camera angle | `viewport_camera_get`, `viewport_camera_set` (`location` + `look_at` or `rotation`, `fov`) then `viewport_capture` |

## 4. Text mirror (`ue_sync`)

**0.5.0 collaboration workflow**: load `workflow_guide_get(category="collaboration")`.
Each agent creates its own checkout with `dry_run=False`, then edits the
returned `files_root`/`file_paths`. Pass the returned `id` as `workspace_id`.
Stage and commit locally; push merges committed HEAD with current UE memory.
Keep later file edits local. Use `resolve`/`continue`/`abort` with returned
merge IDs; inspect apply receipts with `recover` after interrupted publication.
Use branch/tag, log/show/diff/blame, restore/revert, private reset/rebase/amend,
cherry-pick, stash and reflog for version operations. Mutations default to
preview; `proposal_id` binds the preview when provided.

Schema is the parameter catalog: `schema(category=..., query=..., details=True)`
through `options` reads classified JSON also used by lint. Function and target
context queries supplement dynamic pins and inherited parameters. Read the
returned `schema_path` index first; respect support flags and `context_required`.
Offline/historical schema describes its bound version, and current publication
still checks the editor environment.

First checkout imports legacy baselines and preserves existing local text in
the `imported` workspace. The following legacy loop applies before activation;
after activation, use workspace IDs, local commits and conflict resolutions.

Layout: `<UE_NEXUS_TRANSCODE_DIR or cwd/Content_Transcoded>/<Project>/<Path>/<Asset>.<kind>.nexus` with kinds `mat mf mi bp ns ne asset stub`; `.nexus/base/` is the merge base, `.nexus/schema/<key>/` the reflection lock.

**Loop**

1. `ue_sync("init")` once per mirror root (binds root, exports schema, pulls everything).
2. `ue_sync("status")` — per-asset state: `clean`, `local-modified`, `ue-modified`, `both-modified`, `local-new`, `ue-new`, `local-deleted`, `ue-deleted`.
3. Edit the `.nexus` file with Read / grep / StrReplace. Only non-default values are written; delete a line to reset.
4. `ue_sync("lint", paths)` — offline: unknown class / property / enum / pin / type, dangling links, opaque edits.
5. `ue_sync("push", paths)` — dry run returns the plan (verb counts, risky verbs).
6. `ue_sync("push", paths, options={"dry_run": false})` — one editor transaction per asset, compile, save touched packages only, re-export, rewrite text to canonical form. Diagnostics come back as `file:line: message`.
7. `ue_sync("pull", paths)` after the editor changed something (`ue-modified`).

**Options**: `push` → `dry_run` `compile` `save` `force` `allow_delete` `stop_on_error`; `pull` → `discover` `include_stubs` `force`; `status` → `discover` `include_stubs` `include_clean`; `init` → `pull_all` `include_stubs` `auto_export` `refresh_schema`. `force` is `"local"` or `"ue"` and is the only way past `both-modified`.

**Format essentials**

```
[asset]
BlendMode = BLEND_Translucent

[graph]
c_eps : Constant(R=0.000001) @ -1000,220                # id : Class(param=value) @ x,y
call  : MaterialFunctionCall(MaterialFunction=/Game/F/MF_A.MF_A)
old   : @opaque(/Script/Engine.MaterialExpressionCustom) @ 0,0   # move / delete / link only

c_eps -> call.A                                          # src[.pin] -> dst[.pin]
call -> out.BaseColor                                    # materials: implicit `out` node
c_eps -> out.WorldPositionOffset
```

- Blueprint: `[variables] Health : float = 100 { Category=Stats, InstanceEditable }`, `[components] Mesh : StaticMeshComponent(parent=Root) { RelativeLocation=(X=0,Y=0,Z=50) }`, `[graph EventGraph]` nodes such as `Event(Actor.ReceiveBeginPlay)`, `CallFunction(KismetSystemLibrary.PrintString, InString="Hi")`, `VariableGet(Health)`, `Sequence(pins=3)`; exec pins are `execute` / `then`; `[function Name(A: double) -> (R: bool)]` has implicit `entry` / `result`; `@renamed(Old)` renames. `ParentClass` is honoured only when the asset is created.
- Niagara: `[emitter Name]`, `[stack Name/ParticleUpdate]` lines like `spawn_rate : SpawnRate(SpawnRate=25, Spawn Probability=0.5) !disabled` list what the Stack panel shows; `[renderers Name] sprite : Sprite { SubImageSize=(X=2,Y=2) }`; `[user] Speed : float = 3` with types `float int bool Vector2 Vector Vector4 Color Position Quat` or a class name. `@link(...)` / `@dynamic` inputs are read-only; `SetVariables(...)` can be edited, not created.
- Ids are yours and stable; GUIDs never appear. Renaming an id recreates the node. A link to a new multi-pin node must name the pin.
- A MaterialFunction interface change refreshes every caller automatically.
- `schema_stale` → `ue_sync("schema")` (engine or plugin set changed). `.stub.nexus` files are read-only registry tags.

## 5. Reading state

**Scene groups:** first pull uses `options.scene` with `map_path`, `name` and
`actor_paths`. Files live at `Scenes/<map-relative>/<group>.scene.nexus`.
Subsequent status/lint/pull/push accepts these files or directories. Mirrored
asset dependencies run first; each scene has a separate transaction and recovery
journal. Root actor transforms are world space; attached actors, components and
instances use local space. Pull leaves UE metadata untouched. Ambiguous instance
identity requires explicit `pull(force="ue")`; construction-script arrays are
read-only. Loaded hidden levels are included; unloaded actors stay unavailable.
Use the in-band scene guide for the complete format and boundaries.

**Build and readiness:** capability `build` contains both DLL identities.
Python 0.4.0 requires contract 2 before writes. Explicit material compilation
reports shader readiness and a target RHI update fence; this is not whole-frame
GPU completion. `diagnostics_get` reads loaded objects without compiling.
Save callbacks enqueue exports; mirror transactions share a process/file lock.

**Getting data out**: `ue_execute` reads default to a one-line summary. Use `ue_read(target=...)` (artifact token for the full body) or `ue_execute(..., response={"mode": "full"})`. `response` accepts only `mode` and `allow_heavy`; `response.format` is invalid — read shape goes into the operation payload (`format`) or `ue_read(format="detail")`. Modes: `silent | brief | ids_only | delta | summary | full | debug`.

| Intent | Call |
|:-------|:-----|
| Asset metadata | `ue_read(target="asset")` / `asset_get` |
| Unknown asset type, fuzzy name | `ue_read(target="auto", asset_path="terrain_demo", format="detail")` |
| Material / MaterialFunction / Blueprint graph | `ue_read(target="graph")` / `graph_snapshot_get(graph_kind=...)` |
| Graph params | `graph_snapshot_get(format="full", include_node_params=true)`; whole-graph `node_params_format="full"` needs `response.allow_heavy=true` |
| Blueprint drill-down | `graph_snapshot_get` / `graph_node_info_get` with `keyword`, `trace_from` + `trace_depth`, `node_class_filter`, `exec_only`; responses list `available_graphs` |
| Blueprint variables / defaults / components | `blueprint_details_get(include_components=true, include_inherited_components=true)` |
| Material static lint (no compile) | `material_lint` |
| Material Instance params / parent chain | `ue_read(target="material_instance")` / `material_interface_resolve` |
| AnimBP, state machines, montages, blend spaces | `anim_blueprint_summary_get`, `anim_state_machine_summary_get`, `anim_montage_summary_get`, `blend_space_summary_get` |
| Niagara / Cascade | `ue_read(target="niagara_system")`, `target="niagara_stack"`, `target="cascade_system"` |
| Level actors, component materials | `level_actors_list`, `level_actor_get`, `object_properties_get` (any actor / component property, `format="full"` for types), `component_*` |
| Dependencies / referencers | `asset_dependencies_get` / `asset_referencers_get` → rows of `[package, hard-or-soft]` |
| Sound cue, texture | `sound_cue_summary_get`, `texture_summary_get` |
| Project diagnostics | `diagnostics_get` → `data.error_count / warning_count / items`; `related_log_items` are historical (`stale_possible=true`) |
| UE log tail | `log_tail_get(tail_kb, match, max_lines)` |

Gotchas: there is no `ue_read(target="material")`; `asset_list(format="indexed")` has no row items (use `compact` or `full`); `graph_snapshot_get` does not descend AnimBP state machines; a `*_patch` / `*_set` op never reads.

## 6. Writing outside the mirror

Sequence: `ue_capability_get(op, detail="schema")` → minimal payload → `ue_plan_validate` for high-risk or batch → `ue_execute` (`delta`) → verify with `ue_diff_get` or the narrowest read. Keep to typed primitives: no scenario templates, no arbitrary Python or console commands, no generic reflection writes.

**graph_patch_apply** — `op`: `connect_pins | disconnect_pins | set_node_param | create_node | delete_node | set_node_position`. Pins by name (`from_pin` / `to_pin`) or GUID (`from_pin_id` / `to_pin_id`). `node_class` accepts `/Script/Engine.MaterialExpressionX`, `MaterialExpressionX` or `X`.

```json
{"op": "create_node", "client_id": "branch", "node_class": "Branch", "position": {"x": 300, "y": 0}}
{"op": "connect_pins", "from_node_id": "<GUID>", "from_pin": "Then", "to_node_id": "branch", "to_pin": "execute"}
{"op": "set_node_param", "node_id": "<GUID>", "name": "PinName", "value": "string_or_number"}
```

Blueprint `create_node.client_id` works in dry run; for Material / MaterialFunction the Python layer expands same-batch `client_id` links only when `dry_run=false`.

**blueprint_components_patch** — `op`: `add_component | remove_component | set_component_defaults | set_component_properties`; `defaults` and `properties` are aliases, plus `RelativeTransform` and `material` conveniences.

```json
{"op": "add_component", "component_class": "/Script/Niagara.NiagaraComponent", "name": "AimVFX", "parent": "Mesh", "defaults": {"bAutoActivate": false}}
```

**node_params_set** — `params` is an object `{"PinName": value}`; discover exact names with `node_params_get`. Material root properties (`ShadingModel`, `BlendMode`, `TwoSided`, ...) live on `node_id="MaterialOutput"` — or, better, in the mirror's `[asset]` section.

**project_input_mappings_patch** — `add_action_mapping | remove_action_mapping | add_axis_mapping | remove_axis_mapping`. Enhanced Input: `input_action_create`, `input_mapping_context_create`, `input_mapping_context_entry_add`.

**landscape_layer_info_set** — binds or creates `LandscapeLayerInfoObject` per paint layer; `name` must match the material layer exactly.

**Level actors** — `level_actor_spawn` (`class_path` = engine short name, `/Script/` path or Blueprint asset), `level_actor_delete`, `level_actor_transform_set` (at least one of location / rotation / scale). `level_open` refuses a dirty map unless `discard_changes=true`; every actor path read before it is stale afterwards.

**level_actor_properties_set** — any editable property on a placed actor or one of its components, by dotted path. Values: JSON primitives, `{x,y,z}` / `{pitch,yaw,roll}` / `{r,g,b,a}` objects, object paths for references, or a raw UE ExportText string for anything else (enum names such as `AEM_Manual` are strings). All paths are validated before the first write; one bad path fails the call with `invalid_property` and lists it. Use `object_properties_get(format="full")` to discover names. This is the way to set exposure, fog, light or camera settings on a level actor — not `node_params_set`.

```json
{"actor_path": "/Game/Maps/L.L:PersistentLevel.PostProcessVolume_0", "dry_run": false,
 "properties": {"bUnbound": true, "Settings.bOverride_AutoExposureMethod": true, "Settings.AutoExposureMethod": "AEM_Manual",
                "Settings.bOverride_AutoExposureBias": true, "Settings.AutoExposureBias": 0}}
{"actor_path": ".../DirectionalLight_1", "component": "LightComponent0", "properties": {"Intensity": 3.0}, "dry_run": false}
```

## 7. Editor-safety contract

What the bridge guarantees, so you do not need sleeps or manual-save workarounds:

- No bridge call opens a modal dialog. Every save (`asset_save`, `editor_save_all`, `save=true` on create / move / duplicate / Niagara ops, `ue_sync push`) is a direct `UPackage::SavePackage`. A file that is read-only on disk (checked in under source control) returns `save_blocked_read_only` with the path — check it out in the editor and retry. `asset_save` is safe to call.
- Material graph writes cancel that material's in-flight shader compilation before touching the graph and compile once at the end. One patch per asset, not many small ones.
- An unknown node class is an `unknown_node_class` diagnostic, never a silently dangling node.
- Material output pins include `WorldPositionOffset`, `ClearCoat`, `ClearCoatRoughness`, `SurfaceThickness`, `FrontMaterial`.
- The editor refuses to nest a request inside a running one (`bridge_busy`); the server retries for about two seconds before surfacing it.
- Bridge readback is editor memory, not disk. `ue_sync("status")` compares against the saved file (`ue-modified`, dirty flag) when on-disk state matters.
- Only touched packages are saved; the bridge never runs SaveAll.

## 8. Concurrency and batching

Every request runs on the UE game thread, one at a time per editor; parallel MCP calls queue.

| Class | Ops | Rule |
|:------|:----|:-----|
| Parallel-safe | all reads | overlap freely |
| Per-asset safe | writes on different assets | never two in-flight writes on one asset |
| Sequential only | `bridge_instance_select`, `auto_index_rebuild`, deletes / moves / renames of paths other calls use, editor-lifecycle ops | run alone, re-read afterwards |

- `batch_execute` runs up to 20 ordered operations in one call: validated as a whole first, stops at the first failure unless `continue_on_error=true`, not a transaction. Prefer one `graph_patch_apply` or one `ue_sync push` when it covers the edit.
- `task_submit` queues a long call on one background worker and returns `task_id`; poll `task_status`, fetch `task_result`, `task_cancel` while queued. Task ids die with the server; `task_*` cannot nest.
- Long compiles: raise `UE_NEXUS_TIMEOUT_SECONDS` rather than splitting the work.

## 9. Editor instances

Transport is one Windows named pipe per editor: `\\.\pipe\UeNodeNexusBridge.<pid>`.

- One editor live → bound automatically.
- Several → calls fail until `ue_execute("bridge_instance_list", {})` then `ue_execute("bridge_instance_select", {"pid": ...})` or `{"project": "<substring>"}` (both MCP-local).
- `ue_context_get` shows `active_instance` and `available_instances`. An auto-bound session survives an editor restart; an explicit selection that exits errors until re-selected.
- "no UE editor instance found" → editor closed or plugin not loaded; `bridge_capabilities_get` says whether the VFX module is present.

## 10. Diagnosis playbooks

**Empty asset list**: never conclude "no project" from one empty result. Check `project_context_get` (path, `/Game` mount), `asset_list(package_paths=["/Game"], format="compact")`, `auto_index_status`, `diagnostics_get`. Context empty → wrong instance; assets present but index empty → AutoIndex lifecycle; both empty with valid context → registry scan / mount / filter.

**Error codes worth recognising**

| Code | Meaning | Do |
|:-----|:--------|:---|
| `invalid_request` | required field missing, or a read op called with no target | read the schema |
| `target_conflict` | more than one target given (e.g. `material_interface_resolve`) | pass exactly one |
| `invalid_response_field` | `response.format` or another unknown response key | use `response.mode` |
| `bridge_busy` | request arrived while another one was executing | retried automatically; if it persists, wait |
| `save_blocked_read_only` | file checked in / read-only | check out in the editor, retry |
| `schema_stale` | mirror text written against an older schema key | `ue_sync("schema")` |
| `both-modified` (status) | text and editor both changed | pull first, or push with `force="local"` |
| `mcp_bridge_error` | pipe / transport failure | editor gone or plugin unloaded; re-probe |

## 11. Evidence order

Live op result > bridge diagnostics > project context and mounts > AssetRegistry > AutoIndex state > repo source and README > historical notes.
