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

## Stable Graph Snapshot Shape

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
      "params": {}
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

`node_params_get` returns editable Blueprint input pin defaults or editable
Material expression properties. `node_params_set` accepts a flat object keyed by
parameter name and returns the same write response shape as graph patches.

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
