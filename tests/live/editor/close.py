"""Wait for the manager's declared recovery window before closing an owned editor."""

import time

from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.instances.session.shutdown import close_instance


def close_owned(session, identifier, saved_packages=()):
    policy = session.broker.policy
    deadline = time.monotonic() + policy["recovery_seconds"] + 2 * policy["sweep_seconds"] + 15
    while True:
        session.status(dict(instance_id=identifier, fresh=True))
        preview = session.call("close", dict(instance_id=identifier, dry_run=True))
        blockers = preview["blockers"]
        if not blockers:
            return close_instance(session, dict(instance_id=identifier, dry_run=False))
        waiting_saved_sample = (set(blockers) == set(("instance_dirty",))
                                and set(preview["instance"].get("dirty_packages", [])) <= set(saved_packages))
        if set(blockers) != set(("recovery_quarantine",)) and not waiting_saved_sample:
            raise InstanceError("close_blocked", "owned editor has active close blockers", preview)
        if time.monotonic() >= deadline:
            raise InstanceError("close_blocked", "owned editor close prerequisites did not settle", preview)
        time.sleep(.5)
