"""Explicit, validated operating limits shared by all clients."""

from dataclasses import asdict, dataclass, fields
import json
import logging
import math
from pathlib import Path

from ..errors import InstanceError
from ..broker.registry import atomic_json

LOG = logging.getLogger(__name__)

PROTOCOL_VERSION = 1
# Optional request fields added within this protocol. A manager ignores fields it
# does not know, so a client sends one only to a manager that announces it.
FEATURES = frozenset(("allow_low_memory",))
LIVE_STATES = frozenset(("STARTING", "READY", "IDLE", "DRAINING", "STOPPING", "UNRESPONSIVE", "BLOCKED"))


@dataclass(frozen=True)
class Policy:
    heartbeat_seconds: float = 15
    suspect_seconds: float = 60
    idle_seconds: float = 300
    grace_seconds: float = 120
    sweep_seconds: float = 10
    resource_seconds: float = 30
    startup_seconds: float = 240
    shutdown_seconds: float = 60
    broker_idle_seconds: float = 60
    recovery_seconds: float = 60
    # A pin is renewed by pinning again, but a single hold has to outlast the
    # work it protects: reading engine source or chasing one compile error keeps
    # an editor busy far longer than it keeps the manager busy.
    max_pin_seconds: float = 14400
    max_startups: int = 1
    min_free_gib: float = 4
    min_free_ratio: float = 0.15
    per_client_queue: int = 16
    per_project_queue: int = 64
    max_queue: int = 256

    def __post_init__(self) -> None:
        integers = ("max_startups", "per_client_queue", "per_project_queue", "max_queue")
        for name, value in asdict(self).items():
            if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
                raise InstanceError("invalid_policy", f"{name} must be positive")
            if name in integers and not isinstance(value, int):
                raise InstanceError("invalid_policy", f"{name} must be an integer")
        if not 0 < self.min_free_ratio < 1:
            raise InstanceError("invalid_policy", "min_free_ratio must be between zero and one")

    @classmethod
    def parse(cls, values: dict) -> tuple["Policy", dict]:
        """Validate stored options without touching the file; also returns them without retired fields."""
        current = dict((key, value) for key, value in values.items() if key != "max_editors")
        unknown = set(current) - set(item.name for item in fields(cls))
        if unknown:
            raise InstanceError("invalid_policy", "unknown policy options", dict(options=sorted(unknown)))
        return cls(**current), current

    @classmethod
    def read(cls, root: Path) -> "Policy":
        path = root / "policy.json"
        values = json.loads(path.read_text(encoding="utf-8")) if path.is_file() else dict()
        policy, current = cls.parse(values)
        if current != values:
            atomic_json(path, current)
            LOG.info("policy_option_retired option=max_editors path=%s", path)
        return policy
