# UE Node Nexus MCP Architecture

## Goal

Provide safe, convenient MCP tools for Unreal Engine material and Blueprint graph
workflows without asking the model to generate ad-hoc editor Python.

## Non-Goals

- No generic Python execution tool.
- No scene dressing automation as a primary surface.
- No viewport control surface in the first implementation.

## Boundary

The MCP process owns protocol shape, validation, readable return contracts, and
tool naming. Unreal Editor owns asset loading, graph mutation, transactions,
compilation, diagnostics, dirty state, and saving.

```text
MCP client
  -> ue-node-nexus-mcp Python server
    -> UE bridge HTTP endpoint
      -> Unreal Editor plugin / editor-side service
```

## UE Integration Shape

The MCP server should stay outside Unreal Editor as a normal MCP process. Unreal
Editor should receive a dedicated Editor plugin named `UeNodeNexusBridge` that
implements the bridge endpoint and all real Unreal API calls.

This split keeps the MCP protocol independent from a specific engine build while
keeping graph edits inside the editor process where transactions, package dirty
state, asset registry access, Blueprint compilation, material recompilation, and
save behavior are valid.

The UE plugin owns:

- Asset registry queries.
- Current editor level and actor/component inspection.
- Material graph snapshots and material graph patch execution.
- Blueprint graph snapshots and Blueprint graph patch execution.
- Material instance parameter enumeration and mutation.
- Compile, validate, diagnostics, and package save operations.

The external MCP server owns:

- MCP tool registration.
- Stable request and response envelopes.
- Basic argument validation.
- Refusing unknown operations.
- Transport to the local UE bridge.

The MCP server must not expose arbitrary Python execution. If Python is used
inside Unreal, it is an implementation detail behind fixed bridge operations.

## Tool Layers

| Layer | Tools | Behavior |
| --- | --- | --- |
| Read-only index | `asset_list`, `asset_get`, `level_current_get`, `level_actors_list` | Enumerate assets and the current editor level without mutation. |
| Graph snapshot | `graph_snapshot_get`, `node_params_get` | Return complete nodes, pins, links, parameters, graph metadata, and diagnostics hints. |
| Transaction patch | `graph_patch_apply`, `node_params_set` | Validate first, default to dry-run, apply through UE transactions, then return pin-integrity and compile diagnostics. |
| Material instance | `material_instance_params_get`, `material_instance_params_set` | Enumerate and edit scalar/vector/texture/static parameters with typed values and post-write diagnostics. |
| Diagnostics/save | `asset_compile`, `asset_validate`, `asset_save`, `diagnostics_get` | Compile, validate, return structured errors, and save without unsafe overwrite. |

## Operation Contract

All MCP tools forward a fixed operation envelope to the UE bridge:

```json
{
  "operation": "graph_snapshot_get",
  "request_id": "uuid",
  "payload": {}
}
```

Expected bridge response:

```json
{
  "ok": true,
  "operation": "graph_snapshot_get",
  "request_id": "same uuid",
  "data": {},
  "diagnostics": [],
  "warnings": []
}
```

Failures must be machine-readable:

```json
{
  "ok": false,
  "operation": "graph_snapshot_get",
  "request_id": "same uuid",
  "error": {
    "code": "asset_not_found",
    "message": "Asset could not be loaded",
    "details": {}
  },
  "diagnostics": []
}
```

## Safety Rules

- Write tools default to `dry_run=true`.
- Patch tools must support validation before mutation.
- Write tools must return a diff-like summary.
- Write tools must return post-mutation checks in the same response: pin
  integrity, compile status, dirty state, and structured diagnostics.
- `compile_after` defaults to `true` for write tools. A failed compile after a
  successful mutation returns `ok=false` with diagnostics and the applied diff,
  not a generic bridge failure.
- UE-side writes must use transactions and mark packages dirty explicitly.
- Save is separate from mutation.
- Save requests must report dirty state, read-only/package lock state, and any
  open editor conflicts instead of silently overwriting.
- Node and pin references use stable IDs supplied by graph snapshots, not display
  names alone.
- Every diagnostic includes severity, message, asset path, optional graph name,
  optional node ID, optional pin ID, and original UE message when available.

## Verified Implementation Scope

The Python MCP server in this repository does not call Unreal APIs directly. It
only validates the stable MCP-facing contract and forwards requests to the UE
bridge. Unreal-specific API calls belong in the future Editor plugin where they
can be compiled and tested against a concrete UE version.
