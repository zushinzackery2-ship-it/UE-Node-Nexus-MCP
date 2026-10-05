"""Await native PIE shutdown and independently verify the produced frame."""

import time

from ...instances.errors import require
from ..capture.status import status as capture_status
from ...diagnostics.contracts.verdict import runtime_verdict


def wait_for_result(session, accepted: dict) -> dict:
    identifier = accepted["data"]["smoke_id"]
    deadline = time.monotonic() + session.timeout
    while time.monotonic() < deadline:
        response = session.call("runtime_smoke_status", dict(smoke_id=identifier))
        data = response.get("data", dict())
        if response.get("ok") is not True and not data.get("done"):
            return response
        if data.get("done"):
            if not data.get("ok"):
                return response
            image = data.get("capture", dict())
            require(image.get("capture_protocol") == 1, "isolation_identity_mismatch", "smoke frame lacks capture protocol")
            verified = capture_status(image["file_path"], image["capture_id"])
            require(verified.get("ok") and verified["data"].get("state") == "completed",
                    "isolated_validation_failed", "smoke rendered frame failed independent verification", capture=verified)
            require(data.get("rhi").lower() == session.rhi, "isolation_identity_mismatch", "smoke ran on a different RHI")
            verdict = runtime_verdict(data["runtime"], session_id=data.get("session_id"),
                                      instance_id=data.get("instance_id"), functional_passed=True)
            data["runtime_verification"] = verdict
            require(verdict["passed"], verdict["code"], "PIE diagnostics did not establish a complete clean run", verdict=verdict)
            data["verified_capture"] = verified["data"]
            return response
        time.sleep(0.1)
    require(False, "isolation_runtime_timeout", "PIE validation did not produce a terminal result")
