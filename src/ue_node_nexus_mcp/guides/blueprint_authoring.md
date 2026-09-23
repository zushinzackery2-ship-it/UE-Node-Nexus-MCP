# Blueprint authoring

## Inspecting a Blueprint

- `blueprint_details_get` — class metadata and variables by default; add
  `include_components=true` (and `include_inherited_components=true` to walk the
  parent chain, entries tagged with `origin`) or CDO defaults explicitly.
- Event/function graphs are read through the generic graph ops with
  `graph_kind="blueprint"`; responses list `available_graphs` (name + node
  count) so you can target the right graph.
- AnimBlueprints have dedicated compact reads: `anim_blueprint_summary_get`
  for AnimGraph nodes, `anim_state_machine_summary_get` for state machines
  (the generic snapshot does not descend into state-machine sub-graphs), and
  `anim_montage_summary_get` / `blend_space_summary_get` for their assets.
- State machines are the one AnimBlueprint surface with no text-mirror form:
  add states with `anim_state_machine_state_add` and wire them with
  `anim_state_machine_transition_add`. Everything else in an AnimBlueprint
  follows the mirror and generic graph rules.

## Component tree (SCS) writes

`blueprint_components_patch` with ops `add_component`, `remove_component`,
`set_component_defaults`, `set_component_properties`. `defaults` accepts
reflected UE property names plus the `RelativeTransform` and `material`
conveniences. Widget Blueprints have no SCS tree — expect
`blueprint_scs_unavailable`, not an error in your payload.

## Event graph edits

Use `graph_patch_apply(graph_kind="blueprint")`:

- `create_node` accepts short class names (`Branch`, `K2Node_CallFunction`) or
  full paths; typed nodes take `function_name`/`function_owner`,
  `variable_name`, `event_name`, `input_key`, `input_action_name`, `axis_name`.
- A generic `K2Node_Event` without `function_name`/`function_owner` is rejected
  (`node_config_required`) instead of creating a meaningless event node.
- `K2Node_CallFunction` creates the class the editor spawns for that function:
  `K2Node_CallArrayFunction` for array functions (`Array_Clear`, `Array_AddUnique`),
  which is what types their wildcard pins, and the data-table / collection call
  classes likewise. `class_path` in the result is the class created; a
  specialised class that does not match the function is refused, dry run included.
- Same-batch `client_id` references work for Blueprints, including dry-run.

## Input wiring

Legacy Project Settings mappings are read/written with
`project_input_mappings_get` / `project_input_mappings_patch`
(`add_action_mapping`, `add_axis_mapping`, ...; can persist to config). Pair
them with `K2Node_InputAction` / `K2Node_InputAxisEvent` nodes when building
input chains.

## Verify

`asset_compile` after structural edits — Blueprint compile diagnostics are
structured, and a clean build is `0 errors / 0 warnings`. Read back with
`blueprint_details_get` or a scoped graph snapshot rather than assuming.
