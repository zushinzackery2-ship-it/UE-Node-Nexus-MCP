"""An actual MCP server with one saved apply and its first recovery response lost."""

import json
import os
from pathlib import Path

from ue_node_nexus_mcp import runtime, server
from ue_node_nexus_mcp.bridge import UeBridgeClient
from ue_node_nexus_mcp.transport import named_pipe_transport


class LostResponses:
    def __init__(self):
        self.path = Path(os.environ["NEXUS_TEST_RESPONSE_TRACE"]).resolve()
        self.path.relative_to((Path(__file__).resolve().parents[3] / "build").resolve())
        self.enabled = os.environ.get("NEXUS_TEST_DROP_RESPONSES") == "1"
        self.apply_id = None
        self.recovery_dropped = False

    def send(self, target, envelope, timeout):
        response = named_pipe_transport.send(target, envelope, timeout)
        operation, payload = envelope["operation"], envelope["payload"]
        collaboration_apply = operation == "transcode_apply" and payload.get("collaboration_version") == 1
        recovery = operation == "transcode_recover"
        if not collaboration_apply and not recovery:
            return response
        receipt = (response.get("data") or dict()).get("receipt") or dict()
        drop = self.enabled and collaboration_apply and self.apply_id is None
        if drop:
            self.apply_id = payload["apply_id"]
        elif self.enabled and recovery and payload.get("apply_id") == self.apply_id and not self.recovery_dropped:
            drop = self.recovery_dropped = True
        event = dict(operation=operation, request_id=envelope["request_id"], apply_id=payload.get("apply_id"),
                     response_dropped=drop, native_ok=response.get("ok"), native_phase=receipt.get("phase"))
        with self.path.open("a", encoding="utf-8") as output:
            output.write(json.dumps(event) + "\n")
        if drop:
            raise OSError("acceptance fixture discarded the completed native response")
        return response


if __name__ == "__main__":
    runtime.bridge = UeBridgeClient(transport=LostResponses())
    server.main()
