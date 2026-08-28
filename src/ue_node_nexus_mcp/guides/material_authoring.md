# Material authoring

## Creating and inspecting

- `asset_create` supports Material, Material Instance Constant, and Blueprint
  assets (dry-run first).
- Material / MaterialFunction node graphs are read with
  `ue_read(target="graph")` / `graph_snapshot_get(graph_kind="material")`.
  There is no `ue_read(target="material")`: use `graph` for node graphs,
  `material_instance` for instance parameters, `auto` for unknown asset types.
- `material_expression_classes_list` enumerates loaded `UMaterialExpression`
  classes with editable-property schema counts when you need to pick a node
  class.
- `material_interface_resolve` walks an instance's parent chain to the root
  material; give it exactly one of `asset_path` / `material_path` /
  `component_path`+`slot_index`.

## Editing node parameters

- `node_class_params_get(node_class=...)` shows the editable template for a
  node type; `node_params_get` reads one node; `node_params_set` writes
  (`params` is a `{"pin_name": value}` object, not an array).
- Values keep UE semantics: enums, booleans, object references, and empty
  strings round-trip.

## Material Instances

- `material_instance_params_get` / `material_instance_params_set` cover scalar,
  vector, texture, and static-switch parameters with type validation.
- On components, use `component_material_instance_params_get/set` for
  MID/MI parameters on a specific material slot.

## Lint before compiling

`material_lint` is a Python-local read: it does not compile. It reads the graph
and texture summaries to flag sampler/compression/sRGB mismatches and
placeholder textures. Run it after wiring textures and before `asset_compile`.

## Compile / save loop

1. `asset_compile` — returns structured diagnostics; expect `0 errors`.
2. `asset_save` — reports dirty/read-only/editor-conflict state.
3. Concrete-asset write responses may carry root-level `remaining_errors` for
   that asset; project-wide counts come from `diagnostics_get` instead.
