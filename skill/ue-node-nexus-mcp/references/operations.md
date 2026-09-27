# Narrow operations and response handling

Use this reference for operations outside normal `.nexus` publication. Read the
selected operation's schema for exact payloads; the in-band workflow guide owns
the complete catalog.

## Reads

| Intent | Operation |
|---|---|
| Asset metadata or fuzzy name | `ue_read(target="asset")`, `target="auto"` |
| Material, MF or Blueprint graph | `ue_read(target="graph")`, `graph_snapshot_get` |
| Node values and pin names | `node_params_get`, `graph_node_info_get` |
| Blueprint members and inherited components | `blueprint_details_get` |
| MI parameters and parent chain | `ue_read(target="material_instance")`, `material_interface_resolve` |
| Niagara/Cascade | `ue_read(target="niagara_system")`, `niagara_stack`, `cascade_system` |
| Level actors and component properties | `level_actors_list`, `level_actor_get`, `object_properties_get` |
| Dependencies and referencers | `asset_dependencies_get`, `asset_referencers_get` |
| AnimBP state machines | `anim_blueprint_summary_get`, `anim_state_machine_summary_get` |
| Diagnostics and UE logs | `diagnostics_get`, `log_tail_get` |
| Viewport preview | `viewport_camera_get/set`, `viewport_capture` |

Use `asset_list(format="compact"/"full")` for rows; indexed results carry index
metadata. Graph snapshots list available graphs and support scoped node filters.
Animation state machines use their own read operations.

`ue_execute` reads default to a summary. Set `response=dict(mode="full")` or use
`ue_read` when the full result is needed. `response` accepts `mode` and
`allow_heavy`; read format belongs in the operation payload or `ue_read(format=...)`.
Modes are `silent`, `brief`, `ids_only`, `delta`, `summary`, `full`, `debug`.
Whole-graph full node parameters require `allow_heavy=True`.

Large results return `next_read` and `page_lists_with`. Follow the call for the
list being inspected. Artifact pages default to 48 KiB, with a 4 MiB maximum.
List pages contain complete items and `next_cursor`; an `oversized_item` can be
read under `path.<index>`. For `encoding="json-text"`, concatenate all chunks in
cursor order before parsing. Historical log diagnostics carry `stale_possible`.

## Graph and scene writes

For a graph patch, build one ordered `graph_patch_apply` per asset. New nodes get
a `client_id` usable by later patch entries, including dry runs. Forward or
duplicate client ids are rejected. Use known pin names or query the node after
dynamic configuration. Supported verbs include create/delete, connect/disconnect,
parameter edits and positions. Root material properties use `MaterialOutput`.

`blueprint_components_patch` accepts add/remove and component default/property
edits. `node_params_set` writes named graph parameters. Asset create/move/rename/
duplicate/delete and folder operations handle asset management. Source-controlled
read-only saves report the blocked path.

Placed actor/component properties use `level_actor_properties_set`, discovered
through `object_properties_get(format="full")`. Values may be primitives, typed
vectors/rotators/colors, object paths or UE ExportText. `level_open` requires an
explicit discard decision for a dirty map; actor paths must be read again after
the map changes. Landscape layer names must match the material paint layer.

For `.scene.nexus`, first pull supplies `scene.map_path`, `name`, and
`actor_paths`. Root actor transforms are world space; attached actors, components
and instances use local space. Loaded hidden levels are included. Construction
script instance arrays remain read-only; ambiguous instance identity requires
an explicit `pull(force="ue")`. Load the scene guide for transaction boundaries.

## Batching and timeouts

UE processes requests on its game thread. Keep writes on one asset ordered.
`batch_execute` validates up to 20 ordered operations and normally stops on the
first failure; the batch itself is not a transaction. `task_submit` queues one
long operation and returns a task id for status/result/cancellation while queued.
Task ids belong to the current MCP process.

Task submission, whole batches and push/recover automatically reserve their
original project and instance. Later selection cannot redirect accepted work.
Lifecycle operations run directly rather than inside batches or tasks. For long
compiles, configure `UE_NEXUS_TIMEOUT_SECONDS` for the expected work.
