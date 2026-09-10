# Diagnostics and repair loops

`diagnostics_get` inspects loaded objects without compiling or loading assets.
`assets_not_loaded` reports the uninspected assets. Explicit `asset_compile`
and material writes use the compilation service, which reports
`submitted/pending/ready/failed`, `shader_ready`, `render_ready` and duration.
`ready` after a requested compile covers target shader completion and an RHI
thread resource-update fence; it is not a whole-frame GPU completion signal.

## Two kinds of error counts — do not mix them

- **Project-wide**: `diagnostics_get` (or `ue_read(target="diagnostics")`)
  returns `data.error_count`, `data.warning_count`, and `data.items`, filterable
  by `asset_path` / `severity`.
- **Per-asset, post-write**: concrete asset responses that carry `asset_path`
  may include a root-level integer `remaining_errors` — the errors still present
  for that asset after the write's post-check. It never appears on global reads.

`diagnostics_get` also attaches `data.related_log_items`: material compile
fallbacks, sampler mismatches, and similar clues mined from the UE log. These
are historical, carry `stale_possible=true`, and are NOT added to the current
`error_count`. For raw log lines beyond those curated clues, use
`log_tail_get` (or `ue_read(target="log")`) with a `match` substring filter —
an MCP-local read of the newest project log file.

## Connection and contract triage

1. `ue_context_get()` — is an instance bound? Which groups are enabled?
2. `ue_execute("bridge_instance_list", {})` — which editors are live? Bind with
   `bridge_instance_select` when more than one.
3. `ue_execute("bridge_contract_check", {})` — Python contract vs. operations
   actually loaded in the UE bridge; catches a stale plugin or server build.
4. `ue_execute("project_context_get", {}, response={"mode":"full"})` — project
   path, content dir, `/Game` mount.

`bridge_capabilities_get` includes `build` entries for Core and VFX: embedded
version, source fingerprint, commit/dirty state, protocol version, loaded module
path and engine BuildId. Python checks the loaded contract before writes.
`bridge_contract_mismatch` or `bridge_build_identity_missing` requires matching
plugin/Python packages and an editor restart. Source builds without recorded
provenance identify that explicitly; they still embed their actual fingerprint.

Request, compilation, fence, instance and save phases share a request ID in UE
logs. Python rotates `bridge.log` under `UE_NEXUS_LOG_DIR` (Windows default:
`%LOCALAPPDATA%/UE-Node-Nexus-MCP/Logs`). Filter by request ID for one operation.

Mirror operations share a filesystem transaction lock; `sync_busy` means an
existing owner must finish before another server writes the same state files.
Save callbacks enqueue exports, processed outside saves/transactions in a
bounded tick budget. Automatic exports use `.nexus/pending/watch/`.

## Crash evidence and isolation

The plugin and editor share an address space. Lifecycle ordering prevents
bridge-originated reentrancy and premature completion claims; it does not make
engine assertions or GPU driver faults recoverable. A dedicated UE worker can
contain automation failures in a disposable project copy. Main-editor loading
still creates its own RHI resources and retains engine/driver risk.

An empty ComputePSO assertion identifies the failing state, not its producer.
Root-cause attribution requires a reproducible state transition with matching
DLL fingerprints, symbols and rendering backend. Keep compile-only validation
separate from a real DX12 replay; NullRHI cannot validate that rendering path.

## Empty-result diagnosis

Never conclude "no project" from one empty list. Cross-check
`project_context_get`, a small `asset_list(package_paths=["/Game"])`,
`auto_index_status` / `auto_index_overview`, and `diagnostics_get`:

- Context empty or mismatched → wrong editor instance.
- Assets present but AutoIndex empty → index lifecycle issue; consider
  `auto_index_rebuild`.
- Both empty with valid context → AssetRegistry scan/mount/filter issue.

## Compile-fix loop

1. `asset_compile` the failing asset; read structured diagnostics.
2. Fix through the narrowest write (`node_params_set`, `graph_patch_apply`,
   `material_instance_params_set`, ...), dry-run first.
3. Re-compile; repeat until `0 errors`; `asset_save`.
4. Confirm the change with `ue_diff_get(since_token=...)` or a targeted read.

After a cold editor start, compile material parents before validating their
instances. An instance compile waits for its parent's existing target resources;
it does not submit a missing parent shader map. A parent readiness diagnostic
names the parent asset that must be compiled first. This ordering also applies
to instance parent changes and batch verification after reopening a project.
