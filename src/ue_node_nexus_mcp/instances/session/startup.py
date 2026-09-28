"""Hold a call while the editor it needs starts, and say how the start ended.

A start takes minutes and can stop at a prompt nobody sees; answering STARTING
and leaving the caller to poll made a loading editor, a waiting prompt and a
stall look the same. The call is held instead, in bounded slices: one slice
stays inside a common MCP client tool timeout, and the answer names the next
call when the start needs longer.
"""

from __future__ import annotations

import time

from ..errors import InstanceError

DEFAULT_WAIT_SECONDS = 45.0
MAX_WAIT_SECONDS = 900.0
POLL_SECONDS = 1.0
SETTLED = frozenset(("READY", "IDLE", "BLOCKED"))
ERRORS = dict(exited=("startup_failed", "the editor exited before it became ready"),
              unresponsive=("startup_timeout", "the editor did not become ready and shows no prompt"),
              waiting_for_user=("waiting_for_user", "the editor is waiting for an answer to a prompt"),
              starting=("instance_starting", "the same editor is still starting"))
# An ensure reports a start still loading or waiting on a prompt; only these end it.
ENDED = ("exited", "unresponsive")


def budget(value, default: float = DEFAULT_WAIT_SECONDS) -> float:
    if value is None:
        return default
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not 0 <= value <= MAX_WAIT_SECONDS:
        raise InstanceError("invalid_request", f"wait_seconds must be a number from 0 to {MAX_WAIT_SECONDS:g}",
                            dict(field="wait_seconds", received=value))
    return float(value)


def outcome(row: dict) -> str:
    state = row.get("state")
    if state in SETTLED:
        return "ready"
    if state == "EXITED":
        return "exited"
    if state == "UNRESPONSIVE":
        return "unresponsive"
    return "waiting_for_user" if row.get("waiting_for_user") else "starting"


def pause(seconds: float) -> None:
    time.sleep(seconds)


def hold(poll, row: dict, seconds: float, clock=time.monotonic) -> tuple[dict, dict]:
    """Observe ``row``'s instance until it settles, a prompt waits, or ``seconds`` pass.

    Anything short of ready is observed again first: a remembered prompt may
    have been answered since, and a remembered start may have finished.
    """
    started = clock()
    while outcome(row) != "ready":
        row = poll(row["instance_id"])
        if outcome(row) != "starting" or clock() - started >= seconds:
            break
        pause(POLL_SECONDS)
    return row, report(row, clock() - started)


def report(row: dict, waited: float) -> dict:
    result = dict(outcome=outcome(row), waited_seconds=round(waited, 3), state=row.get("state"))
    for key in ("blocking_dialog", "exited_while_waiting", "exit_code", "error"):
        if row.get(key) not in (None, ""):
            result[key] = row[key]
    acknowledged = [item for item in row.get("dialog_notices") or () if item.get("resolution") == "auto_acknowledged"]
    if acknowledged:
        result["acknowledged_dialogs"] = acknowledged
    if row.get("startup_progress"):
        result["progress"] = row["startup_progress"]
    if result["outcome"] == "starting":
        result["next"] = "still loading; call bridge_instance_ensure again with the same arguments to keep holding"
    elif result["outcome"] == "waiting_for_user":
        result["next"] = "an editor prompt needs an answer: show blocking_dialog to the user, then call bridge_instance_ensure again"
    return result


def warnings(report_: dict) -> list[str]:
    """What the caller must see even when it reads only the envelope."""
    result = [f"editor dialog acknowledged: {item.get('title')}: {item.get('meaning') or item.get('message', '')}"
              for item in report_.get("acknowledged_dialogs", ())]
    if report_["outcome"] == "waiting_for_user":
        dialog = report_.get("blocking_dialog") or dict()
        result.append(f"editor is waiting for an answer to: {dialog.get('title', '')}")
    return result


def require(row: dict, report_: dict, outcomes=tuple(ERRORS)) -> None:
    """Raise the error of a start whose outcome is one of ``outcomes``."""
    if report_["outcome"] in outcomes:
        code, message = ERRORS[report_["outcome"]]
        raise InstanceError(code, message, dict(instance=row, startup=report_))
