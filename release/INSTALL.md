# UE Node Nexus MCP

## Install

1. Close the target Unreal Editor. Copy the archive's `Plugins` directory into
   your UE project's root directory. The resulting paths must include
   `Plugins/UeNodeNexusBridge/UeNodeNexusBridge.uplugin` and
   `Plugins/UeNodeNexusVfxBridge/UeNodeNexusVfxBridge.uplugin`.
2. Install Python 3.11+ and the matching wheel from the same release or local build:

   ```bat
   py -3 -m venv .venv
   .venv\Scripts\python.exe -m pip install ue_node_nexus_mcp-0.4.0-py3-none-any.whl
   ```

3. Configure your MCP client to run the absolute path to
   `.venv/Scripts/ue-node-nexus-mcp.exe` using stdio. Set
   `UE_NEXUS_TRANSCODE_DIR` to the directory for your asset text mirror.
4. Start UE with both plugins enabled. Query `ue_context_get`, then initialize
   the mirror with `ue_sync("init")`. The VFX plugin is optional for projects
   that do not use Niagara or Cascade.

The included `skill/ue-node-nexus-mcp` directory contains optional agent workflow
instructions. Full documentation and source:
https://github.com/zushinzackery2-ship-it/UE-Node-Nexus-MCP

## Compatibility

The binaries target UE 5.5.4 Launcher on Windows x64, module BuildId `37670630`.
They are editor Development binaries. Source is included for rebuilding against
other engine builds; matching Python and plugin versions are required.

Version 0.4.0 uses contract 2. `bridge_capabilities_get` reports each loaded
module's embedded source fingerprint, commit/dirty state, version, path and
BuildId. Python checks this contract before writes. Each plugin includes a
`BuildIdentity.json`; the packaging verifier checks its fingerprint against
both the source and a string embedded in the DLL.

`SCENES.md` describes loaded Actor/Blueprint/ISM/HISM groups. Start with an
explicit `ue_sync("pull", options=...)` scene selector, then edit `.scene.nexus`
files. Python logs rotate under `%LOCALAPPDATA%/UE-Node-Nexus-MCP/Logs`, overridable
with `UE_NEXUS_LOG_DIR`.

## Verify Downloads

`SHA256SUMS.txt` covers the ZIP, wheel and release manifest. Use
`Get-FileHash -Algorithm SHA256` in PowerShell to compare downloaded files.
The manifest also records the source commit and checksums for packaged files.

Local build manifests use `mode=local-build` and record `source_dirty`; they
do not imply a Git tag or a published release. Validation logs and the local
delivery report distinguish compilation/static checks from runtime testing.
