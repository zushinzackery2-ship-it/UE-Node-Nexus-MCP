"""Bound inactive process history without forgetting durable request outcomes."""

import time

MAX_EXITED = 256
RETENTION_SECONDS = 7 * 86400


def prune(service) -> None:
    with service.lock:
        protected = set(service.editors.jobs)
        protected.update(lease["instance_id"] for lease in service.sessions.leases.values())
        protected.update(scope["instance_id"] for scope in service.scopes.active.values())
        protected.update(scope["instance_id"] for scope in service.scopes.queue.values())
        rows = sorted((item for item in service.instances.values()
                       if item["state"] == "EXITED" and item["instance_id"] not in protected),
                      key=lambda item: item.get("exited_wall_time", 0), reverse=True)
        now = time.time()
        removed = [item for index, item in enumerate(rows)
                   if index >= MAX_EXITED or now - item.get("exited_wall_time", now) > RETENTION_SECONDS]
        service.registry.delete_many("instance", [item["instance_id"] for item in removed])
        for item in removed:
            service.instances.pop(item["instance_id"])
            members = service.projects[item["project_key"]]
            members.discard(item["instance_id"])
            if not members:
                service.projects.pop(item["project_key"])
        if removed:
            service.event("history_pruned", instances=len(removed), retained=len(rows) - len(removed))
