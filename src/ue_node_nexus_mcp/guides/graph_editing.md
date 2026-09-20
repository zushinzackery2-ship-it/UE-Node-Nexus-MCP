# Editing Material and Blueprint graphs

## Read before you write

- Topology: `graph_snapshot_get` (default `wires_tiny` — node dictionary, edge
  list, type counts). Blueprint responses include `available_graphs` so you can
  pick `graph_name` instead of guessing.
- Whole-graph node info: `graph_node_info_get` (default `indexed`, one call for
  nodes + params + wires; `include_position=true` adds coordinates).
- Single node: `node_info_get`; editable params: `node_params_get`.
- Big Blueprint graphs: narrow with `keyword`, `node_class_filter`,
  `trace_from` + `trace_depth`, or `exec_only=true` on both snapshot and
  node-info reads. Check `filter_stats` in the response.

Heavy whole-graph schema reads (`format="full"` + `include_node_params=true` +
`node_params_format="full"`) are blocked unless `response.allow_heavy=true`;
prefer `wires_tiny` plus targeted `node_params_get` calls.

## Patch loop

`graph_patch_apply` takes `operations: [{"op": ...}]` with ops `connect_pins`,
`disconnect_pins`, `set_node_param`, `create_node`, `delete_node`,
`set_node_position`. Pins accept a GUID (`from_pin_id`) or a name (`from_pin`);
names are easier, GUIDs are unambiguous for duplicate pin names.

- Same-batch node references: `create_node` with `client_id`, then use that
  `client_id` in later `connect_pins` / `set_node_param` entries.
  Blueprint patches support this in dry-run too; Material / MaterialFunction
  Material and MaterialFunction patches resolve `client_id` in the native
  ordered patch context. Dry-run uses a transient graph and follows the same
  operation order as the real write.
- Always run once with `dry_run=true` and read the reported diff and pin
  integrity before applying.
- After applying, the response carries applied changes, compile state, and
  dirty flags; follow up with `asset_compile` / `asset_save` when done.

## Bulk graph construction

`graph_build_apply` creates nodes, sets params, and wires links in one call:
`nodes` (`id`, `node_class`, `x/y`, `params`) plus `links` using the
`node.pin -> node.pin` shorthand (e.g. `base_color.RGB -> MaterialOutput.BaseColor`).
Vector outputs accept `RGB/RGBA/R/G/B/A/...` aliases; invalid pins return the
list of valid output candidates.

## Verify

Re-read with the same snapshot format you started from and compare, or use
`ue_diff_get(since_token=...)` for the compact change list. Compile with
`asset_compile` and confirm `0 errors` before saving.
