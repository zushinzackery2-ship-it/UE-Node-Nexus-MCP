# Local collaboration and version history

Version 0.6.0 / bridge contract 4 shares the editor and repository across MCPs.
Use one mirror root per UE project and one workspace per agent. Each workspace
has an independent HEAD, index and files directory. The editor serializes final
publication; independent local edits and commits stay isolated.
Call `bridge_instance_ensure` for the exact project and use its returned
`mirror_root`. Existing UE bindings are preserved; a conflicting explicit root
returns `repository_mismatch`. Release the editor after work; files and history
remain available for offline lint/stage/commit. See the `instances` guide.

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

For work on a few assets, check out only those paths: the workspace answers
`sparse: true` and holds them plus what they reference. A push merges and checks
exactly its selection, and its dry run computes the same merge session the real
push opens, so the previewed conflicts are the session's conflicts. Without
`paths`, a sparse workspace publishes its projected files and every asset its
branch carries beyond the editor's history (a merge or deletion included), never
the inherited rest of the project. An asset the push does not change is taken as
UE holds it and is not validated again. A failure names its `asset` and `stage`
(`observe`, `merge`, encoding `section`/`entity`/`line`).

Every revision a push asks UE to verify, the target and each referenced asset,
is measured from the editor rather than taken from memory, and measured again
after an earlier apply of the same push compiled. `stale_target` then means
another writer; `details.stale` names the asset, its role and both revisions.

High-risk publication first runs complete isolation: Niagara changes, material
Custom/WPO/depth/interpolator contracts and MaterialFunction interface changes.
The worker compiles, saves and exports through its own guarded native transaction.
Admission requires matching candidate and dependencies, live package baseline,
engine/loaded plugin/configuration identity, measured RHI and normal exit code 0.
The result row and apply record retain `safety_validation`; failed or stale proof
prevents the publishing editor's apply. Ordinary material values use the normal
transaction directly. High-risk publication requires `compile=true`.

Baseline serialization preserves the publishing editor's dirty flags and disk
bytes. Explicit `safety_validate` uses saved disk inputs; automatic publication
uses `baseline=live_memory`. Resource admission remains enabled. After explicit
user authorization, `safety_allow_low_memory=true` on this push/continue permits
that invocation's workers to start below the memory threshold. A legacy high-risk
push reports `safety_workspace_required`; use checkout and commit its candidate.

`fetch` observes UE without moving workspace layers. `pull` integrates that
version and replays staged and unstaged changes separately. `status` reports
both layers, branch movement, active conflicts, pending applies and file paths.
`workspaces` reads metadata and pending-record summaries without scanning files.
Inspect one workspace with `status`; use `show(merge_id=...)` or `show(apply_id=...)`
for an individual record's complete evidence. Repository format 2 indexes record
ownership and lifecycle state; existing repositories migrate once on first open.

MaterialFunction interfaces are resolved from the complete candidate during
planning and replanning. Verified function receipts update the dynamic catalog.
Each selected caller rebuilds its call nodes before applying its final wiring.
Unselected callers are listed in `deferred_callers` for a later publication and
retain their unsaved state. A same-id node replacement keeps its identity,
connections and position while taking properties/defaults from its new class.

## Long workflows

Editor workflows return within 30 seconds by default. If work is still running,
the answer contains a durable `job_id` and an exact `next` call. Set
`background=True` for immediate submission or `wait_seconds=0..60` to change the
wait. A queued push fixes its source commit at submission; later commits remain
available for a separate publication. `idempotency_key` binds retries to one job.

```python
ue_sync("push", options=dict(workspace_id="<id>", dry_run=False, background=True))
ue_sync("job_status", options=dict(job_id="<job-id>"))
ue_sync("job_result", options=dict(job_id="<job-id>"))
ue_sync("job_cancel", options=dict(job_id="<job-id>"))
```

Status reports the current stage/asset and completed assets. Results live in the
repository and can be read from another MCP session. Cancellation and optional
`deadline_seconds` stop between atomic asset operations; committed receipts stay
published. An exited worker owner reports `interrupted` with recovery instructions.
Queued jobs are cancelled when their MCP session closes. Running writes retain
their normal receipt and recovery protocol. Capability and polling queries remain
available while the workflow runs.

Publication reports each change's `origin` (`ue` or `workspace`) and its
`comparison.before` / `comparison.after` roles. `ue_drift_adopted` explains when
UE changes enter the candidate; an empty plan means UE already has those values.
To publish committed workspace values over UE drift, use `push` with
`force="local"`. This still performs validation, revision checks and readback.
It starts a fresh publication; abort an existing `merge_id` before using force.
Explicit default values and omitted defaults have the same effective semantics.

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
must use its typed representation. A value over 2 KiB is carried as
`{"state": "elided", "bytes", "digest"}`; `ours`/`theirs`/`base` still resolve it
from the snapshot the conflict names. Page a large conflict list with
`ue_read(target="artifact", query={"artifact_id", "path": "data.conflicts"})`.
`abort` cancels an unresolved operation; original files remain available. A
changed source or target produces a stale session with preserved evidence.
A changed schema does not strand work: merges re-read older states under the
current schema, and a session opened before the change answers `stale_session`
with ready `abort` and `retry` calls; re-running the push re-reads everything.

## History actions

| Action | Inputs and behavior |
|---|---|
| `log` | `revision`, `limit`, `cursor`, `author`; commits retain real parent links |
| `show` | `revision`, or `merge_id` / `apply_id`; historical asset rows contain full `.nexus` text |
| `diff` | `left`, `right` using revisions, `HEAD`, `index`, `files`; asset paths and current/historical workspace filenames are accepted |
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
records package checkpoints, save steps and a native receipt under
`<Project>/Saved/Nexus/Collaboration/<apply_id>/`. The publisher record and
exported result live in `<repository>/transactions/<apply_id>/`.

```python
ue_sync("recover", options=dict(workspace_id="<id>", apply_id="<apply-id>", resolution="inspect"))
ue_sync("recover", options=dict(workspace_id="<id>", apply_id="<apply-id>",
    resolution="restore", dry_run=False))
```

Query the recorded phase before deciding how to recover. A committed receipt
publishes the saved result without replaying create operations. Conflicting
external edits return `recovery_conflict`. Multi-asset batches can partially
complete; inspect `rows`, `errors` and the per-asset apply IDs. New file edits
during publication return `workspace_rebase_required`; pull preserves and
replays their layers.

Before publication, requested writable fields and connections are checked
against the native receipt. `apply_result_mismatch` includes the asset, file,
line and differing fields. The unpublished result is restored automatically
when its memory and disk guards still match; the local commit remains retryable.
A failed create restores an absent-package checkpoint, including sidecar files.
Existing packages reload at their original paths so material instances,
Blueprint instances and active maps retain valid references. Restoration keeps
pre-apply memory, original disk bytes and the prior dirty flag independently.
UE's native reload resets editor undo history; the receipt records
`restore_method=package_reload` and `undo_history_reset` when reload is required.
Published receipts are immutable history and require a new revert publication.
Failed restoration preserves dirty state and reports `recovery_conflict` or
`recovery_required` with its durable evidence.

`continue` with a publication's `merge_id` completes the resolved push and accepts
its preview's `proposal_id`; a separate push is optional.

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
Reflection family names such as `component` and `material_expression` also work.
`schema_key` reports the current catalog used by the action;
`workspace_schema_key` reports the workspace's bound historical version.
Offline actions return `bridge_contacted=False` and leave bridge availability
unmeasured. Schema availability is measured after first checkout exports it.

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
