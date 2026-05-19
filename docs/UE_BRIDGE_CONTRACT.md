# UE Bridge Contract

This document defines the editor-side contract consumed by the MCP server.

The bridge must expose an HTTP `POST /mcp` endpoint that accepts one operation
envelope and returns one structured result. The bridge may be implemented as an
Unreal Editor plugin, editor subsystem, or trusted local editor service.

## Request Envelope

```json
{
  "operation": "graph_snapshot_get",
  "request_id": "uuid",
  "payload": {}
}
```

## Response Envelope

```json
{
  "ok": true,
  "operation": "graph_snapshot_get",
  "request_id": "uuid",
  "data": {},
  "diagnostics": [],
  "warnings": []
}
```

## Error Envelope

```json
{
  "ok": false,
  "operation": "graph_snapshot_get",
  "request_id": "uuid",
  "error": {
    "code": "asset_not_found",
    "message": "Asset could not be loaded",
    "details": {}
  },
  "diagnostics": [],
  "warnings": []
}
```

## Graph Snapshot Shape

`graph_snapshot_get` defaults to `format="wires_tiny"` and
`include_node_params=false`. This is the normal mode for AI callers because it
returns a node dictionary plus compact edge table instead of JSON node and pin
objects.

Tiny wire text snapshot:

```json
{
  "format": "wires_tiny",
  "asset_path": "/Game/Materials/M_Example.M_Example",
  "asset_class": "/Script/Engine.Material",
  "graph_name": "MaterialGraph",
  "graph_kind": "material",
  "text": "W2\nN:0=SP:Roughness;1=Mul\nE:0.o>1.B\nT:SP1,Mul1\n"
}
```

Use `format="wires_min"` for a less abbreviated node dictionary and edge table.
Use `format="wires"` for a more human-readable horizontal wire table.

Wire text snapshot:

```json
{
  "format": "wires_text",
  "asset_path": "/Game/Materials/M_Example.M_Example",
  "asset_class": "/Script/Engine.Material",
  "graph_name": "MaterialGraph",
  "graph_kind": "material",
  "text": "========================================================================\n  Wire graph - 3 wires\n========================================================================\n  ScalarParameter(Roughness).0 (N0) -> Multiply.B (N1)\n\n------------------------------------------------------------------------\n  Node type stats:\n    ScalarParameter x 1\n    Multiply x 1\n"
}
```

Use `format="compact"` when a caller needs node/pin aliases plus the real UE id
map for later write tools.

Compact snapshot:

```json
{
  "format": "compact_graph",
  "asset_path": "/Game/Materials/M_Example.M_Example",
  "asset_class": "/Script/Engine.Material",
  "graph_name": "MaterialGraph",
  "graph_kind": "material",
  "columns": {
    "nodes": ["id", "class", "name", "x", "y"],
    "pins": ["id", "node", "dir", "name", "type", "default"],
    "links": ["from_pin", "to_pin"],
    "params": ["node", "name", "type", "value", "editable"]
  },
  "nodes": [
    ["n0", "MaterialExpressionScalarParameter", "Roughness", 0, 0]
  ],
  "pins": [
    ["p0", "n0", "output", "0", "material", ""]
  ],
  "links": [
    ["p0", "p3"]
  ],
  "params": [],
  "ids": {
    "nodes": { "n0": "real-node-guid" },
    "pins": { "p0": "real-pin-id" }
  }
}
```

Use `ids.nodes` and `ids.pins` to translate compact aliases back to the stable
node and pin identifiers required by write tools.

Set `format="full"` for the legacy verbose shape:

```json
{
  "asset_path": "/Game/Materials/M_Example.M_Example",
  "asset_class": "Material",
  "graph_name": "MaterialGraph",
  "graph_kind": "material",
  "nodes": [
    {
      "node_id": "stable-node-id",
      "class_name": "MaterialExpressionScalarParameter",
      "display_name": "Roughness",
      "position": { "x": 0, "y": 0 },
      "pins": [
        {
          "pin_id": "stable-pin-id",
          "name": "Result",
          "direction": "output",
          "type": "float",
          "linked_to": []
        }
      ],
      "params": [
        {
          "name": "ParameterName",
          "type": "float",
          "value": "1.0",
          "editable": true
        }
      ]
    }
  ],
  "links": [
    {
      "from_node_id": "stable-node-id",
      "from_pin_id": "stable-pin-id",
      "to_node_id": "stable-node-id",
      "to_pin_id": "stable-pin-id"
    }
  ],
  "diagnostics": []
}
```

## Patch Operation Shape

## Blueprint Detail Shape

`blueprint_details_get` is a read-only Blueprint summary. It defaults to
`format="compact"` and returns row arrays to keep CDO/default inspection cheap
for AI callers.

```json
{
  "format": "blueprint_details_compact",
  "asset_path": "/Game/BP/BP_Example.BP_Example",
  "parent_class": "/Script/Engine.Actor",
  "generated_class": "/Game/BP/BP_Example.BP_Example_C",
  "variable_columns": ["name", "type", "default", "category"],
  "variables": [["Health", "real", "100", "Stats"]],
  "default_columns": ["name", "value"],
  "defaults": [["Health", "100.0"]],
  "component_columns": ["name", "class", "parent", "socket", "asset"],
  "components": [["Mesh", "SkeletalMeshComponent", "", "", "/Game/Characters/SK.SK"]],
  "missing_defaults": [],
  "elapsed_ms": 2.1
}
```

When `property_names` is empty, defaults are limited to Blueprint-authored
variables. Pass explicit property names to inspect specific CDO values.

## AnimBlueprint Summary Shape

`anim_blueprint_summary_get` is a read-only semantic pass over common AnimGraph
nodes. It intentionally extracts a fixed allowlist of useful fields instead of
dumping every reflected property.

```json
{
  "format": "anim_blueprint_summary_compact",
  "asset_path": "/Game/ABP/ABP_Example.ABP_Example",
  "columns": ["node_id", "graph", "class", "title", "x", "y", "summary"],
  "items": [
    ["guid", "AnimGraph", "AnimGraphNode_TwoBoneIK", "Two Bone IK", 0, 0, "alpha=1;ik_bone=hand_r"]
  ],
  "type_counts": [["AnimGraphNode_TwoBoneIK", 1]],
  "returned_count": 1,
  "total_count": 1,
  "truncated": false,
  "elapsed_ms": 3.4
}
```

## Asset Creation Shape

`asset_create` creates only fixed supported asset kinds. It does not expose
arbitrary factories or Python execution.

```json
{
  "asset_path": "/Game/Materials/M_New.M_New",
  "asset_kind": "material",
  "dry_run": true,
  "save": false
}
```

Supported `asset_kind` values:

- `material`
- `material_instance`
- `blueprint`

For `material_instance`, `parent_asset_path` may point to a material or material
instance parent. For `blueprint`, `parent_class_path` may point to a Blueprintable
UClass path; it defaults to Actor.

Creation defaults to `dry_run=true`. A real create returns the normal write
response shape with `diff.assets_created`, dirty state, and optional save status.

```json
{
  "asset_path": "/Game/BP/BP_Example.BP_Example",
  "graph_kind": "blueprint",
  "graph_name": "EventGraph",
  "dry_run": true,
  "compile_after": true,
  "operations": [
    {
      "op": "connect_pins",
      "from_node_id": "node-a",
      "from_pin_id": "then",
      "to_node_id": "node-b",
      "to_pin_id": "execute"
    }
  ]
}
```

Supported patch operation names are intentionally finite:

- `connect_pins`
- `disconnect_pins`
- `set_node_param`
- `set_node_position`
- `create_node`
- `delete_node`

The UE bridge must reject unknown operation names.

Blueprint node and pin identifiers are the GUID strings returned by
`graph_snapshot_get`. Material node identifiers are material expression GUID
strings. Material pin identifiers use the snapshot format
`<node_id>:in:<index>` and `<node_id>:out:<index>`.

`create_node` requires a concrete Unreal class path:

```json
{
  "op": "create_node",
  "class_path": "/Script/BlueprintGraph.K2Node_CallFunction",
  "position": { "x": 120, "y": 80 }
}
```

`set_node_param` writes a Blueprint input pin default or an editable Material
expression property:

```json
{
  "op": "set_node_param",
  "node_id": "stable-node-id",
  "name": "ParameterName",
  "value": "0.5"
}
```

`graph_snapshot_get` returns the same node `params` array when
`include_node_params=true`. Keep the default `false` for topology-first reads
and call `node_params_get` for specific nodes that need values. `include_links=false`
suppresses link population while preserving node and pin identity.

`node_params_get` returns editable Blueprint input pin defaults or editable
Material expression properties for one node. `node_params_set` accepts a flat
object keyed by parameter name and returns the same write response shape as graph
patches.

## Write Response Shape

Every write operation must return the normal response envelope plus a write
result in `data`. This applies to `graph_patch_apply`, `node_params_set`, and
`material_instance_params_set`.

```json
{
  "ok": false,
  "operation": "graph_patch_apply",
  "request_id": "uuid",
  "data": {
    "dry_run": false,
    "applied": true,
    "changed": true,
    "diff": {
      "nodes_created": [],
      "nodes_deleted": [],
      "links_added": [],
      "links_removed": [],
      "params_changed": []
    },
    "post_checks": {
      "pin_integrity": {
        "ok": true,
        "broken_links": [],
        "missing_pins": []
      },
      "compile": {
        "requested": true,
        "ran": true,
        "ok": false
      },
      "dirty_state": {
        "package_dirty": true,
        "saved": false
      }
    }
  },
  "diagnostics": [
    {
      "severity": "error",
      "code": "compile_error",
      "message": "Readable compile error",
      "asset_path": "/Game/BP/BP_Example.BP_Example",
      "graph_name": "EventGraph",
      "node_id": "optional-node-id",
      "pin_id": "optional-pin-id",
      "source": "Unreal",
      "raw": "Original engine message"
    }
  ],
  "warnings": []
}
```

`ok` is false when the bridge successfully applied a mutation but post-mutation
compile or pin-integrity checks fail. This lets the MCP client see exactly what
changed and why the asset is now invalid instead of hiding the failure behind a
generic transport error.

For `dry_run=true`, the bridge must not mutate the asset. It still must return
the planned diff and validation diagnostics. Compile checks may be skipped for a
pure dry run unless the bridge can perform them without modifying editor state.

## Diagnostic Shape

```json
{
  "severity": "error",
  "code": "compile_error",
  "message": "Readable message",
  "asset_path": "/Game/BP/BP_Example.BP_Example",
  "graph_name": "EventGraph",
  "node_id": "optional-node-id",
  "pin_id": "optional-pin-id",
  "source": "Unreal",
  "raw": "Original engine message"
}
```
