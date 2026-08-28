# Diagnostics and repair loops

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
`error_count`.

## Connection and contract triage

1. `ue_context_get()` — is an instance bound? Which groups are enabled?
2. `ue_execute("bridge_instance_list", {})` — which editors are live? Bind with
   `bridge_instance_select` when more than one.
3. `ue_execute("bridge_contract_check", {})` — Python contract vs. operations
   actually loaded in the UE bridge; catches a stale plugin or server build.
4. `ue_execute("project_context_get", {}, response={"mode":"full"})` — project
   path, content dir, `/Game` mount.

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
