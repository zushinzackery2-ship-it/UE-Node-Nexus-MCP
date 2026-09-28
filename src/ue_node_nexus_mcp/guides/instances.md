# Shared editor instances

Version 0.6.0 / bridge contract 4 shares one editor for each physical `.uproject`
across MCP processes and workspaces belonging to the same Windows user.
The user-level manager arbitrates starts and usage. The early UE Guard holds
the project's process lock. Asset payloads still travel directly to UE.
The manager survives the MCP session that first started it. On Windows it is
created under the signed-in user's desktop shell, so an MCP host's process-tree
cleanup cannot terminate a shared editor belonging to other workspaces.

## Connect, work, release

1. Configure the exact `.uproject` with `--project` or `UE_NEXUS_PROJECT_PATH`.
   An explicit `project_path` in ensure takes precedence. cwd provides candidates;
   ambiguous candidates return `project_required`.
2. Read `ue_context_get()` and the schema of `bridge_instance_ensure`.
3. When starting the project is authorized, ensure it with `dry_run=False`:

   ```python
   ue_execute("bridge_instance_ensure", dict(
       project_path="D:/UEProjects/Demo/Demo.uproject",
       mode="reuse_or_start", launch_profile="interactive", rhi="d3d12",
       dry_run=False))
   ```

   The default mode is `reuse_only`. `reuse_or_start` authorizes starting this
   project when absent. Supply `engine_path` if EngineAssociation cannot resolve
   an installed engine. `dry_run=True` provides an optional preview and a
   `proposal_id` that binds execution to it. Every new managed editor opens a
   visible window for user interaction and preview; `interactive` is the default.
   Visible starts use `d3d12` (default) or `d3d11`.
4. An executed ensure is held while the editor starts: it returns once the
   editor is ready, a prompt waits for its user, the process exits, or
   `wait_seconds` pass (default 45, at most 900). Its `startup` report names the
   `outcome` (`ready`, `waiting_for_user`, `starting`, `exited`, `unresponsive`),
   `waited_seconds`, the editor's `progress` (phase, launch window, latest log
   line), `acknowledged_dialogs`, the `blocking_dialog` and the `next` call. A
   first asset or `ue_sync` call on a starting editor is held the same way, and
   `bridge_instance_status` accepts `wait_seconds`. All callers share the same
   launch `operation_id` and instance identity.
5. Read `project_context_get`, then use the normal asset and `ue_sync` workflow.
   Each agent checks out its own workspace in the returned shared `mirror_root`.
6. Finish with `ue_execute("bridge_instance_release", dict())`.
   This releases this MCP session's leases; pending work keeps its scope until
   UE completion is verified. Normal MCP shutdown also ends its registration.
   The editor window stays available to the user. Use explicit close when the
   task includes closing the editor; normal work needs no manual pin or renewal.

Do not launch another editor to resolve a timeout, busy state or repository
conflict. Inspect the returned instance, blockers and operation first. A task
already submitted stays bound to its original instance even after selection changes.

## Startup prompts

The Guard records every UE message dialog from PostConfigInit on. A start that
nobody can see — launched by Nexus, launched hidden (`Start-Process -WindowStyle
Hidden`) or offscreen — has its OK-only advisories confirmed by the Guard:
UE's "Low Drive Space" warning (engine and project directories under 5120 MB,
user settings directory under 1024 MB) continues the start, and the confirmation
is reported in `dialog_notices` and the ensure warnings with each location's
path, free and recommended MB. Every other prompt, and every prompt of a visible
external start, waits for its user:

| Field | Meaning |
|---|---|
| `waiting_for_user` | A prompt holds the editor; the startup timeout does not apply |
| `blocking_dialog` | `title`, `message`, `buttons`, `category`, `code`, `opened_at`; `source=window` for a native dialog outside UE's handler |
| `windows` | Titled windows of the process, hidden ones included, with `visible`, `class_name` and dialog text |
| `dialog_notices` | The last 16 prompts with `resolution` (`auto_acknowledged` or `answered`) and `result` |
| `startup_progress` | Phase, launch window (`normal`, `hidden`, `offscreen`) and the latest log line with its time |

Show the `blocking_dialog` to the user and call ensure again once it is answered;
the same instance continues. While any modal dialog is open, bridge requests are
refused with `waiting_for_user` and the dialog in `details`. An editor that exits
while a prompt waits reports it as `exited_while_waiting`. A write that fails
for lack of space is an error, never an advisory, and is not confirmed.

## Ownership and cleanup

| State or ownership | Behavior |
|---|---|
| Legacy background `managed`, clean, no users/work | Normal exit after the idle grace period |
| Existing `external` editor | Reused when compatible; retains user ownership |
| Interactive managed editor | Protected from automatic reclamation |
| `STARTING` | Join the existing launch; ensure and work calls are held until it settles |
| `DRAINING` | Acquire cancels the uncommitted close before granting a lease |
| `STOPPING` | Wait for actual OS process exit before starting again |
| `UNRESPONSIVE` | Retains identity and capacity; investigate logs/status |
| Dirty packages, PIE, compile/save or pending recovery | Blocks close and reports reasons |

Heartbeats indicate client liveness. They do not renew editor usage. Context,
list, status and log polling also leave usage deadlines unchanged. Log tails use
the selected/configured project and read `Saved/Logs` and `Saved/Nexus/Logs`
directly, including after editor reclamation. Defaults are:

| Policy | Default |
|---|---:|
| Heartbeat / process reconciliation | 15 s / 10 s |
| Lease idle expiry / exit grace | 300 s / 120 s |
| Resource sampling | 30 s |
| Start / close wait (a waiting prompt pauses the start wait) | 240 s / 60 s |
| Call held on a start: default / maximum `wait_seconds` | 45 s / 900 s |
| Recovery quarantine / idle manager exit | 60 s / 60 s |
| Managed editors / simultaneous starts | 2 / 1 |
| Required free physical memory | max(4 GiB, 15% of physical RAM) |
| Waiting scopes per client / project / total | 16 / 64 / 256 |

Policy is shared through `%LOCALAPPDATA%/UE-Node-Nexus-MCP/Runtime/policy.json`.
Use field names returned in `bridge_instance_list.policy`, such as `max_editors`,
`max_startups`, `idle_seconds`, `grace_seconds`, `min_free_gib` and `min_free_ratio`.
All values must be positive; unknown fields are rejected. Changes take effect
when an idle manager restarts. Different workspaces must use the same runtime.
`UE_NEXUS_RUNTIME_DIR` exists for isolated installations and tests.
For a previously observed project, admission also reserves its recorded peak
private working set above that free-memory floor. Up to 256 project estimates
are retained. Unknown projects use the floor and the serialized startup slot.
The manager limits blocking work to two requests and keeps control queries and
release available. `manager_busy` means the operation was not submitted; the SDK
retries this specific pre-admission response within a bounded five-second window.

No automatic path saves all packages, discards edits or terminates a live editor.
An exit is confirmed only after its exact PID/creation identity has exited.
A process crash is distinguishable from normal exit by its `exit_code`.

## Explicit maintenance

Discover exact payloads with `ue_capability_get(operation=..., detail="schema")`.

| Operation | Use |
|---|---|
| `bridge_instance_list/status` | Identity, ownership, windows, startup progress and prompts, users, work, resources and logs; status holds with `wait_seconds` |
| `bridge_instance_select` | Select an exact `project_path` / `instance_id`; legacy PID/name must be unambiguous |
| `bridge_instance_reap` | Preview eligible managed instances and each blocker; apply with `dry_run=False` |
| `bridge_instance_close` | `dry_run=False` waits for actual process exit; name any packages explicitly intended for saving |
| `bridge_instance_adopt` | Transfer an external editor into management using exact instance/project/process creation identity |

Ensure, close, reap and adopt default to optional previews. Ensure does not adopt.
Adoption remains protected by default; use an explicit `protected=False` only
when that editor should become eligible for automatic cleanup. Other users and
in-flight work always block explicit close. Save failure returns failed packages
and keeps the editor running. A completed close returns `exit_confirmed=True`,
`state=EXITED` and `exit_code`. `wait=False` submits an asynchronous close and
returns `exit_confirmed=False`; poll status to confirm completion. A blocked close
or shutdown timeout reports the live instance and its blockers.

`bridge_instance_pin` is hidden from ordinary discovery. It remains callable for
legacy background editors, with a reason and duration up to `max_pin_seconds`
(default 14400). It applies immediately unless `dry_run=True`. Calling it for a
windowed editor reports its existing protection and `requires_renewal=False`
without changing the instance generation. Active work scopes protect themselves.

The legacy `editor_request_exit` routes through the same manager checks.
Its old `force` and blanket save behavior are rejected. Ordinary lifecycle
operations cannot be nested in `batch_execute` or `task_submit`.
To replace a legacy offscreen editor, select its exact instance, finish/release
work, close with `dry_run=False`, then ensure the project. New offscreen launches
are rejected with the supported profile in the error.

## Shared repository and offline work

The existing UE `Saved/Nexus/collaboration-binding.json` takes precedence.
Without an existing binding, the first explicit `mirror_root` /
`UE_NEXUS_TRANSCODE_DIR` wins; otherwise the default is
`<Project>/Saved/Nexus/Content_Transcoded`. All subsequent workspaces receive
that same repository. A different explicit root returns `repository_mismatch`
with the existing location. Preserve its history and create a separate checkout.

Lease expiry or editor reclamation leaves workspace files, index, commits,
conflicts and recovery receipts intact. `lint`, `stage`, `commit`, history and
cached schema can run offline without starting UE. Schema refresh/function/target
queries and live publication use editor scopes. `push/recover` retain their scope
through observation, apply, saving and receipt convergence.

## Errors and evidence

| Code | Action |
|---|---|
| `project_required`, `instance_ambiguous` | Choose an exact returned project/instance |
| `instance_missing` | Use authorized `reuse_or_start`, or connect to an existing editor |
| `waiting_for_user` | Show `startup.blocking_dialog` / `details.dialog` to the user; call again once it is answered |
| `startup_failed` | The editor exited before it was ready; read `exit_code`, `exited_while_waiting` and the editor log |
| `startup_timeout` | Alive without a prompt past the start wait; read `startup_progress.last_log`, `windows` and the log |
| `instance_starting`, `instance_stopping` | The hold ended while still loading or stopping; call again; retain the project target |
| `stale_instance` | The explicitly selected process ended; make a new explicit selection |
| `instance_unverified` | Inspect the identified process, permissions, startup and Guard installation |
| `instance_incompatible`, `manager_version_mismatch` | Install matching builds and drain the old version |
| `instance_in_use`, `instance_dirty` | Inspect users, scopes and named packages |
| `capacity_exceeded` | Inspect occupancy/free RAM; wait for eligible reclamation |
| `operation_outcome_unknown` | Inspect state/receipts; a possibly executed write is not replayed |
| `manager_response_unknown` | Query status; retry ensure with the same idempotency key |
| `manager_launch_unavailable` | Check the signed-in user's desktop shell and process access; inspect the returned Win32 error |

An ensure `idempotency_key` is scoped to the MCP session, retained at least 24
hours, and must keep identical parameters. A completed launch key cannot start
a new editor. The ledger holds at most 4096 keys; create a new key for a new task.

The manager writes `Runtime/Logs/lifecycle.jsonl` (10 MiB × 4 files).
Automatic bootstrap requires an accessible desktop shell belonging to the same
Windows user. A client in a noninteractive session can connect to an existing
manager. For such a host, run `ue-node-nexus-manager` in an independent user
terminal before connecting; the manager retains its normal idle-exit policy.
MCP sessions write independent logs: one 1 MiB active file and three backups.
Each session reserves 5 MiB within a 256 MiB total budget, with seven-day inactive
retention. `retention-status.json` records
cleanup decisions. Exited instance history retains at most 256 inactive entries
or seven days; live processes and unresolved work are retained.

Status records expose working set, private commit and private working set
separately. Summing working sets is not a measure of unique physical memory.

## Upgrade

Install a matching 0.6.0 Wheel and both Bridge modules: `UeNodeNexusBridge` and
`UeNodeNexusGuard`; VFX is optional. All required DLLs need the same engine
BuildId and contract 4, and main/Guard need the same source fingerprint.
The manager validates the installed Guard before launch and UE reports loaded
module identities at runtime. Existing editors retain their current DLLs until
the user finishes work and restarts them. An old editor never triggers a duplicate
managed launch merely because it lacks the new control pipe.

Guard also covers ordinary `UnrealEditor-Cmd` sessions. Genuine `-run=...`
commandlets bypass the editor lifetime gate. A duplicate guarded Editor exits
early with code 73; failed project identity/lock access exits with code 74.
