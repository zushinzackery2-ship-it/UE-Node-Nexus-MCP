---
name: ue-node-nexus-mcp
description: Operate the UE Node Nexus MCP bridge to inspect and edit a running Unreal Editor — assets, Material/Blueprint graphs, Niagara, levels, diagnostics — and to author assets through the Content_Transcoded text mirror (ue_sync pull/push of .nexus files). Use when the user mentions UE Node Nexus MCP, UEMCP Bridge, ue-node-nexus-mcp, ue_sync, .nexus files, Content_Transcoded, L2U/U2L, ue_execute, ue_read, ue_diff_get, ue_capability_get, auto_index, project_context_get, empty asset lists, or any MCP-to-Unreal bridge question.
---

# UE Node Nexus MCP

Seven MCP tools share a project editor across agent workspaces. The editor stays the compiler; the user-level manager owns startup, usage and guarded cleanup.

**Ground rules**

1. Live results beat everything: a fresh tool result outranks source comments, README text and this file.
2. Never guess a payload. `ue_capability_get(operation, detail="schema")` first, `detail="examples"` for a valid body.
3. Author assets through the text mirror (`ue_sync`); use graph ops only for what the mirror marks `@opaque`, for levels, and for asset management.
4. Writes default to `dry_run=true`. Read the plan, then apply.
5. Mirror text is edited with your own file tools (Read / grep / StrReplace) and validated with `ue_sync("lint")`. Do not generate or patch `.nexus` files with a script, do not parse them with the project's Python modules, and never write conflict markers into them — a `.nexus` suffix inside the mirror root is treated as a live mirror file.

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

- The operation registry sits behind `ue_execute`. Hidden operations remain callable by name; discover them with `include_hidden=true`. These include mirror transport, narrow graph extensions, index maintenance and legacy editor operations.
- Task recipes are served in-band: `ue_execute("workflow_guide_get", {})` lists categories, including `instances`, `getting_started`, `text_mirror`, `scene_mirror`, `collaboration` and authoring/diagnostic guides. `{"category": ...}` returns one guide; `{"query": "connect pins"}` routes by keyword. `ue_context_get` returns the same category list.
- If an operation named here is missing from `ue_capability_get`, the MCP server process runs stale code: reinstall the Python package and restart the client. Plugin (editor) and server must both be current.

## 2. First probe

Run this before answering any "is the bridge working / what project is this" question:

Configure an exact `.uproject`, then follow section 9 to ensure/reuse it. Querying context alone never starts UE. Poll a STARTING instance before making data calls.

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
| Create / delete / move / rename / duplicate assets, folders, fix redirectors | `asset_create`, `asset_delete`, `asset_move`, `asset_rename`, `asset_move_batch`, `asset_rename_batch`, `asset_duplicate`, `folder_create`, `folder_delete`, `asset_redirectors_fixup` |
| Blueprint that is not a plain actor Blueprint | `asset_create(asset_kind="blueprint", blueprint_type=...)` — `normal \| const \| macro_library \| interface \| function_library`. Leave it unset to take what the parent implies (`BlueprintFunctionLibrary` → function library, an `Interface` parent → interface). A macro library has no distinguishing parent, so it must be named |
| Asset dependencies / referencers, find who uses a material | `asset_dependencies_get`, `asset_referencers_get`, `material_usage_find` |
| Level actors, components, material slots, landscape layer info | `level_*`, `component_*`, `landscape_layer_info_set` |
| Read or set a level actor, its transform, component materials, or its instances without a scene group | `level_actor_get`, `level_actor_transform_get`, `level_mesh_instances_list`, `material_usage_find`, `component_materials_get`, `component_materials_set`, `component_material_instance_params_get/set` |
| AnimBlueprint state machines, montages, blend spaces | `anim_blueprint_summary_get`, `anim_state_machine_summary_get`, `anim_montage_summary_get`, `blend_space_summary_get`; add states and transitions with `anim_state_machine_state_add`, `anim_state_machine_transition_add` (neither the mirror nor the graph snapshot descends into state machines) |
| Persistent asset index | `auto_index_enable` (setup), `auto_index_status`, `auto_index_overview`, `auto_index_rebuild`, `auto_index_query`, `auto_index_resolve_path`, `auto_index_get`, `auto_index_tree_get` |
| Loaded Actor/Blueprint/ISM/HISM scene groups | `ue_sync` with `.scene.nexus`; load `workflow_guide_get(category="scene_mirror")` |
| Paged instance reads and batch edits | `component_instances_get/patch`; use the read revision and persistent IDs |
| Compile, validate, save one asset | `asset_compile`, `asset_validate`, `asset_save` |
| Project error list, UE log lines | `diagnostics_get`, `log_tail_get` |
| Screenshot | `viewport_capture` (level viewport, drawn synchronously; `data.exists`, absolute `file_path`); `viewport_capture_status` only if `exists=false` |
| Look at something / second camera angle | `viewport_camera_get`, `viewport_camera_set` (`location` + `look_at` or `rotation`, `fov`) then `viewport_capture` |

## 4. Text mirror (`ue_sync`)

**Collaboration workflow**: load `workflow_guide_get(category="collaboration")`.
Each agent creates its own checkout with `dry_run=False`, then edits the
returned `files_root`/`file_paths`. Pass the returned `id` as `workspace_id`.
Stage and commit locally; push merges committed HEAD with current UE memory.
Keep later file edits local.
Use branch/tag, log/show/diff/blame, restore/revert, private reset/rebase/amend,
cherry-pick, stash and reflog for version operations. Mutations default to
preview; `proposal_id` binds the preview when provided. A preview answers
`status="preview"`, `applied=0` and `execute={...}`; its `action` is `preview`
with the completed-form verb under `preview_of`, so a preview never reads as a
finished write.

**Resolving conflicts**: `resolve` takes one decision and the set it applies to —
`conflict_id`, `conflict_ids: [...]`, `asset` (glob), `conflict_type`, `layer`, or
`all: true` — and replays the three layers once for the whole batch. It answers
`resolved` and `resolved_count`. `choice="custom"`/`"rename"` name one conflict at
a time. A selection that matches nothing is refused with the open conflict count,
types and layers, so you can widen it without re-reading the session.

**Recovering an interrupted publication**: `recover` with no `apply_id` clears
every pending transaction the repository owns, including one whose workspace was
closed. `resolution` is `inspect` (read the durable receipt, publish one UE
already committed), `restore` (replay the package checkpoint, the default) or
`abandon` (keep current editor memory and end the record). `abort {apply_id}`
gives up on one transaction without asking UE. A publication blocked by pending
work returns the pending rows plus the exact `recover` / `abort` calls that clear
them, so there is always a next call.

Schema is the parameter catalog: `schema(category=..., query=..., details=True)`
through `options` reads classified JSON also used by lint. Function and target
context queries supplement dynamic pins and inherited parameters. Read the
returned `schema_path` index first and respect support flags. A coverage gap
carries its own resolution: `coverage.blueprint_pins` answers
`{state: "context_required", reason, resolve_with}`, and `resolve_with` is the
`schema(target="<Blueprint asset path>")` call that returns the real pins under
`context.definition.blueprint.graphs[].nodes[].pins[]` — a Blueprint node's pins
come from the owning asset, never from the node class.
Offline/historical schema describes its bound version, and current publication
still checks the editor environment.

First checkout imports legacy baselines and preserves existing local text in
the `imported` workspace. The following legacy loop applies before activation;
after activation, use workspace IDs, local commits and conflict resolutions.

Layout: `<shared mirror_root returned by ensure>/<Project>/<Path>/<Asset>.<kind>.nexus` with kinds `mat mf mi bp ns ne asset stub`; `.nexus/base/` is the merge base, `.nexus/schema/<key>/` the reflection lock. Existing UE repository bindings take precedence; new projects default to `<Project>/Saved/Nexus/Content_Transcoded`.

**Loop**

1. `ue_sync("init")` once per mirror root (binds root, exports schema, pulls everything).
2. `ue_sync("status")` — per-asset state: `clean`, `local-modified`, `ue-modified`, `both-modified`, `local-new`, `ue-new`, `local-deleted`, `ue-deleted`.
3. Edit the `.nexus` file with Read / grep / StrReplace. Only non-default values are written; delete a line to reset.
4. `ue_sync("lint", paths)` — offline: unknown class / property / enum / pin / type, dangling links, opaque edits. `options={"files_root": "<dir>"}` lints a plain directory of `.nexus` files that no workspace owns (a hand-prepared or handed-over tree), against the cached schema.
5. `ue_sync("push", paths)` — dry run returns the plan (verb counts, risky verbs).
6. `ue_sync("push", paths, options={"dry_run": false})` — one editor transaction per asset, compile, save touched packages only, re-export, rewrite text to canonical form. Diagnostics come back as `file:line: message`. A rolled back apply still carries the compiler messages that explain it under `details.diagnostics` (they name the node), so read those rather than re-running the push to see what broke.
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

- Blueprint: `[variables] Health : float = 100 { Category=Stats, InstanceEditable }`, `[components] Mesh : StaticMeshComponent(parent=Root) { RelativeLocation=(X=0,Y=0,Z=50) }`, `[graph EventGraph]` nodes such as `Event(Actor.ReceiveBeginPlay)`, `CallFunction(KismetSystemLibrary.PrintString, InString="Hi")`, `VariableGet(Health)`, `Sequence(pins=3)`; exec pins are `execute` / `then`; `[function Name(A: double) -> (R: bool)]` has implicit `entry` / `result`; `@renamed(Old)` renames.
- Blueprint `[asset]` carries `ParentClass` and `BlueprintType` (`normal | const | macro_library | interface | function_library`). Both are honoured only when the asset is created and are verified, never changed, on an existing one — a macro library is indistinguishable from an actor Blueprint without the type, so a create needs it.
- A `DynamicCast` result pin is written as `AsResult`, not `As<TargetType>`: the engine names that pin after the target class's **localized** display name, which would make the same `.nexus` unusable on an editor in another language. The bridge maps the alias back on apply.
- An execution output drives exactly one place; any number of execution lines may join at one execution input, and a data input takes one source while a data output fans out. `pin_cardinality` means a link violated the side that is actually constrained.
- Niagara: `[emitter Name]`, `[stack Name/ParticleUpdate]` lines like `spawn_rate : SpawnRate(SpawnRate=25, Spawn Probability=0.5) !disabled` list what the Stack panel shows; `[renderers Name] sprite : Sprite { SubImageSize=(X=2,Y=2) }`; `[user] Speed : float = 3` with types `float int bool Vector2 Vector Vector4 Color Position Quat` or a class name. `@link(...)` / `@dynamic` inputs are read-only; `SetVariables(...)` can be edited, not created.
- Ids are yours and stable; GUIDs never appear. Renaming an id recreates the node. A link to a new multi-pin node must name the pin.
- A struct property prints every member, including members whose value is zero. A property the engine never reflected (a plain C++ member with no `UPROPERTY`) cannot be set from text at all, and the error says so instead of failing silently.
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

**Build and readiness:** capability `build` contains Guard, Core and optional VFX identities. Version 0.6.0 requires contract 4 and matching engine BuildIds; Guard and Core also share a source fingerprint. Python validates the loaded contract before writes. Explicit material compilation
reports shader readiness and a target RHI update fence; this is not whole-frame
GPU completion. `diagnostics_get` reads loaded objects without compiling.
Save callbacks enqueue exports; mirror transactions share a process/file lock.

**Getting data out**: `ue_execute` reads default to a one-line summary. Use `ue_read(target=...)` (artifact token for the full body) or `ue_execute(..., response={"mode": "full"})`. `response` accepts only `mode` and `allow_heavy`; `response.format` is invalid — read shape goes into the operation payload (`format`) or `ue_read(format="detail")`. Modes: `silent | brief | ids_only | delta | summary | full | debug`.

**Payload fields are checked, not filtered**: `ue_execute`, `ue_read(query=...)` and `batch_execute` refuse a field the operation does not declare and answer `unknown_field` / `unknown_query_field` with the accepted names. A filter that never applied used to look exactly like a filter that found nothing — `graph` is `graph_name`, and `graph_node_search` takes `filters: {...}`, not a bare `node_class`.

**Reading a large artifact**: `ue_read(target="artifact", query={"artifact_id": ...})` returns the payload inline when it fits the transport frame. Anything larger comes back as `encoding="json-text"` chunks: concatenate every `chunk` in cursor order, then parse. Follow `next_read` (or pass `cursor=next_cursor`) until `next_cursor` is null; `total_bytes` is stable across pages and `limit_bytes` may ask for less, never more.

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

Gotchas: there is no `ue_read(target="material")`; `asset_list(format="indexed")` has no row items (use `compact` or `full`); `graph_snapshot_get` does not descend AnimBP state machines; a `*_patch` / `*_set` op never reads; a `query` key must be a field the backing operation declares — the facade refuses unknown ones instead of dropping them.

## 6. Writing outside the mirror

Sequence: `ue_capability_get(op, detail="schema")` → minimal payload → `ue_plan_validate` for high-risk or batch → `ue_execute` (`delta`) → verify with `ue_diff_get` or the narrowest read. Keep to typed primitives: no scenario templates, no arbitrary Python or console commands, no generic reflection writes.

**graph_patch_apply** — `op`: `connect_pins | disconnect_pins | set_node_param | create_node | delete_node | set_node_position`. Pins by name (`from_pin` / `to_pin`) or GUID (`from_pin_id` / `to_pin_id`). `node_class` accepts `/Script/Engine.MaterialExpressionX`, `MaterialExpressionX` or `X`.

```json
{"op": "create_node", "client_id": "branch", "node_class": "Branch", "position": {"x": 300, "y": 0}}
{"op": "connect_pins", "from_node_id": "<GUID>", "from_pin": "Then", "to_node_id": "branch", "to_pin": "execute"}
{"op": "set_node_param", "node_id": "<GUID>", "name": "PinName", "value": "string_or_number"}
```

Wiring order that survives review: connect exec pins before data pins, read the exact pin names with `node_params_get` / `graph_node_info_get` before any `connect_pins` (dynamic nodes and overloads do not have stable names), set defaults only where no data source exists, and compile after each logical chunk rather than after a whole batch of edits.

`create_node.client_id` works in dry run for Blueprint, Material, and MaterialFunction. Native patch contexts preserve operation order, reject duplicate or forward aliases, and use transient graphs for preview.

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
- Publication saves only touched packages. `editor_save_all` is an explicit exclusive operation; automatic cleanup never saves or discards edits.

## 8. Concurrency and batching

Every request runs on the UE game thread, one at a time per editor; parallel MCP calls queue.

| Class | Ops | Rule |
|:------|:----|:-----|
| Parallel-safe | all reads | overlap freely |
| Per-asset safe | writes on different assets | never two in-flight writes on one asset |
| Sequential only | `bridge_instance_select`, `auto_index_rebuild`, deletes / moves / renames of paths other calls use, editor-lifecycle ops | run alone, re-read afterwards |

- `batch_execute` runs up to 20 ordered operations in one call: validated as a whole first, stops at the first failure unless `continue_on_error=true`, not a transaction. Prefer one `graph_patch_apply` or one `ue_sync push` when it covers the edit.
- `task_submit` queues a long call on one background worker and returns `task_id`; poll `task_status`, fetch `task_result`, `task_cancel` while queued. Task ids die with the server; `task_*` cannot nest.
- A task reserves its project and instance at submission; later selection cannot redirect it. Whole batches and push/recover retain scopes. Lifecycle operations cannot be batched or queued.
- Long compiles: raise `UE_NEXUS_TIMEOUT_SECONDS` rather than splitting the work.

## 9. Editor instances

Load `workflow_guide_get(category="instances")` for schemas, state transitions, policy and upgrades.

- Set `--project` / `UE_NEXUS_PROJECT_PATH`, or pass exact `project_path` to `bridge_instance_ensure`. cwd only supplies candidates; ambiguity needs an exact target.
- Ensure defaults to `mode="reuse_only"`, `dry_run=true`. Preview and apply with `dry_run=false`; choose `reuse_or_start` when starting the project is authorized. Set `engine_path` when EngineAssociation is unresolved.
- STARTING callers join one launch. Poll `bridge_instance_status`; do not open another editor to address startup, busy, unresponsive or repository errors.
- End work with `bridge_instance_release`. Heartbeats and context/status polling do not renew usage. Defaults: idle lease 300 s, exit grace 120 s, max two managed editors and one startup.
- Existing compatible editors remain `external`. Automatic cleanup applies to clean, unused managed editors; interactive editors are protected. Other users, work, dirty packages, PIE, compilation/saving and recovery block close.
- All workspaces use ensure's shared repository; each gets its own checkout. Conflicting `mirror_root` returns `repository_mismatch`; preserve the existing history. Offline lint/stage/commit keep working after UE exits.
- Use list/status/reap to inspect users, scopes, resource samples and blockers. Explicit close uses a preview and named `save_packages`; `EXITED` and its exit code confirm the actual outcome.
- `adopt` transfers ownership only with exact project/instance/process creation identity and a reason. It is protected by default.
- `pin` requires a reason and `seconds` greater than zero, up to `policy.max_pin_seconds` (default 14400). It is renewable — pin again to extend — and accepts a `STARTING` editor, which is the window worth protecting because losing it costs the whole startup. The response echoes `max_pin_seconds`, `state` and `renewable`; a rejected call names the field it rejected, and an unrecognised field is refused rather than ignored.
- Every listed row states how old its evidence is: `snapshot_age_seconds` and `stale`. `list` / `status` refresh a snapshot older than `policy.sweep_seconds` before answering, so a read never reports readiness a write would not accept. A row whose observation aged out drops `ready` and carries `snapshot_stale` instead of claiming a state nobody measured.
- Explicit instance selection stays stale after that process exits. Authorized project-level `reuse_or_start` can restore the same project; accepted tasks retain their original instance.
- A possibly executed write with `operation_outcome_unknown` is not replayed. Query state or use the recorded apply ID for publication recovery. A `manager_response_unknown` ensure can be inspected/retried with the same idempotency key.
- Install matching Wheel/Core/Guard before enabling managed startup. Legacy `editor_request_exit` uses the same ownership/usage checks; its old force/blanket save modes are rejected.

## 10. Diagnosis playbooks

**Empty asset list**: never conclude "no project" from one empty result. Check `project_context_get` (path, `/Game` mount), `asset_list(package_paths=["/Game"], format="compact")`, `auto_index_status`, `diagnostics_get`. Context empty → wrong instance; assets present but index empty → AutoIndex lifecycle; both empty with valid context → registry scan / mount / filter.

**Error codes worth recognising**

| Code | Meaning | Do |
|:-----|:--------|:---|
| `invalid_request` | required field missing, or a read op called with no target | read the schema |
| `target_conflict` | more than one target given (e.g. `material_interface_resolve`) | pass exactly one |
| `invalid_response_field` | `response.format` or another unknown response key | use `response.mode` |
| `unknown_field` / `unknown_query_field` | the payload or `ue_read` query named a field the operation does not declare | read `details.accepted_fields` and rename (`graph` → `graph_name`, bare `node_class` → `filters`) |
| `invalid_pin` | `pin` was called without `seconds`, over `max_pin_seconds`, or with an unrecognised field | the error names `field` and lists the accepted ones |
| `pin_cardinality` | a link occupies a pin UE allows one connection on (execution output, data input) | reroute; joining several execution lines at one execution input is legal |
| `workspace_required` | collaboration is enabled and the action came without `options.workspace_id`; `init` always returns it once collaboration is on | run `checkout` (the error lists existing workspaces), then pass the returned `id` as `workspace_id` |
| `both-modified` (status) | text and editor both changed | pull first, or push with `force="local"` |
| `sync_busy` | another transaction owns the mirror root | wait for it to finish and retry |
| `dependency_not_selected` | a newly referenced asset is not in the same selection | include it and push again |
| `dependency_cycle` | new assets reference each other in a cycle | break the cycle before pushing |
| `recovery_required` | an interrupted apply left a durable receipt | the error lists the pending rows and the `recover` / `abort` calls that clear them; `ue_sync("recover")` with no `apply_id` clears the whole repository |
| `stale_proposal` / `stale_session` / `stale_target` | the input changed since the preview or session was produced | keep your edits, re-observe, then merge and preview again |
| `bridge_busy` | request arrived while another one was executing | retried automatically; if it persists, wait |
| `save_blocked_read_only` | file checked in / read-only | check out in the editor, retry |
| `schema_stale` | mirror text written against an older schema key | `ue_sync("schema")` |
| `mcp_bridge_error` | pipe / transport failure | editor gone or plugin unloaded; re-probe |

## 11. Evidence order

Live op result > bridge diagnostics > project context and mounts > AssetRegistry > AutoIndex state > repo source and README > historical notes.
