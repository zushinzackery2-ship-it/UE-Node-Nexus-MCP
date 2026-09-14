# Local collaboration and version history

Version 0.5.0 / bridge contract 3 adds a local repository behind `ue_sync`.
Use one mirror root per UE project and one workspace per agent. Each workspace
has an independent HEAD, index and files directory. The editor serializes final
publication; independent local edits and commits stay isolated.

## Start and publish

1. `ue_sync("checkout", paths=["/Game/Materials"], options=dict(agent_id="A", dry_run=False))`.
   Read the returned `id`, `files_root`, `file_paths`, `schema_key` and `schema_path`.
2. Edit only this workspace's files. New assets use the same `.nexus` syntax
   beneath its `files_root`. Another agent performs a separate checkout.
3. `ue_sync("stage", paths=["/Game/Materials/M_A"], options=dict(workspace_id="<id>", dry_run=False))`.
4. `ue_sync("commit", options=dict(workspace_id="<id>", message="Describe intent", dry_run=False))`.
   Commit reads the index. Edits made after stage remain unstaged.
   `commit(all=True)` combines capture, stage and commit.
5. Preview `ue_sync("push", options=dict(workspace_id="<id>"))`.
6. Apply with `dry_run=False`, optionally including the returned `proposal_id`.

Mutations default to `dry_run=True`; queries run directly. `push` defaults to
committed HEAD, or accepts `source`/`revision`. A changed preview input returns
`stale_proposal`. Uncommitted file changes remain local.

`fetch` observes UE without moving workspace layers. `pull` integrates that
version and replays staged and unstaged changes separately. `status` reports
both layers, branch movement, active conflicts, pending applies and file paths.

## Resolve conflicts

The merge uses persisted common ancestors. Compatible field edits merge;
competing values or structural changes return `merge_id`, stable `conflict_id`,
base/ours/theirs values and an artifact with complete inputs. Ours always means
the current workspace; theirs is the branch or current UE state being integrated.

```python
ue_sync("show", options=dict(workspace_id="<id>", merge_id="<merge-id>", limit=40))
ue_sync("resolve", options=dict(workspace_id="<id>", merge_id="<merge-id>",
    conflict_id="<conflict-id>", choice="ours", dry_run=False))
ue_sync("continue", options=dict(workspace_id="<id>", merge_id="<merge-id>", dry_run=False))
```

Use the conflict's `allowed_resolutions` for supported choices. Custom values
must use its typed representation. `abort` cancels an unresolved operation;
original files remain available. A changed source, target or schema produces
a stale session with preserved evidence. Refresh and create a new merge.

## History actions

| Action | Inputs and behavior |
|---|---|
| `log` | `revision`, `limit`, `cursor`, `author`; commits retain real parent links |
| `show` | `revision`, or `merge_id` / `apply_id`; page full version or conflict data |
| `diff` | `left`, `right` using revisions, `HEAD`, `index`, `files`; filter paths/entity/field |
| `blame` | `asset`, `field_path`, optional `revision`; traces semantic field ancestry |
| `branch` | List, or create with `name`, `revision`; removal requires `delete`, `expected` |
| `switch` | `name`; protects dirty workspaces and uses ref CAS |
| `tag` | `name`, `revision`, `message`; replacement requires the expected tag ref |
| `restore` | `revision`, paths or `entity`/`field_path`; `destination=files/index/both` |
| `revert` | `revision`; reverses that commit's delta while retaining unrelated later edits |
| `cherry-pick` | `revision`; applies the selected commit's delta onto local HEAD |
| `reset` | `revision`, `mode=soft/mixed/hard`; private history, with a safety ref |
| `amend` | `message`, optional `all`; replaces a private tip with a new commit |
| `rebase` | `onto`, optional `steps` with pick/squash/drop; resumable cursor |
| `stash` | `mode=push/list/apply/pop/drop`, `message` / `stash_id`; preserves all three layers |
| `reflog` | `ref`, `before`, `limit`; find old commits after private history changes |

Reverting or cherry-picking a merge commit requires explicit `mainline`.
Use `continue`/`abort` with `rebase_id` for a rebase. Published history is
append-only: restore old state or revert a commit, then commit/push the result.
Asset and entity deletion requires `allow_delete=True`; staging a missing file
also requires explicit `delete=True`.

## Execution receipts and recovery

Publication captures current UE memory, including unsaved edits, computes a
merged candidate, and derives only its remaining delta. Native apply checks
editor epoch, target revision and dependency read set before mutation. It
records package checkpoints, save steps and a durable receipt under
`<Project>/.nexus/collaboration/transactions/<apply_id>/`.

```python
ue_sync("recover", options=dict(workspace_id="<id>", apply_id="<apply-id>"))
ue_sync("recover", options=dict(workspace_id="<id>", apply_id="<apply-id>",
    restore=True, dry_run=False))
```

Query the recorded phase before deciding how to recover. A committed receipt
publishes the saved result without replaying create operations. Conflicting
external edits return `recovery_conflict`. Multi-asset batches can partially
complete; inspect `rows`, `errors` and the per-asset apply IDs. New file edits
during publication return `workspace_rebase_required`; pull preserves and
replays their layers.

## Working at project scale

Each observation records the memory revision it measured per asset. The next
one re-exports only what the editor reports as dirty or newly saved, so
publishing one edit in a ten-thousand asset project exports nothing and applies
one asset. `status`, `stage` and `commit` re-encode only the files whose bytes
changed, using `workspaces/<id>/captures.json`; that file is derived state and
can be deleted at any time at the cost of one full re-read. Local commands take
only their own workspace lock, so agents serialize on `push` alone.

## Schema and migration

The model and lint share `Content_Transcoded/.nexus/schema/<schema_key>/`.
Read `index.md`, then a category index and the requested JSON. Categories are
blueprint, material, niagara, scene, asset and common; contexts are separate
records within the same schema. Records distinguish engine availability from
bridge inspect/create/write/delete support and mark unresolved dynamic rules.

```python
ue_sync("schema", options=dict(category="blueprint", query="CallFunction", details=True))
ue_sync("schema", options=dict(function="KismetSystemLibrary.PrintString"))
ue_sync("schema", options=dict(target="/Game/M/MI_A.MI_A"))
ue_sync("schema", options=dict(workspace_id="<id>", revision="<commit-id>"))
```

`refresh=True` collects current facts. Supplied unevaluated dynamic conditions
remain `context_required`; cached offline results report unknown freshness.
Snapshots pin their consumed schema records for historical interpretation.

First checkout imports legacy bases and preserves original local bytes in the
`imported` workspace. Legacy recovery evidence is retained in the migration
record. Before activation, legacy init/pull/push retain their old contract.
After activation, use workspace IDs and explicit resolutions. Keep a backup
of `.nexus/collaboration` when preserving local history; these generated local
files are excluded from source Git. Git CLI interoperability and remote
repository transport are outside this local version store.
