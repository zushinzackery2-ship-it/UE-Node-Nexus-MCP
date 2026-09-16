from concurrent.futures import ThreadPoolExecutor
import json
import logging
import os
import threading

import pytest

from ue_node_nexus_mcp.coordination.file_lock import LockBusy
from ue_node_nexus_mcp.instances.broker import client as module
from ue_node_nexus_mcp.instances.broker.main import EventFormatter
from ue_node_nexus_mcp.instances.errors import InstanceError


@pytest.mark.parametrize("failure", [OSError("denied"), ValueError("malformed state"), LockBusy(dict(lock="start.lock"))])
def test_bootstrap_errors_have_a_lifecycle_envelope(tmp_path, monkeypatch, failure):
    def failed(_root):
        raise failure

    monkeypatch.setattr(module, "ensure_manager", failed)
    client = module.BrokerClient(tmp_path)
    with pytest.raises(InstanceError) as caught:
        client.call("list")
    assert caught.value.code == "manager_unavailable"
    assert not client.registered
    client.close()


def test_close_serializes_with_a_reconnect_and_prevents_new_registration(tmp_path, monkeypatch):
    entered, resume = threading.Event(), threading.Event()
    actions = []

    def bootstrap(_root):
        entered.set()
        assert resume.wait(5)

    def send(_target, request, _timeout):
        actions.append(request["action"])
        return dict(ok=True, manager_epoch=1, data=dict(policy=dict(), instances=[]))

    monkeypatch.setattr(module, "ensure_manager", bootstrap)
    monkeypatch.setattr(module.named_pipe_transport, "send", send)
    client = module.BrokerClient(tmp_path)
    with ThreadPoolExecutor(max_workers=2) as workers:
        request = workers.submit(client.call, "list")
        assert entered.wait(3)
        closing = workers.submit(client.close)
        assert not closing.done()
        resume.set()
        request.result(timeout=3)
        closing.result(timeout=3)
    assert actions == ["register", "list", "client_end"]
    with pytest.raises(InstanceError) as caught:
        client.call("heartbeat")
    assert caught.value.code == "session_closed"
    assert not client.registered


@pytest.mark.parametrize("submitted", [False, True])
def test_only_explicit_pre_admission_rejection_is_retried(tmp_path, monkeypatch, submitted):
    calls = []

    def send(_target, request, _timeout):
        if request["action"] == "register":
            return dict(ok=True, data=dict(policy=dict()))
        calls.append(request["action"])
        if len(calls) == 1:
            return dict(ok=False, error=dict(code="manager_busy", message="busy", details=dict(submitted=submitted)))
        return dict(ok=True, data=dict(instances=[]))

    monkeypatch.setattr(module, "ensure_manager", lambda _root: None)
    monkeypatch.setattr(module.named_pipe_transport, "send", send)
    client = module.BrokerClient(tmp_path)
    if submitted:
        with pytest.raises(InstanceError):
            client.call("ensure")
        assert calls == ["ensure"]
    else:
        assert client.call("ensure") == dict(instances=[])
        assert calls == ["ensure", "ensure"]
    client.close()


@pytest.mark.parametrize("message", ["plain warning", '{"event":"ready","instance_id":"example"}', "x" * 20000])
def test_lifecycle_log_always_contains_bounded_json(message):
    record = logging.LogRecord("lifecycle", logging.WARNING, __file__, 1, message, (), None)
    value = json.loads(EventFormatter().format(record))
    assert value["event"] in ("ready", "manager_diagnostic")
    assert len(value.get("message", "")) <= 16384


@pytest.mark.skipif(os.name != "nt", reason="Windows process handles")
def test_exit_between_discovery_and_refresh_retains_exact_exit_code(tmp_path):
    import subprocess
    import sys
    from ue_node_nexus_mcp.instances.identity.processes import inspect_process
    from ue_node_nexus_mcp.instances.identity.watch import ProcessWatch
    from ue_node_nexus_mcp.instances.lifecycle.platform import WindowsPlatform

    platform = WindowsPlatform(tmp_path)
    process = subprocess.Popen([sys._base_executable, "-c", "import time; time.sleep(0.2); raise SystemExit(7)"],
                               creationflags=subprocess.CREATE_NO_WINDOW)
    identity = dict(inspect_process(process.pid), instance_id="owned-exit-test")
    try:
        platform.watches[identity["instance_id"]] = ProcessWatch(identity)
        platform.processes[identity["instance_id"]] = process
        assert process.wait(timeout=5) == 7
        assert platform.exit_result(identity) == dict(exit_code=7)
        assert platform.exit_result(identity) == dict(exit_code=7)
        assert not platform.watches and not platform.processes
    finally:
        platform.close()
