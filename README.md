# UE Node Nexus MCP

UE Node Nexus MCP is a focused MCP server for Unreal Engine graph workflows.

The project intentionally avoids arbitrary Python execution. Models call fixed,
typed tools for asset discovery, level inspection, graph snapshots, graph patch
application, material instance parameter edits, compilation diagnostics, and
package saving.

## Architecture

- `src/ue_node_nexus_mcp`: MCP server and bridge client.
- `docs/ARCHITECTURE.md`: module boundaries, operation contract, safety model.
- `tests`: contract-level tests that do not require Unreal Editor.

The MCP server forwards validated operations to a UE bridge endpoint. The bridge
is expected to be implemented by an Unreal Editor plugin or editor-side service.

## Run

```powershell
python -m ue_node_nexus_mcp.server
```

Environment variables:

- `UE_NEXUS_BRIDGE_URL`: UE bridge endpoint. Default: `http://127.0.0.1:8765`.
- `UE_NEXUS_TIMEOUT_SECONDS`: HTTP timeout. Default: `30`.

## UE 5.5 Bridge Plugin

The editor-side bridge plugin lives at `Plugins/UeNodeNexusBridge`.

Scripts:

- `scripts/build_plugin_ue55.bat`: packages the plugin with UE 5.5 RunUAT.
- `scripts/install_ue55_plugin.bat`: copies the plugin to `UE_5.5\Engine\Plugins\Marketplace`.
- `scripts/clean_specialagent.ps1`: removes exact-name `SpecialAgent` files/directories from known agent config roots and reports remaining config references.
- `scripts/clean_specialagent_and_install_ue55.bat`: runs the cleanup script, then installs the bridge plugin.

The bridge listens on `http://127.0.0.1:8765/mcp` inside Unreal Editor.

Implemented bridge write surfaces:

- Blueprint graph patch: connect/disconnect pins, set node position, create/delete nodes, and set input pin defaults.
- Material graph patch: connect/disconnect expression inputs, set expression position, create/delete expressions, and set editable expression properties.
- Node params: enumerate and write Blueprint input pin defaults and Material expression editable properties.

Write tools default to `dry_run=true` and return diff, pin integrity, compile,
dirty state, and diagnostics in one response.
