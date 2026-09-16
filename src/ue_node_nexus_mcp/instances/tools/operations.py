"""Typed operation signatures are the lifecycle payload schema source."""

from typing import Literal

from .dispatch import execute


def bridge_instance_list(project_path: str | None = None, include_exited: bool = False) -> dict:
    return execute("bridge_instance_list", locals())


def bridge_instance_status(instance_id: str | None = None, project_path: str | None = None) -> dict:
    return execute("bridge_instance_status", locals())


def bridge_instance_ensure(project_path: str | None = None, mode: Literal["reuse_only", "reuse_or_start"] = "reuse_only",
                           launch_profile: Literal["interactive", "offscreen"] | None = None, engine_path: str | None = None,
                           rhi: Literal["d3d12", "d3d11", "nullrhi"] | None = None, instance_id: str | None = None,
                           idempotency_key: str | None = None, mirror_root: str | None = None,
                           dry_run: bool = True, proposal_id: str | None = None) -> dict:
    return execute("bridge_instance_ensure", locals())


def bridge_instance_select(pid: int | None = None, project: str | None = None,
                           project_path: str | None = None, instance_id: str | None = None) -> dict:
    return execute("bridge_instance_select", locals())


def bridge_instance_release(lease_id: str | None = None) -> dict:
    return execute("bridge_instance_release", locals())


def bridge_instance_close(instance_id: str | None = None, generation: int | None = None,
                          save_packages: list[str] | None = None, dry_run: bool = True, proposal_id: str | None = None) -> dict:
    return execute("bridge_instance_close", locals())


def bridge_instance_reap(project_path: str | None = None, instance_id: str | None = None, dry_run: bool = True) -> dict:
    return execute("bridge_instance_reap", locals())


def bridge_instance_adopt(instance_id: str, project_path: str, process_created: str, reason: str,
                          protected: bool = True, dry_run: bool = True, proposal_id: str | None = None) -> dict:
    return execute("bridge_instance_adopt", locals())


def bridge_instance_pin(instance_id: str, reason: str, seconds: float,
                        dry_run: bool = True, proposal_id: str | None = None) -> dict:
    return execute("bridge_instance_pin", locals())
