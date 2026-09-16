# UE Node Nexus MCP 0.6.0

## Install

1. Finish work, save the intended changes and close the target editor. Install the archive's `Plugins` directory into
   your UE project's root directory. The resulting paths must include
   `Plugins/UeNodeNexusBridge/UeNodeNexusBridge.uplugin` and
   `Plugins/UeNodeNexusVfxBridge/UeNodeNexusVfxBridge.uplugin`. Bridge must contain
   both Core and Guard DLLs plus the module manifest. VFX is optional.
2. Install Python 3.11+ and the matching wheel from the same release or local build:

   ```bat
   py -3 -m venv .venv
   .venv\Scripts\python.exe -m pip install ue_node_nexus_mcp-0.6.0-py3-none-any.whl
   ```

3. Configure your MCP client to run the absolute path to
   `.venv/Scripts/ue-node-nexus-mcp.exe` using stdio. Pass
   `--project D:/UEProjects/Demo/Demo.uproject`, or set `UE_NEXUS_PROJECT_PATH`.
   All workspaces for this project use the same physical project path.
4. Query `ue_context_get`, then preview/apply `bridge_instance_ensure`.
   Default mode is `reuse_only`; explicitly choose `reuse_or_start` to start
   an absent editor. Apply with `dry_run=False`, poll status until READY, and
   finish tasks with `bridge_instance_release`.
5. Use ensure's shared `mirror_root` and create a separate `ue_sync("checkout")`
   for each agent. Existing UE repository bindings take precedence; conflicting
   explicit roots return `repository_mismatch` with the current location.

The Wheel also provides `ue-node-nexus-manager`. Clients bootstrap its fixed,
self-contained archive in the user's Runtime directory; ordinary workspaces
share that directory automatically. `INSTANCES.md` describes policy and diagnosis.
Automatic bootstrap uses the same Windows user's desktop shell as the process
parent, keeping the manager and editors alive when the initiating MCP host ends
its process tree. An accessible signed-in desktop is required for bootstrap.
Noninteractive clients can connect to an already running manager; start
`ue-node-nexus-manager` in an independent user terminal for that setup.
Unavailable shell/process access returns `manager_launch_unavailable` with
diagnostic details. Startup helpers remain hidden.

The included `skill/ue-node-nexus-mcp` directory contains optional agent workflow
instructions. Full documentation and source:
https://github.com/zushinzackery2-ship-it/UE-Node-Nexus-MCP

## Compatibility

The binaries target UE 5.5.4 Launcher on Windows x64, module BuildId `37670630`.
They are editor Development binaries. Source is included for rebuilding against
other engine builds; matching Python and plugin versions are required.

Version 0.6.0 uses bridge contract 4 and lifecycle protocol 1. `bridge_capabilities_get` reports each loaded
module's embedded source fingerprint, commit/dirty state, version, path and
BuildId. Python checks this contract before writes. Each plugin includes a
`BuildIdentity.json`; the packaging verifier checks its fingerprint against
both the source and a string embedded in every DLL, including Guard. Core and
Guard must share a source fingerprint; all required modules share the engine
BuildId. The manager checks installed Guard binaries before creating an editor.

Existing editors keep their loaded DLLs and remain external/protected. Old or
unverified editors prevent duplicate managed startup; finish their work before
upgrading and restarting them. For a mismatched manager, release its work and
let it exit before starting the new version.

`SCENES.md` describes loaded Actor/Blueprint/ISM/HISM groups. Start with an
explicit `ue_sync("pull", options=...)` scene selector, then edit `.scene.nexus`
files. Python logs rotate under `%LOCALAPPDATA%/UE-Node-Nexus-MCP/Logs`, overridable
with `UE_NEXUS_LOG_DIR`.

Manager state lives in `%LOCALAPPDATA%/UE-Node-Nexus-MCP/Runtime`. Defaults are
two managed editors, one startup, 300 seconds unused lease expiry and 120 seconds
idle grace. Heartbeats/status queries do not renew use. Other users, work,
dirty packages, interactive protection and recovery receipts block cleanup.
Lifecycle logs are 10 MiB × 4 files. MCP logs are independent per session,
with one 1 MiB active file and three backups. Each session reserves 5 MiB,
within a 256 MiB total budget and seven-day inactive retention.

## Verify Downloads

`SHA256SUMS.txt` covers the ZIP, wheel and release manifest. Use
`Get-FileHash -Algorithm SHA256` in PowerShell to compare downloaded files.
The manifest also records the source commit and checksums for packaged files.

Local build manifests use `mode=local-build` and record `source_dirty`; they
do not imply a Git tag or a published release. Validation logs and the local
delivery report distinguish compilation/static checks from runtime testing.
