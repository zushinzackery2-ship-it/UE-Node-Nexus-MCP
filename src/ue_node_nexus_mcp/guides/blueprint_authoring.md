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

## Pin names

The text and the canonical export both use the engine's own pin names. Where
the editor builds a name an author cannot guess, the mirror registers a stable
alias that resolves on both sides of a comparison:

- Array Get Item (`K2Node_GetArrayItem`) builds `Array` / `Dimension 1` /
  `Output`; the spellings `TargetArray` / `Index` / `Item` are accepted for
  them. The engine name stays reachable in both directions.

Write whichever spelling reads better: lint, diff and the editor canonicalize
to the engine name, and a `pin_not_found` failure lists the node's real pins
with its aliases.

## Multicast dispatchers

Declare a dispatcher and use its native delegate nodes in the text mirror:

```nexus
[dispatchers]
Changed(Value: int)

[graph EventGraph]
listener : CustomEvent(OnValue, Value: int)
bind : AddDelegate(self.Changed)
emit : CallDelegate(self.Changed)
remove : RemoveDelegate(self.Changed)
clear : ClearDelegate(self.Changed)
listener.OutputDelegate -> bind.Delegate
listener.OutputDelegate -> remove.Delegate
```

Wire the execution pins to the events that bind, emit, remove or clear the
dispatcher. `CallDelegate` takes the dispatcher's declared input parameters;
binding events must match their types and order. Parameter names may differ.

For another object, use its class and dispatcher name, for example
`AddDelegate(/Game/Example/BP_Emitter.BP_Emitter_C.Changed)`, and wire the
emitter object to the node's `self` pin. Native class aliases are accepted.
External calls require a Blueprint-callable multicast property; external
binding/removal/clearing require a Blueprint-assignable property.

`graph_patch_apply` uses `delegate_owner` and `delegate_name` in node config.
The four node classes are `K2Node_CallDelegate`, `K2Node_AddDelegate`,
`K2Node_RemoveDelegate` and `K2Node_ClearDelegate`. Binding is configured from
the reflected multicast property before pins are allocated. Preview validates
the same reference without creating nodes or mutating node defaults; it also
accepts a dispatcher declared earlier in the same plan. The schema's
`delegates.json` family records owner, signature and access flags.

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
