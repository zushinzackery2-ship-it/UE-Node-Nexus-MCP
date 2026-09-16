"""Read-only native admission probes and control-path latency on an owned host."""

import argparse
import json
import math
from pathlib import Path
import time
import uuid

from ue_node_nexus_mcp.bridge import UeBridgeClient
from ue_node_nexus_mcp.instances.broker.client import BrokerClient
from ue_node_nexus_mcp.instances.session.binding import EditorSession
from ue_node_nexus_mcp.transport import named_pipe_transport
from .host import ROOT


def distribution(values):
    ordered = sorted(values)
    return dict(samples=len(values), p50_ms=ordered[len(ordered) // 2],
                p95_ms=ordered[math.ceil(len(ordered) * 0.95) - 1], max_ms=ordered[-1])


def measure(callback, count):
    samples = []
    for _ in range(count):
        started = time.perf_counter()
        callback()
        samples.append((time.perf_counter() - started) * 1000)
    return distribution(samples)


def reject(response, code):
    assert not response.get("ok") and response.get("error", dict()).get("code") == code, response


def run(root: Path):
    root = root.resolve()
    root.relative_to((ROOT / "build").resolve())
    project = root.parent / "NexusValidation.uproject"
    assert project.is_file() and (root / "manager.json").is_file()
    session = EditorSession(BrokerClient(root, str(root.parent)), str(project))
    report = dict(project=str(project), assertions=[])
    try:
        session.ensure(dict(dry_run=False))
        bridge = UeBridgeClient(instances=session)
        report["builds"] = bridge.call("bridge_capabilities_get", dict())["data"]["build"]
        instance = session.current()
        identifier = instance["instance_id"]
        report["identity"] = dict(instance_id=identifier, pid=instance["pid"], process_created=instance["process_created"])
        target = instance["target"]
        with session.work_scope("protocol_probe") as scope:
            def envelope(operation="project_context_get"):
                return dict(operation=operation, request_id=uuid.uuid4().hex, payload=dict(), lifecycle=scope.metadata())

            def send(request):
                return named_pipe_transport.send(target, request, 5)

            first = envelope()
            assert send(first)["ok"]
            reject(send(first), "operation_outcome_unknown")
            report["assertions"].append("duplicate request identity rejected")
            request = envelope("level_open")
            reject(send(request), "exclusive_scope_required")
            request = envelope("level_current_get")
            request["lifecycle"]["context_epoch"] = -1
            reject(send(request), "stale_context")
            report["assertions"].extend(["exclusive native operations reject shared grants", "stale context rejected"])
            for field, replacement in (("instance_id", "wrong"), ("client_session_id", "wrong"), ("manager_epoch", -1)):
                request = envelope()
                request["lifecycle"][field] = replacement
                reject(send(request), "stale_scope")
            report["assertions"].append("instance/client/manager identity validated")
            malformed = [(dict(), "invalid_envelope"),
                         (dict(operation="editor_request_exit", request_id=uuid.uuid4().hex, payload=dict()), "lease_required"),
                         (dict(operation="project_context_get", request_id=42, payload=dict(), lifecycle=scope.metadata()), "invalid_envelope")]
            for request, code in malformed:
                reject(send(request), code)
            report["assertions"].append("unscoped old exit and malformed metadata cannot enter the queue")
            control = "\\\\.\\pipe\\UeNodeNexusLifecycle." + str(instance["pid"])
            for request in (dict(), dict(operation="status", payload=[])):
                reject(named_pipe_transport.send(control, request, 5), "invalid_request")
            reject(named_pipe_transport.send(control, dict(operation="prepare_close", payload=dict()), 5), "stale_manager")
            report["assertions"].append("malformed and unauthenticated native control rejected")
            assert bridge.call("project_context_get", dict())["ok"]
        released = session.status()
        assert released["state"] == "READY"
        report["latency"] = dict(
            list=measure(lambda: session.list_instances(), 200),
            status=measure(lambda: session.status(), 1000),
            reuse=measure(lambda: session.ensure(dict(dry_run=False)), 200))

        def admitted():
            with session.work_scope("admission_benchmark"):
                pass

        report["latency"]["admission_release"] = measure(admitted, 300)
        assert report["latency"]["list"]["p95_ms"] <= 50, report["latency"]
        assert report["latency"]["status"]["p95_ms"] <= 50, report["latency"]
        assert report["latency"]["reuse"]["p95_ms"] <= 300, report["latency"]
        assert report["latency"]["admission_release"]["p95_ms"] <= 10, report["latency"]
        report["ok"] = True
        print(json.dumps(dict(ok=True, assertions=report["assertions"], latency=report["latency"])), flush=True)
    finally:
        session.close()
        (root.parent / "protocol-result.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True)
    run(parser.parse_args().runtime)
