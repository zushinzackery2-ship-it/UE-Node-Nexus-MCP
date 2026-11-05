"""A released private worker must save through its original instance binding."""

import threading

from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.instances.lifecycle.engine import compatible
from ue_node_nexus_mcp.instances.session import binding
from ue_node_nexus_mcp.safety.isolation.session import ValidationSession


def test_private_worker_reacquires_exact_instance_after_release(tmp_path, monkeypatch):
    project = tmp_path / "Project/Sample.uproject"
    engine = tmp_path / "EngineRoot"
    worker = dict(instance_id="private-worker", project_path=str(project), state="READY",
                  guard_protocol=1, contract_version=4, launch_profile="offscreen", rhi="d3d12")
    calls = []
    manager = object.__new__(binding.EditorSession)
    manager.lock = threading.RLock()
    manager.binding, manager.lease, manager.options = {}, {}, {}
    manager.explicit_instance = False
    manager.configured_project, manager.startup_wait = str(project), 1
    manager.on_bind_changed = None
    manager.start = lambda: None
    manager.broker = type("Broker", (), dict(workspace=str(tmp_path), epoch="manager-epoch",
                                            supports=lambda self, name: True))()
    manager.poll = lambda: worker
    manager.list_instances = lambda **options: [worker]
    monkeypatch.setattr(binding, "resolve_project", lambda *args: dict(project_path=str(project)))
    monkeypatch.setattr(binding.startup, "hold", lambda poll, row, wait: (row, dict(outcome="ready")))

    def broker_call(operation, payload):
        calls.append((operation, dict(payload)))
        if operation == "release":
            return {}
        assert operation == "ensure"
        if manager.binding:
            compatible(worker, {key: value for key, value in payload.items() if key != "engine_path"})
        return dict(instance=dict(worker), lease=dict(lease_id="private-lease"), action="created")

    manager.call = broker_call
    session = object.__new__(ValidationSession)
    session.manager, session.project, session.engine = manager, project, engine
    session.timeout, session.allow_low_memory, session.rhi = 30, True, "d3d12"
    session.expected = {"Nexus": dict(binary=str(engine / "Engine/Binaries/Win64/Nexus.dll"))}

    def bridge_call(operation, payload):
        if operation == "project_context_get":
            return dict(ok=True, data=dict(project_file_path=str(project)))
        if operation == "bridge_capabilities_get":
            return dict(ok=True, data=dict(rhi="d3d12", build={"Nexus": dict(
                loaded=True, build_id="engine", module_path="Nexus.dll")}))
        assert operation == "asset_save"
        try:
            instance, _ = manager.acquire()
            return dict(ok=True, data=dict(instance_id=instance["instance_id"]))
        except InstanceError as error:
            return error.envelope()

    session.call = bridge_call
    session.start()
    manager.release()
    saved = session.call("asset_save", dict(asset_path="/Game/DirtyCopiedMap"))
    assert saved["ok"], saved
    assert saved["data"]["instance_id"] == worker["instance_id"]
    assert calls[-1][1]["instance_id"] == worker["instance_id"]
    assert calls[-1][1]["mode"] == "reuse_only"
