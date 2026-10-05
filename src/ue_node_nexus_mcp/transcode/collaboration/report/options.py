"""Action-specific options keep mistakes from becoming silent mutations."""

from ...errors import SyncError
import math

READ_ACTIONS = set(("status", "workspaces", "diff", "log", "show", "blame", "reflog", "lint", "schema"))
COMMON = set(("workspace_id", "project", "agent_id", "dry_run", "proposal_id"))
OPTIONS = dict(
    checkout=set(("revision", "branch", "scene", "include_stubs")),
    workspaces=set(), status=set(("include_clean", "discover")),
    fetch=set(("discover", "include_stubs", "scene")),
    stage=set(("delete", "allow_delete")), unstage=set(),
    commit=set(("message", "all", "delete", "allow_delete")), amend=set(("message", "all", "delete", "allow_delete")),
    merge=set(("revision", "source", "message")), pull=set(("message", "discover")),
    resolve=set(("merge_id", "conflict_id", "conflict_ids", "choice", "value", "name", "asset", "conflict_type", "layer", "all")),
    abort=set(("merge_id", "rebase_id", "apply_id")),
    push=set(("revision", "source", "merge_id", "compile", "save", "allow_delete", "stop_on_error", "replace", "force", "safety_allow_low_memory")),
    recover=set(("apply_id", "projection_id", "restore", "preserve_current", "resolution")), close=set(),
    branch=set(("name", "revision", "delete", "expected")), switch=set(("name",)),
    tag=set(("name", "revision", "message", "expected")),
    log=set(("revision", "limit", "cursor", "author", "entity", "since", "until")),
    show=set(("revision", "merge_id", "apply_id", "part", "limit", "cursor")),
    diff=set(("left", "right", "entity", "field", "merge_id", "limit", "cursor")),
    blame=set(("revision", "asset", "field_path")), reflog=set(("ref", "limit", "before")),
    stash=set(("mode", "stash_id", "message")),
    restore=set(("revision", "destination", "entity", "field_path", "allow_delete")),
    revert=set(("revision", "mainline", "message", "allow_delete")),
    reset=set(("revision", "mode", "allow_delete")),
    rebase=set(("onto", "steps")), lint=set(("files_root",)),
    schema=set(("refresh", "category", "query", "details", "function", "target", "context", "limit", "cursor", "revision")),
)
OPTIONS["cherry-pick"] = set(OPTIONS["revert"])
OPTIONS["continue"] = set(("merge_id", "rebase_id", "message", "allow_delete", "compile", "save", "stop_on_error", "force", "safety_allow_low_memory"))
BACKGROUND_ACTIONS = frozenset(("checkout", "fetch", "pull", "push", "continue", "recover", "status", "lint", "schema"))
JOB_ACTIONS = frozenset(("job_status", "job_result", "job_cancel"))
TRANSPORT_KEYS = frozenset(("background", "wait_seconds", "deadline_seconds", "idempotency_key"))
for action in BACKGROUND_ACTIONS:
    OPTIONS[action].update(TRANSPORT_KEYS)
for action in JOB_ACTIONS:
    OPTIONS[action] = set(("job_id",))
READ_ACTIONS.update(("job_status", "job_result"))
ACTIONS = tuple(OPTIONS)
BOOL_KEYS = set(("dry_run", "all", "delete", "allow_delete", "include_clean", "discover", "include_stubs", "compile", "save", "stop_on_error", "refresh", "details", "restore", "preserve_current", "background", "safety_allow_low_memory"))
STRING_LIST_KEYS = set(("conflict_ids",))


def validate(action: str, options: dict) -> None:
    unknown = set(options) - OPTIONS[action] - COMMON
    if unknown:
        raise SyncError("invalid_option", "unsupported options", dict(action=action, unknown=sorted(unknown), allowed=sorted(OPTIONS[action] | COMMON)))
    if "force" in options and options["force"] != "local":
        raise SyncError("invalid_option", "force must be 'local' to publish the selected source over UE state")
    if options.get("force") and options.get("merge_id"):
        raise SyncError("invalid_option", "force=local publishes the source directly; abort the existing merge session first",
                        dict(abort=dict(action="abort", options=dict(merge_id=options["merge_id"], dry_run=False))))
    for key in BOOL_KEYS & options.keys():
        if type(options[key]) is not bool:
            raise SyncError("invalid_option", f"{key} must be a boolean")
    for key in ("wait_seconds", "deadline_seconds"):
        if key in options and (type(options[key]) not in (int, float) or not math.isfinite(options[key]) or options[key] < 0):
            raise SyncError("invalid_option", f"{key} must be a finite nonnegative number")
    if options.get("wait_seconds", 0) > 60:
        raise SyncError("invalid_option", "wait_seconds must be at most 60; poll job_status for longer work")
    for key in ("idempotency_key", "job_id"):
        if key in options and (not isinstance(options[key], str) or not 0 < len(options[key]) <= 128):
            raise SyncError("invalid_option", f"{key} must contain 1–128 characters")
    if action in JOB_ACTIONS and not options.get("job_id"):
        raise SyncError("invalid_option", "job_id is required")
    for key in STRING_LIST_KEYS & options.keys():
        value = options[key]
        if not isinstance(value, list) or not all(isinstance(item, str) and item for item in value):
            raise SyncError("invalid_option", f"{key} must be a list of non-empty strings")
    for key in ("limit", "cursor", "mainline", "before"):
        if key in options and key not in ("cursor",) and (type(options[key]) is not int or options[key] < 0):
            raise SyncError("invalid_option", f"{key} must be a nonnegative integer")
    if options.get("delete") and not options.get("allow_delete") and action not in ("branch",):
        raise SyncError("delete_not_allowed", "explicit file deletion requires allow_delete=true")


def mutates(action: str, options: dict) -> bool:
    if action in READ_ACTIONS:
        return False
    if action in ("branch", "tag") and not options.get("name"):
        return False
    return not (action == "stash" and options.get("mode", "list") == "list")
