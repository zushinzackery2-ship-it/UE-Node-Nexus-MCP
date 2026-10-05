# Diagnostics and repair loops

`diagnostics_get` combines retained PIE/MessageLog, script and RHI events with
loaded Blueprint (including AnimBlueprint), MaterialInterface and MaterialFunction
compile state. It inspects loaded objects without compiling or loading assets;
`assets_not_loaded` and `assets_unsupported` describe coverage. Explicit `asset_compile`
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

Runtime events preserve message source, severity, asset, node/GUID, graph and
function when UE provides those tokens. Identical events aggregate with
`occurrence_count`, first/last times and sequence. `error_count`/`warning_count`
include all matching session occurrences even after a cursor consumes their rows;
`returned_error_count`/`returned_warning_count` describe this page's rows.

The default session is the most recent PIE, retained after PIE ends; before the
first PIE it is the editor session. `runtime.session_id` identifies it. Use an
explicit `session_id` for another retained session; an expired ID reports
`runtime_session_not_found`. Operation notices also expose `current_session_id`
and `editor_error_count` so post-PIE editor events remain visible.

Use `cursor=runtime.next_cursor` for incremental reads and `limit=1..2000` for
compile rows (runtime rows cap at 512). A repeated event can appear again with
its updated lifetime occurrence count. Retention caps at 16 sessions, 512 event
aggregates and about 2 MiB of records; text truncates at 4096 characters.
`dropped_count`, `cursor_gap` and `asset_counts_complete` expose lost evidence
or the 128-asset per-session counter budget. `coverage=partial` requires reviewing
these fields, unloaded/unsupported assets and unattributed events before treating
a filtered zero count as conclusive. `include_assets=false` reads runtime only.

`ue_read(target="diagnostics", format="detail")` returns complete diagnostics;
large responses use bounded artifact pages and preserve counts in their summary.
Other summary modes include three example records and a detailed follow-up call.

`diagnostics_get` also attaches `data.related_log_items`: historical material,
PIE, script, PSO and Fatal clues mined from the selected editor log. They carry
`stale_possible=true` and stay separate from current session counts. Set
`include_history=false` to skip the extra context/log read. For raw log lines, use
`log_tail_get` (or `ue_read(target="log")`) with a `match` substring filter —
an offline read of the bound instance's exact log; an unbound project selects its
newest log. A missing bound log reports `log_not_found` with its expected path.

## Connection and contract triage

1. `ue_context_get()` — is an instance bound? Which groups are enabled?
2. `ue_execute("bridge_instance_list", {})` — inspect ownership, users and state.
   Use `bridge_instance_ensure` with the exact `.uproject`; STARTING, busy and
   unresponsive processes keep their identity. A start that does not finish is
   told apart by status: `waiting_for_user` names the prompt (`blocking_dialog`,
   also for hidden windows), a moving `startup_progress.last_log` is a long load,
   and a still one with no prompt is a stall. See the `instances` guide.
3. `ue_execute("bridge_contract_check", {})` — Python contract vs. operations
   actually loaded in the UE bridge; catches a stale plugin or server build.
4. `ue_execute("project_context_get", {}, response={"mode":"full"})` — project
   path, content dir, `/Game` mount.

`bridge_capabilities_get` includes `build` entries for Guard, Core and VFX: embedded
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

`safety_validate` executes explicit asset operations in a fresh project copy and
a separate editor. Its schema lists `project_path`, `operations`, `engine_path`,
`rhi`, `timeout_seconds`, `dry_run` and `allow_low_memory`. A preview measures saved inputs without
creating a copy; execution uses `dry_run=false`. Use `task_submit` to run it in
the background. The caller's selected editor is never rebound.

The input snapshot covers saved Content, Config, project plugins/modules,
shaders and tools. Live queues, caches and journals are excluded; an explicitly
selected project-relative scene plan is copied separately. Linked inputs and
external project/plugin roots are rejected. Mutation targets must remain in the
copied `/Game` mount; shared engine/plugin packages are never saved by cleanup.
References can use installed enabled content plugins, including `/Niagara`.
Unreal descriptors accept comments and trailing commas; malformed descriptors
remain errors. Worker publication generates private transaction identities and
forces compilation, save and a matching native receipt/exported raw result.

Validation records the input digest, loaded plugin identities and engine BuildId,
every request/result, actual source-file checks and confirmed worker exit. The
receipt and copy remain inspectable after failure. Success requires a confirmed
worker exit with code 0; abnormal or unknown exit codes fail validation and retain
the instance's crash evidence. A stalled validation worker
is cancelled only after its project and process creation identity are verified;
forced cancellation is reported as failure. This is process/project isolation,
not an OS sandbox for arbitrary native project plugins.

Explicit validation uses `baseline=saved_disk`. High-risk checkout publications
automatically validate a serialized live-memory baseline before the main apply,
preserving source disk bytes and dirty state. Candidate, dependency revisions,
engine/loaded plugin/configuration identity and measured RHI must match; inspect
the published row's `safety_validation`. Ordinary material values use the normal
transaction. The main editor still verifies current revisions and readback.
Low-memory startup requires explicit user authorization: `allow_low_memory=true`
for explicit validation or `safety_allow_low_memory=true` for one push/continue.

Niagara parameter reads and existing-value writes validate the category, byte
size/range and object slot. Vector/quaternion values use the engine byte-copy
contract, including LWC conversion, so packed offsets need no typed alignment.
Invalid storage returns the parameter name, type and specific reason through
export and publication. Validation receipts expose copied bytes and separate
preflight/copy/startup/execution/exit/source-check timings.

For actual gameplay/rendering acceptance, include `level_open(map_path="/Game/...")`
and `runtime_smoke_start` in the validation operations after applying the candidate.
Smoke runs 1..30 simulated seconds and 1..600 frames on the requested D3D12/D3D11
backend, verifies a rendered PNG, ends PIE and checks retained runtime errors/loss.
`runtime_smoke_status(smoke_id=...)` reads its durable receipt, including structured
events and capture evidence. Smoke requires the validation copy's worker marker;
calling it in an ordinary editor reports `runtime_worker_required`.

After process exit, `bridge_instance_status` returns `exit_evidence`: fatal and
ensure reports, context/dump/log paths, bounded thread stacks and Fatal/PSO lines.
It also retains runtime and portable call stacks, D3D12 validation messages and
GC reference-chain arrows (including Python collector roots). Log evidence reads
at most the final 256 KiB and reports its offset, whole-log coverage and retained
line losses; an empty subset cannot establish absence outside that window.
Reports are correlated by project, PID, creation time and Nexus instance identity.
The collector retries delayed artifacts every two seconds for up to 30 seconds.
`last_editor_snapshot` preserves the final live sample; EXITED snapshots expose
`stale=true`, `live_state=false` and cleared active compiling/PIE/saving blockers.

The plugin and editor share an address space. Lifecycle ordering prevents
bridge-originated reentrancy and premature completion claims; it does not make
engine assertions or GPU driver faults recoverable. A dedicated UE worker can
contain automation failures in a disposable project copy. Main-editor loading
still creates its own RHI resources and retains engine/driver risk.

Transport requests execute before world ticking through the editor frame queue.
This prevents `level_open` from releasing a tick-active level while the TaskGraph
is pumping tasks; see the `concurrency` guide for queue and shutdown behavior.

An empty ComputePSO assertion identifies the failing state, not its producer.
Root-cause attribution requires a reproducible state transition with matching
DLL fingerprints, symbols and rendering backend. Keep compile-only validation
separate from a real DX12 replay; NullRHI cannot validate that rendering path.

## Response scopes and runtime acceptance

Every facade keeps `operation_diagnostics` separate from `runtime_diagnostics`.
`ok` describes the requested operation; a successful compile or publication can
therefore retain a failed PIE session. Runtime notices include instance/session
identity, `sampled_at`, cumulative counts, coverage, at most three severe error
samples and a `next_read` route. Samples prioritize fatal/error before warnings.
Auto reads, artifact pages, failed operations, sync results and task/job polling
preserve these fields. Stored observations report `live_state=false`.

Filtered diagnostics use `matched_error_count` for the named asset; the session's
`error_count` remains cumulative. An empty cursor page retains earlier errors.
`remaining_errors` is emitted only by an actual asset postcheck and includes its
asset/stage scope. Offline actions report `runtime_observed=false` and `unknown`.
Publication records `runtime_verification=not_run` until a runtime gate is run.

To accept a project-owned test, capture the active PIE `session_id` and
`instance_id`, run its functional checks, end PIE, then call:

```python
ue_execute("runtime_verification_get", dict(
    session_id="the-test-pie-session", instance_id="the-editor-instance",
    functional_passed=True))
```

The gate fails on observed runtime errors or failed functional checks. A changed
identity, active PIE, incomplete `sources_complete`/asset counts, lost events or
cursor gap produces `inconclusive`; neither state passes acceptance. Historical
errors from a different PIE remain separately inspectable.

Scripts running inside UE can use
`unreal.NexusRuntimeDiagnosticsLibrary.runtime_diagnostics_json("")` to capture
the active session, then `runtime_verification_json(session_id, instance_id,
functional_passed)` after PIE ends. These calls read copied diagnostic values
directly and can run inside editor callbacks without a synchronous pipe request.

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
