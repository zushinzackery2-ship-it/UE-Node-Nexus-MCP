# Niagara authoring

## Availability gate

The `vfx` group is served by the separate `UeNodeNexusVfxBridge` plugin and is
only usable when the UE side reports it: check
`bridge_capabilities_get().data.modules.vfx_available`. Local configuration can
disable VFX or express intent, but cannot force it on when the plugin or the
Niagara plugin is not loaded. If a probe is inconclusive (editor not connected)
the result is not cached — reconnect and retry.

## Authoring loop

1. **System**: `niagara_system_create` (empty) or `niagara_system_duplicate`;
   `niagara_system_summary_get` for a compact overview.
2. **Emitters**: `niagara_emitter_create` (`default` / `empty` / `from_asset`),
   `niagara_emitters_list`; per-emitter properties through
   `niagara_emitter_properties_get/set` (enabled, local space, determinism,
   random seed, ...).
3. **Module stack**: `niagara_modules_list` per emitter and usage;
   `niagara_module_add` (existing module scripts only), `niagara_module_remove`
   (keeps parameter-map links intact), `niagara_module_set_enabled`;
   inputs through `niagara_module_inputs_get/set`.
4. **Renderers**: `niagara_renderer_create` (Sprite/Ribbon/Mesh/Light/
   Component/Decal/Volume), `niagara_renderers_list`,
   `niagara_renderer_properties_get/set`; materials through
   `niagara_materials_get/set`.
5. **User parameters**: edit the mirror's `[user]` section
   (`Speed : float = 3`); the push verbs are `ns_user_param_add`,
   `ns_user_param_set` and `ns_user_param_remove`. Types are
   `float int bool Vector2 Vector Vector4 Color Position Quat`, or a class name.

## What the text mirror covers, and the reflected-property escape hatch

The mirror exports Niagara by reflection, so it is the primary surface:

- `[asset]` - editable UObject properties of the system (`set_asset_prop`).
- `[emitter Name]` - editable `FVersionedNiagaraEmitterData` properties
  (`ns_emitter_prop_set`), plus `Parent`/`enabled`.
- `[renderers Name]` - editable renderer properties with attribute bindings
  removed (`ns_renderer_add/remove/set_prop`).
- `[user]` - the exposed parameter store (`ns_user_param_*`).
- `[stack Name/Group]` - module stack lines and their rapid-iteration inputs.

Four hidden operations remain deliberately available outside the mirror, because
their property predicate is wider than the mirror's `IsEditableProperty`
(`CPF_Edit && !EditConst && !Deprecated`). They also accept BlueprintVisible-only
and Edit+EditConst properties, and `allow_non_editable=true` widens that further:

- `niagara_system_properties_get/set`
- `niagara_emitter_properties_get/set`
- `niagara_renderer_properties_get/set`

Reach for them only when the mirror's editable filter excludes the property you
need; the default mirror path stays the reviewable one. `niagara_materials_get/set`
also stays: it resolves the material slot per renderer type (mesh renderers use a
nested array), which has no first-class text form.

User parameters are not in that category. They are parameter-store entries with a
first-class `[user]` form, so no separate operation exists.

## Validation

- `niagara_asset_lint` is a no-write risk check for authoring mistakes; the
  `indexed` format returns compact `G/S/C/I` lines, `full` returns issue
  objects.
- `niagara_compile` requests compilation and reports
  `ready` / `needs_compile` / `readiness_issue_count`.
- Save through the regular `asset_save`.

## Response sizes

Niagara reads default to `indexed`/summary shapes and trim verbose data fields
on the Python side. Ask for `format="full"` only when you need complete
objects; large envelopes come back as artifact handles.

## Cascade

Cascade (`UParticleSystem`) is read-only: `cascade_system_summary_get` returns
emitters, type data, and module stacks — the migration-read entry point for
legacy particles.
