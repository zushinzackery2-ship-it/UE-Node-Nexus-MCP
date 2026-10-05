"""Persist exit evidence, allowing late CrashReporter files to finish writing."""

import logging
import time

from .reports import collect

LOG = logging.getLogger(__name__)


def observe(instance: dict, now: float | None = None) -> bool:
    now = time.time() if now is None else now
    previous = instance.get("exit_evidence", dict())
    if previous.get("state") == "complete" or now < previous.get("next_check_at", 0):
        return False
    end = instance.setdefault("exited_wall_time", now)
    try:
        reports = collect(instance, end) if instance.get("pid") and instance.get("process_created") else dict(
            fatal=None, ensures=[], errors=[dict(message="process creation identity is unavailable")])
    except (OSError, ValueError) as error:
        reports = dict(fatal=None, ensures=[], errors=[dict(message=str(error)[:1024])])
    fatal = reports["fatal"]
    artifacts_ready = fatal is not None and fatal.get("dump_bytes", 0) > 0 and bool(fatal.get("log_path"))
    finished = artifacts_ready or now >= end + 30
    evidence = dict(reports, state="complete" if finished else "pending", checked_at=now,
                    next_check_at=now + 2, exit_code=instance.get("exit_code"))
    if reports["fatal"]:
        evidence["exit_kind"] = "crash"
        instance["error"] = dict(code="editor_crashed", message=reports["fatal"]["error_message"],
                                 details=dict(context_path=reports["fatal"]["context_path"],
                                              dump_path=reports["fatal"].get("dump_path")))
    elif finished:
        evidence["exit_kind"] = "normal" if instance.get("exit_code") == 0 else "abnormal"
    else:
        evidence["exit_kind"] = "pending"
    instance["exit_evidence"] = evidence
    if finished:
        LOG.info("instance=%s exit=%s kind=%s reports=%s", instance["instance_id"], instance.get("exit_code"),
                 evidence["exit_kind"], len(reports["ensures"]) + int(reports["fatal"] is not None))
    return True
