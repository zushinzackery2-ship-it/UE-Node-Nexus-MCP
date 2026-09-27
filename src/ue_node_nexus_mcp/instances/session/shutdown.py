"""A public close completes only after the manager observes process exit."""

import time

from ..errors import InstanceError


def close_instance(session, payload):
    request = dict(payload)
    wait = request.pop("wait", True)
    result = session.call("close", request)
    if request.get("dry_run", True):
        return result
    identifier = result["instance_id"]
    if session.current().get("instance_id") == identifier:
        session.lease = dict()
    if result.get("exit_confirmed"):
        return result
    if not wait:
        return dict(result, exit_confirmed=False)
    deadline = time.monotonic() + session.broker.policy.get("shutdown_seconds", 60)
    while True:
        state = session.status(dict(instance_id=identifier))
        if state["state"] == "EXITED":
            return dict(result, state="EXITED", exit_confirmed=True, exit_code=state.get("exit_code"), instance=state)
        if state["state"] in ("BLOCKED", "UNRESPONSIVE"):
            raise InstanceError("close_blocked" if state["state"] == "BLOCKED" else "instance_unresponsive",
                                "editor process is still alive; inspect the close blockers",
                                dict(instance=state, exit_confirmed=False))
        if time.monotonic() >= deadline:
            raise InstanceError("shutdown_timeout", "editor process has not exited before the close deadline",
                                dict(instance=state, exit_confirmed=False))
        time.sleep(0.2)
