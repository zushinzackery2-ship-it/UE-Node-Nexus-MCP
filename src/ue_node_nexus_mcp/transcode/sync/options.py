"""Validate the selected mirror protocol before acquiring editor ownership."""

from ..collaboration.report.options import COMMON, OPTIONS, validate as validate_collaboration, validate_values
from ..errors import SyncError

LEGACY_KEYS = dict(
    init=set(("pull_all", "include_stubs", "auto_export", "refresh_schema", "scene")),
    status=set(("discover", "include_stubs", "include_clean", "scene")),
    pull=set(("discover", "include_stubs", "force", "scene")),
    lint=set(),
    push=set(("dry_run", "compile", "save", "force", "allow_delete", "stop_on_error", "scene")),
    schema=set(),
)
OPTION_KEYS = dict((action, keys | OPTIONS.get(action, set()) | COMMON) for action, keys in LEGACY_KEYS.items())
OPTION_KEYS.update((action, keys | COMMON) for action, keys in OPTIONS.items() if action not in OPTION_KEYS)


def validate(action, options, project=None):
    collaboration = bool(options.get("workspace_id") or project and (project / ".nexus/collaboration/active.json").is_file())
    if action != "init" and (collaboration or action not in LEGACY_KEYS):
        validate_collaboration(action, options)
        return
    unknown = set(options) - OPTION_KEYS[action]
    if unknown:
        raise SyncError("invalid_option", "unsupported options", dict(action=action, unknown=sorted(unknown),
                                                                      allowed=sorted(OPTION_KEYS[action])))
    validate_values(action, options, force_values=("local", "ue"))
