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
5. **User parameters**: `niagara_user_params_get/set`
   (float/int/bool/vector/color/material).

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
