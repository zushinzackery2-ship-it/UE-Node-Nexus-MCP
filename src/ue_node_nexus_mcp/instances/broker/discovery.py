"""Promote OS-only observations when the same process publishes its Guard identity."""


def accept(service, observed: dict) -> bool:
    identity = ("pid", "process_created", "executable", "project_key")
    peers = [item for item in service.instances.values()
             if item["instance_id"] != observed["instance_id"] and item["state"] != "EXITED"
             and all(item.get(key) == observed.get(key) for key in identity)]
    verified = observed.get("guard_protocol") == 1
    if not verified:
        return not any(item.get("guard_protocol") == 1 for item in peers)
    for item in peers:
        identifier = item["instance_id"]
        if item.get("guard_protocol") or item["ownership"] != "external":
            continue
        if service.sessions.for_instance(identifier) or service.scopes.for_instance(identifier):
            service.event("identity_conflict", instance_id=identifier, observed_instance_id=observed["instance_id"])
            return False
        service.registry.delete("instance", identifier)
        service.instances.pop(identifier)
        service.projects[item["project_key"]].discard(identifier)
        service.event("discovery_verified", provisional_instance_id=identifier,
                      instance_id=observed["instance_id"], pid=observed["pid"])
    return True
