"""Public acceptance gate for a project-owned, explicitly identified PIE run."""

from ...contracts import require_non_empty_string
from ...diagnostics.contracts.notice import normalize
from ...diagnostics.contracts.verdict import runtime_verdict


def verify(bridge, session_id: str, instance_id: str, functional_passed: bool) -> dict:
    require_non_empty_string(session_id, "session_id")
    require_non_empty_string(instance_id, "instance_id")
    if not isinstance(functional_passed, bool):
        raise ValueError("functional_passed must be a boolean")
    response = bridge("diagnostics_get", dict(session_id=session_id, include_assets=False,
        include_history=False, limit=3, severity="error"))
    if response.get("ok") is not True:
        return dict(response, operation="runtime_verification_get", runtime_verification="inconclusive")
    runtime = (response.get("data") or dict()).get("runtime") or dict()
    verdict = runtime_verdict(runtime, session_id=session_id, instance_id=instance_id,
        functional_passed=functional_passed)
    result = dict(ok=verdict["passed"], operation="runtime_verification_get",
        data=dict(runtime_verification=verdict, runtime=runtime, functional_passed=functional_passed),
        runtime_verification=verdict, runtime_diagnostics=normalize(runtime), diagnostics=[], warnings=[])
    if not verdict["passed"]:
        result["error"] = dict(code=verdict["code"], message="PIE acceptance " + verdict["status"], details=verdict)
    return result
