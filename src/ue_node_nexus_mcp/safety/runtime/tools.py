from ...runtime import call_bridge


def runtime_verification_get(session_id: str, instance_id: str, functional_passed: bool) -> dict:
    """Require functional success and complete diagnostics from the named PIE window."""
    from .verification import verify

    return verify(call_bridge, session_id, instance_id, functional_passed)


def runtime_smoke_start(duration_seconds: float = 2, min_frames: int = 30,
                        timeout_seconds: float = 120, dry_run: bool = True) -> dict:
    """Admit a bounded PIE run and verified rendered frame in a safety_validate worker."""
    return call_bridge("runtime_smoke_start", dict(duration_seconds=duration_seconds, min_frames=min_frames,
                       timeout_seconds=timeout_seconds, dry_run=dry_run))


def runtime_smoke_status(smoke_id: str) -> dict:
    """Read the durable PIE/frame result for one smoke validation."""
    return call_bridge("runtime_smoke_status", dict(smoke_id=smoke_id))
