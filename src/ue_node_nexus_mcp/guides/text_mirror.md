# Editing assets as text: the Content_Transcoded mirror

UE stays the compiler; `.nexus` text files are the source. Edit them with the
normal file tools, then let `ue_sync` push the diff into the editor in one
transaction. Bulk data never enters the tool result: the UE plugin writes raw
exports straight into the mirror directory.

## Layout

```
<root>/                          UE_NEXUS_TRANSCODE_DIR or <cwd>/Content_Transcoded
  .nexus/schema/<key>/           schema lock (classes, props, defaults, enums, MF signatures)
  <Project>/A/B/M_X.mat.nexus    material        .mf = function   .mi = instance
  <Project>/A/B/BP_X.bp.nexus    blueprint       .ns/.ne = Niagara system/emitter
  <Project>/A/B/DA_X.asset.nexus property-bag asset (DataAsset, InputAction, IMC, ...)
  <Project>/A/B/T_X.stub.nexus   read-only AssetRegistry tags (textures, meshes, ...)
  <Project>/.nexus/base/         last synced raw export (ids, opaque nodes) = merge base
  <Project>/.nexus/pending/      raw exports and *.push.json recovery records
```

## Loop

1. `ue_sync("status")` — three-way state per asset: `clean`, `local-modified`,
   `ue-modified`, `both-modified` (conflict), `local-new`, `ue-new`, `local-deleted`.
   First time: `ue_sync("init")` pulls everything and writes the schema lock.
   Rows also include `ue_dirty` and `ue_saved_changed` (unknown = null), so an
   unsaved editor package is distinguishable from a changed saved file.
2. Edit the `.nexus` files (Read / grep / StrReplace). Only non-default values
   are written; delete a line to reset to default.
3. `ue_sync("lint")` — offline, no editor needed: unknown class/property/enum/pin,
   bad types, dangling links, opaque-node edits.
4. `ue_sync("push")` — dry run by default; returns the plan (verb counts + first
   verbs). Then `ue_sync("push", options={"dry_run": false})` applies it: one
   editor transaction per asset (Ctrl+Z undoes the whole push), compile, save
   only the touched packages (never SaveAll), re-export, rewrite the text in
   canonical form. Diagnostics come back as `file:line: message`.
5. `ue_sync("pull")` when the editor changed something (`ue-modified`).

## Format essentials

```
[graph]
c_eps   : Constant(R=0.000001) @ -1000,220         # id : Class(param=value) @ x,y
call    : MaterialFunctionCall(MaterialFunction=/Game/F/MF_A.MF_A)
tex     : TextureSample(Texture=/Game/T/T_Rock.T_Rock)
old     : @opaque(/Script/Engine.MaterialExpressionCustom) @ 0,0   # move/delete/link only

c_eps -> call.A                # src[.pin] -> dst[.pin]; omit pin = single pin
tex.R -> out.Roughness         # materials: implicit `out` node = material outputs
```

- Values are UE `ExportText`: numbers, `True`/`False`, `(R=1,G=0,B=0,A=1)`,
  `/Game/Path/Asset.Asset`, enum short names. Quote strings with spaces.
- Blueprint: `[asset] ParentClass = /Script/Engine.Actor` (only honoured when the
  asset is created), `[variables]` `Name : float = 1 { Category=X, InstanceEditable }`,
  `[components]` `Mesh : StaticMeshComponent(parent=Root) { StaticMesh=... }`,
  `[graph EventGraph]` nodes like `CallFunction(KismetSystemLibrary.PrintString, InString="Hi")`,
  `VariableGet(Health)`, `Event(Actor.ReceiveBeginPlay)` (adopts the template's disabled
  ghost event instead of duplicating it), `Sequence(pins=3)`; exec pins are `execute` /
  `then`; functions are `[function Name(A: double) -> (R: bool)]` with implicit `entry` /
  `result` nodes. `@renamed(Old)` renames a variable/component. Pin defaults equal to the
  function's own default are dropped on re-export.
- Niagara: `[emitter Name]`, `[stack Name/ParticleUpdate]` module lines
  `id : SpawnRate(SpawnRate=100, "Spawn Probability"=1) !disabled` list what the Stack
  panel shows (rapid-iteration values); `[renderers Name]` `sprite : Sprite { SubImageSize=(X=2,Y=2) }`
  (attribute bindings are not in text), `[user]` `"Spawn Rate" : float = 100` with types
  `float int bool Vector2 Vector Vector4 Color Position Quat` or a class name.
  `@link(...)` / `@dynamic` inputs are read-only; `SetVariables(...)` modules can be edited
  but not created from text.

## Rules that save you a round trip

- Ids are yours; GUIDs stay in the base. Renaming an id recreates the node.
- New nodes referenced by links must name the pin unless the node has one pin.
- `both-modified` is refused; pull (or push with `force="local"`) first.
- A MaterialFunction interface change refreshes callers in UE. Callers with
  local edits keep their text and accepted base; selected callers are replanned
  against the refreshed graph before applying their changes.
- Stub files are read-only; `@opaque` nodes cannot be created or edited.
- If `schema_stale` appears, run `ue_sync("schema")` (engine/plugin set changed).

## Push ordering and recovery

- New referenced assets and material dependencies are pushed before consumers.
  References include component properties, node arguments, defaults and nested
  ExportText arrays. New-asset cycles return `dependency_cycle` before writes.
  Include local new dependencies in the selection (`dependency_not_selected`).
- Preflight errors stop the batch with the default `stop_on_error=true`.
  With false, independent assets continue; consumers of failed assets remain blocked.
- Apply, compile, save or export failures preserve local text and the accepted
  base. Rows include `local_preserved`, `pending_file` when available, and
  `recovery_file`; recovery records contain the response, plan, source text and IDs.
- Correct the reported error, then push with `force="local"` for a conflict.
  Force works for both `ue-modified` and `both-modified`, comparing local intent
  against a fresh editor export. Recovery IDs prevent duplicate node creation.
- Dry-run leaves text, base and sync state unchanged; schema/export staging may
  still write into `.nexus`. Successful pushes normalize text and accept a new base.
- A texture property omitted from text has its engine default, including
  DefaultTexture. Remove a material-instance parameter line to clear its override.
