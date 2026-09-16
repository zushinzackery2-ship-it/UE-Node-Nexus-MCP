"""Bounded project memory observations inform future launch reservations."""

import time

MAX_PROFILES = 256


class Resources:
    def __init__(self, service):
        self.service = service
        self.profiles = dict((row["project_key"], row) for row in service.registry.all("project_resource"))

    def observe(self, instance: dict) -> None:
        value = instance.get("private_working_set_bytes", 0)
        if not value:
            return
        key = instance["project_key"]
        profile = self.profiles.get(key)
        if profile and value <= profile["peak_private_working_set_bytes"]:
            return
        profile = dict(project_key=key, peak_private_working_set_bytes=value, observed_at=time.time())
        self.profiles[key] = profile
        self.service.registry.put("project_resource", key, profile)
        if len(self.profiles) > MAX_PROFILES:
            oldest = min(self.profiles.values(), key=lambda item: item["observed_at"])["project_key"]
            self.profiles.pop(oldest)
            self.service.registry.delete("project_resource", oldest)

    def estimate(self, project_key: str) -> int:
        return self.profiles.get(project_key, dict()).get("peak_private_working_set_bytes", 0)
