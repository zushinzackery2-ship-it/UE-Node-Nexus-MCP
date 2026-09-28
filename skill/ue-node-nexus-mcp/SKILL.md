---
name: ue-node-nexus-mcp
description: Operate the UE Node Nexus MCP bridge to inspect and edit Unreal Editor assets, graphs, levels and diagnostics, and author .nexus assets through ue_sync. Use for UE Node Nexus MCP, UEMCP Bridge, ue_sync, Content_Transcoded, ue_execute, ue_read, ue_capability_get, and MCP-to-Unreal bridge troubleshooting.
---

# UE Node Nexus MCP

Seven MCP tools share one visible editor per physical project across workspaces.
The editor compiles and saves assets; the manager owns startup, usage and exit.
Live results take precedence over cached schema, source comments and this guide.

## Connect and finish

Use the exact `.uproject` from the task or `--project` / `UE_NEXUS_PROJECT_PATH`.
Context queries inspect the selection. To start an authorized project, call once:

```python
ue_execute("bridge_instance_ensure", dict(
    project_path="D:/UEProjects/Demo/Demo.uproject",
    mode="reuse_or_start", dry_run=False))
```

The default launch profile is `interactive`; all new instances open usable
`UnrealEditor.exe` windows, using `d3d12` (default) or `d3d11`. Supply `engine_path` when EngineAssociation cannot
resolve the installed engine. `mode="reuse_only"` and `dry_run=True` remain the
API defaults; a preview is optional when the intended action is already clear.
An executed ensure is held while the editor starts (`wait_seconds`, default 45)
and returns `startup.outcome`: `ready`; `waiting_for_user` with `blocking_dialog`,
which you show to the user and then call ensure again; or `starting`, which you
repeat as `startup.next` says. Callers share the existing launch.

Confirm the selected project with `project_context_get` and loaded modules with
`bridge_capabilities_get`. Version 0.6.0 uses contract 4; engine BuildIds must
match, and Guard/Core must share a source fingerprint. A running process keeps
the build it loaded until it restarts.

Tasks and publications automatically hold work scopes. Windowed instances are
protected from automatic reclamation. Routine work requires no `pin` calls or
renewals. The hidden legacy pin operation only supports old background editors;
on a windowed instance it reports `requires_renewal=False` without changing state.

Finish with `bridge_instance_release`. Ending the MCP session releases its usage
and leaves the editor window available to the user. When the task includes closing
the editor, `bridge_instance_close(dry_run=False)` waits for actual process exit
and returns `exit_confirmed=True`, `state=EXITED`, and `exit_code`.
`wait=False` requests asynchronous close and reports `exit_confirmed=False`.
Other users, work scopes, dirty packages, PIE, compilation, saving and pending
recovery are reported as close blockers. Save only the named packages authorized
by the task. List/status include `windows` (hidden ones too), `waiting_for_user`,
`blocking_dialog`, `dialog_notices`, `startup_progress`, freshness, resources and
logs; use these to tell a prompt, a long load and a stalled start or exit apart.

For shared policy, adoption, legacy instance cleanup or upgrades, load
`workflow_guide_get(category="instances")`. Exact instance selection remains
available to inspect and close an older offscreen editor.

## Discover the relevant operation

| Tool | Purpose |
|---|---|
| `ue_context_get` | Selection, enabled groups and relevant workflow guides |
| `ue_capability_get` | Operation index; `detail="schema"` or `"examples"` for one operation |
| `ue_execute` | Execute a named registry operation |
| `ue_read` | Typed reads and paged artifact access |
| `ue_diff_get` | Changes since a diff token |
| `ue_plan_validate` | Validate operation payloads before execution |
| `ue_sync` | Text mirror, schema, local history, publication and recovery |

Read an operation schema when its payload is unknown; reuse it during the task.
Hidden operations remain callable by name and are discoverable with
`include_hidden=True`. Read `workflow_guide_get` for task-specific recipes:
`collaboration`, `text_mirror`, `scene_mirror`, `blueprint_authoring`, `instances`
and the other categories listed by the operation.

Author supported asset content through `.nexus` files. Use graph operations for
opaque graph sections, and typed operations for asset management and level work.
The [operation reference](references/operations.md) covers narrow reads, graph
patches, scene writes, response modes and batching.

## Author and publish assets

Load `workflow_guide_get(category="collaboration")` for the full local protocol.
Use ensure's shared `mirror_root`; an existing UE repository binding takes
precedence. Each workspace gets an independent checkout and files directory.

1. `ue_sync("checkout", paths=["/Game/MyFolder"], options=dict(agent_id="A", dry_run=False))`.
   A scoped checkout is sparse and includes referenced dependencies.
2. Edit the returned `files_root` / `file_paths`; pass the returned `id` as
   `workspace_id` in later calls. Use file editing tools and the `.nexus` grammar.
3. Run `lint`, then `stage` and `commit`; `commit(all=True)` captures and stages
   the current files. All these actions support offline work.
4. Push committed HEAD. `dry_run=True` previews the same merge and plans that
   execution uses. Apply with `dry_run=False`; `proposal_id` optionally binds the
   preview. Edits made after commit stay local.
5. Inspect per-asset rows and diagnostics. Publication compiles, verifies readback
   and saves touched packages. Read the relevant result when checking behavior.

New assets use the same grammar beneath `files_root`. Asset and entity deletion
requires `allow_delete=True`; staging a missing file also requires `delete=True`.
Ids identify authored entities; changing an id recreates that entity unless the
format supplies a supported rename annotation. Keep conflict markers out of files.

Publication reports `origin=ue/workspace` and `comparison.before/after` for changes.
`ue_drift_adopted` explains UE changes incorporated into the candidate, including
why its execution plan may be empty. To publish committed text over UE drift, use
`force="local"`; validation and revision guards still apply. Abort an existing
merge session before a fresh forced push.

Explicit default values and omitted defaults represent the same value. Canonical
publication preserves float32/float64 precision and reconciles authored aliases,
generated ids and pin names. A writable readback mismatch produces
`apply_result_mismatch` with file/line/field evidence and guarded restoration;
the requested commit remains available to retry.

## Resolve, recover and inspect history

Conflicts return `merge_id`, `conflict_id`, layer and base/ours/theirs values.
`resolve` accepts `conflict_id`, `conflict_ids`, `asset` glob, `conflict_type`,
`layer`, or `all=True`. Use an allowed `choice`; `custom` and `rename` address
one conflict at a time. Large values are represented by a digest; `ours`,
`theirs` and `base` still resolve the stored value.

`continue(merge_id=...)` completes a resolved publication and accepts the push
preview's `proposal_id`. `abort` ends the session while retaining local edits.
`show(revision=...)` returns historical `.nexus` text; show/diff accept asset paths
and current or historical workspace filenames. Use branch/tag, log, blame,
restore/revert, cherry-pick, stash and reflog as documented by the history guide.
Published history is append-only; reverse it with a new revert publication.

`recover` without `apply_id` handles pending records in the repository, including
records belonging to closed workspaces. Choose `resolution`:

- `inspect`: inspect the durable receipt and adopt an already committed result.
- `restore`: restore the guarded checkpoint; this is the default.
- `abandon`: retain current editor state and close the record.

`abort(apply_id=...)` closes the local pending record without contacting UE.
Failed new-asset creation restores prior absence. Readback rejection can restore
only an unpublished result whose recorded memory and files still match.
`recovery_conflict` preserves the user's dirty state and reports what changed.
Restoring an existing package uses UE's native reload to update references,
preserves the checkpoint's unsaved memory and original disk bytes, and resets
the editor undo history as part of the native reload. The receipt records this.
Published receipts require a revert, rather than checkpoint restoration.

## Schema and format facts

`schema(category=..., query=..., details=True)` reads the same catalog used by
lint. Categories include blueprint/material/niagara/scene/asset/common and
reflection aliases such as `component` and `material_expression`. Follow the
returned `schema_path` index. Function queries resolve inherited functions;
target queries provide instance-dependent properties and pins. Refresh catalogs
after changing reflected Blueprint classes or plugin builds.

`schema_key` identifies the catalog used by the current action;
`workspace_schema_key` identifies the workspace's historical binding.
Offline actions return `bridge_contacted=False` and do not claim measured bridge
availability. A stale merge session includes executable abort/retry instructions;
old snapshots are read under the current schema before comparison.

- Material sections: `[asset]` properties, `[graph]` expressions and connections.
  `out.BaseColor`, `out.EmissiveColor` and other root pins use engine names.
  Custom inputs and TextureSample mip pins are derived from each declaration.
  Texture FunctionInputs require a Preview connection.
- MI sections: `[scalar]`, `[vector]`, `[texture]`, `[switch]` and other
  supported override sections. Omitted overrides inherit from the parent.
- Blueprint: `[variables]`, `[components]`, `[graph EventGraph]`, and
  `[function Name(A: float) -> (Result: bool)]`. Functions have implicit
  `entry` / `result` nodes; local variables are scoped to their function and used
  with `VariableGet(name)` / `VariableSet(name)`. New components and functions
  can be referenced in the same publication.
- `[interfaces]` lists generated interface class paths; `[dispatchers]` contains
  signatures such as `Changed(Value: float)`. Both support addition/removal;
  dispatcher signatures support edits. An implemented function retains its
  interface's declared parameter names and types.
- `Branch`, `Sequence`, `Reroute` and `Comment` are supported Blueprint aliases.
  Multiple exec paths may join one exec input; each exec output and data input
  accepts one connection. DynamicCast result pins use the stable `AsResult` name.
- Calls use `CallFunction(Owner.Function)` or `CallFunction(self.Function)`.
  The bridge selects the native specialized node host, including array calls.
- Blueprint creation honors `ParentClass` and `BlueprintType` (`normal`, `const`,
  `macro_library`, `interface`, `function_library`). Existing types are verified.
- Niagara stacks and scene groups have distinct formats; load their workflow
  guides before authoring them. Schema support flags identify writable fields.

## Diagnose from evidence

An empty list alone does not establish whether the project has assets. Check
`project_context_get`, then `asset_list(package_paths=["/Game"], format="compact")`,
`auto_index_status` and bridge diagnostics as relevant to the failure.

For `unknown_field` / `unknown_query_field`, use the returned accepted fields and
mapping hints. `graph_snapshot_get` uses `graph_name` / `node_class_filter`;
`graph_node_search` uses `filters.node_class`. For `stale_target`, inspect the
reported target/dependency and measured revisions. For
`operation_outcome_unknown`, inspect state or the apply receipt before retrying.

Live material compilation submits missing shader work, then reports shader
readiness and the target RHI update fence. Diagnostics reads inspect loaded
objects. A compile result does not assert whole-frame GPU completion.
Bridge save operations report read-only files directly. A start nobody can see
(managed, hidden or offscreen) has UE's OK-only "Low Drive Space" advisory
confirmed by the Guard and reported in `dialog_notices` with the locations and
free space. Any other startup prompt waits for its user as `waiting_for_user`;
requests meanwhile return `waiting_for_user` with the dialog in `details`.
