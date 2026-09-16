import os
import time
import uuid

import pytest

from ue_node_nexus_mcp.instances.broker.service import BrokerService
from ue_node_nexus_mcp.instances.lifecycle.policy import Policy
from tests.instances.support.platform import Clock, Platform


class Client:
    def __init__(self, service):
        self.service = service
        self.identifier = uuid.uuid4().hex
        self.call("register", client_session_id=self.identifier, workspace=self.identifier,
                  process_created=service.platform.inspect(os.getpid())["process_created"])

    def call(self, action, **payload):
        result = self.service.dispatch(dict(protocol=1, action=action, payload=payload,
                                            client_session_id=self.identifier), os.getpid())
        return result

    def require(self, action, **payload):
        result = self.call(action, **payload)
        deadline = time.monotonic() + 5
        while result.get("error", dict()).get("code") == "manager_busy" and time.monotonic() < deadline:
            assert result["error"]["details"]["submitted"] is False
            time.sleep(0.025)
            result = self.call(action, **payload)
        assert result["ok"], result
        return result["data"]

    def ready(self, path):
        result = self.require("ensure", project_path=str(path), mode="reuse_or_start", dry_run=False)
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            self.service.reconcile()
            status = self.require("status", instance_id=result["instance"]["instance_id"])
            if status["state"] == "READY":
                return result
            time.sleep(0.005)
        pytest.fail("test editor did not become ready")


@pytest.fixture
def lifecycle(tmp_path):
    clock, platform = Clock(), Platform()
    service = BrokerService(tmp_path / "runtime", platform, Policy(), clock)
    service.reconcile()
    project = tmp_path / "Demo.uproject"
    project.touch()
    yield service, platform, clock, project
    service.editors.workers.shutdown(wait=True, cancel_futures=True)
    service.registry.close()
