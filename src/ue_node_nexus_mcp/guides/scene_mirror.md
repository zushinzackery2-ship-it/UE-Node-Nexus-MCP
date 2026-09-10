# Loaded scene groups and ISM/HISM instances

Use `ue_sync` to edit an explicit group in the current editor world. Ordinary
Actors, Blueprint Actors, ISM and HISM are supported. Select actor paths from
`level_actors_list`; the selected map must already be open outside PIE.

## Import and edit

```python
ue_sync("pull", options=dict(scene=dict(
    map_path="/Game/Maps/World", name="Block",
    actor_paths=["/Game/Maps/World.World:PersistentLevel.Tiles"],
)))
ue_sync("status", paths=["Scenes/Maps/World"])
ue_sync("lint", paths=["Scenes/Maps/World/Block.scene.nexus"])
ue_sync("push", paths=["Scenes/Maps/World/Block.scene.nexus"])
ue_sync("push", paths=["Scenes/Maps/World/Block.scene.nexus"],
        options=dict(dry_run=False, compile=True, save=True))
```

The file lives at `<mirror>/<Project>/Scenes/Maps/World/Block.scene.nexus`.
New files under `Scenes/` are discovered by file or directory selection. Their
map/group header must agree with their path. Use `class: World` and the current
schema key. A small authored group can look like this:

```text
nexus: 1
asset: /Game/Maps/World
class: World
schema: <current-schema-key>

[scene]
Name = "Block"

[actors]
tiles : /Script/Engine.Actor { Label="Tiles" }

[components tiles]
mesh : /Script/Engine.InstancedStaticMeshComponent { StaticMesh=/Engine/BasicShapes/Cube.Cube, CustomDataCount=1 }

[instances tiles.mesh]
tile_1 : Instance { Transform=(Rotation=(X=0,Y=0,Z=0,W=1),Translation=(X=100,Y=0,Z=0),Scale3D=(X=1,Y=1,Z=1)), CustomData=(0.25) }
```

Use a Blueprint asset path for a Blueprint Actor's class. Native classes and
component classes use `/Script/...` paths. Actor fields include `Label`,
`Folder`, `Level`, `Parent` and `Transform`. Other declaration properties are
reflected editable instance properties, including `Tags`, meshes,
`OverrideMaterials`, collision, visibility and shadows. A reflected property
sharing a reserved field name uses `Property.Label`, `Property.Transform`, etc.

## Coordinates and identity

| Content | Convention |
|:-----|:-----|
| **Root actor** | World transform |
| **Attached actor** | Transform relative to its parent; `Parent` names a logical actor ID or a loaded actor path |
| **Component** | Local transform; root component transform belongs to the actor |
| **Instance** | Local transform inside its ISM/HISM component |
| **Custom data** | Exactly `CustomDataCount` finite floats per instance, with 0–1024 channels |

Logical text IDs map to native Actor GUIDs and persistent editor-only
component/instance metadata. Array positions are not identities. Reordering
text lines does not recreate instances; removal and relocation preserve
survivors' IDs. Metadata is transactional and excluded from cooked objects.

Pull and status leave UE metadata unchanged. The first applied push validates
the snapshot before binding IDs. An ambiguous whole-array replacement returns
`identity_conflict`. Explicitly adopt UE with `pull` and `force="ue"`, then
edit and push. Status marks the pending adoption until that write completes.

Construction-script components export as `@opaque`. Arrays changed by a
construction script carry `InstancesReadOnly=true` and reject content edits.
Actor changes finish construction before component overrides; instance changes
run after component updates. Inherited and root components are retained. Actor
copies acquire distinct identities through their new native Actor GUID.

## Transactions, dependencies and recovery

- One actor belongs to one scene group. Overlapping ownership is an error.
- Loaded hidden sublevels are included. Unloaded levels/World Partition actors
  report `unavailable`; absence from a loaded-only scan cannot authorize deletion.
- Actor/component/instance deletion requires `allow_delete=True`. Deleting a
  managed scene file under this option removes its actors and writes an empty
  scene file retaining the group identity.
- Mirrored dependencies are selected recursively and applied before the scene.
  Failed dependencies block their scenes. A dry run with planned asset changes
  reports `preflight=pending_dependencies`; UE preflight follows asset application.
- Dry-run rows include verb counts, a compact preview and a local plan file.
  A real apply uses one editor transaction per group and batched instance APIs.
- Save targets are the touched map/sublevel and external Actor packages.
  Responses separate applied operations, saved packages and failed packages.
- Failures preserve edited text, accepted baseline and state. Journals live in
  `.nexus/scenes/recovery/`. Retry compares live state and reconciles interrupted
  creation instead of duplicating actors or their native components.
- Attempt tokens associate late responses with the correct journal. Local
  edits made during a push survive. `force="local"` resolves a live-content
  conflict; `force="ue"` belongs to pull when adopting editor content or identity.
- A mirror root has one transaction owner across threads and server processes.
  Concurrent mirror requests return `sync_busy`; retry after the owner finishes.

Level Instance/Packed Level Actor contents, Foliage/PCG editing and automatic
loading of unloaded partitions are outside this backend.

## Direct instance operations

`component_instances_get(component_path, offset, limit, revision)` returns at
most 10,000 rows, a revision, channel count, total and next offset. Reuse the
revision for later pages to avoid mixing versions of a changed array.

`component_instances_patch` accepts `component_path`, the read revision, `ops`,
optional `custom_data_count`, `dry_run` and `save`. Ops are `add`, `update` and
`remove`; update/remove require an ID, while add can allocate one. Custom data
matches the component channel count. Query schema/examples with `ue_capability_get`.

Internal `scene_export`, `scene_status` and `scene_apply` transfer large data
through JSON files. The seven public MCP facades remain the entry points.
