import os
import time

from ue_node_nexus_mcp.instances.lifecycle.crashes.observe import observe
from ue_node_nexus_mcp.instances.lifecycle.crashes.reports import collect


def instance(tmp_path):
    now = time.time()
    return dict(project_path=str(tmp_path / "ShoudunUE.uproject"), pid=21768, instance_id="geometry",
                process_created=str(int((now - 20 + 11_644_473_600) * 10_000_000)), exit_code=3,
                exited_wall_time=now)


def report(tmp_path, name="fatal", pid=21768, ensure=False):
    directory = tmp_path / "Saved/Crashes" / name
    directory.mkdir(parents=True)
    (directory / "CrashContext.runtime-xml").write_text(
        '<FGenericCrashContext><RuntimeProperties>'
        f'<ProcessId>{pid}</ProcessId><GameName>UE-ShoudunUE</GameName>'
        f'<IsEnsure>{str(ensure).lower()}</IsEnsure><CrashType>{"Ensure" if ensure else "Assert"}</CrashType>'
        '<MemoryStats.bIsOOM>0</MemoryStats.bIsOOM>'
        '<ErrorMessage>Shader compilation failures are Fatal.</ErrorMessage>'
        '</RuntimeProperties><Threads><Thread><IsCrashed>true</IsCrashed>'
        '<ThreadName>Background Worker #9</ThreadName><CallStack>UnrealEditor-RHI + 4ba7a</CallStack>'
        '</Thread></Threads></FGenericCrashContext>')
    (directory / "UEMinidump.dmp").write_bytes(b"minidump")
    (directory / "geometry.log").write_text('LogD3D12RHI: Error: Failed to create pipeline state, error 80070057.\n')
    return directory


def test_fatal_and_nonfatal_ensure_are_kept_separate(tmp_path):
    item = instance(tmp_path)
    report(tmp_path)
    report(tmp_path, "ensure", ensure=True)
    result = collect(item, time.time())
    assert result["fatal"]["error_message"] == "Shader compilation failures are Fatal."
    assert result["fatal"]["threads"][0]["name"] == "Background Worker #9"
    assert len(result["ensures"]) == 1
    assert observe(item) and item["error"]["code"] == "editor_crashed"
    assert item["exit_evidence"]["exit_kind"] == "crash"


def test_other_pid_and_reused_pid_from_an_old_process_are_rejected(tmp_path):
    item = instance(tmp_path)
    report(tmp_path, "other", pid=15448)
    old = report(tmp_path, "old") / "CrashContext.runtime-xml"
    timestamp = time.time() - 100
    os.utime(old, (timestamp, timestamp))
    assert collect(item, time.time())["fatal"] is None


def test_incomplete_context_is_reported_and_late_report_is_retried(tmp_path):
    item = instance(tmp_path)
    path = report(tmp_path) / "CrashContext.runtime-xml"
    content = path.read_text()
    path.write_text("<FGenericCrashContext>")
    now = time.time()
    assert observe(item, now) and item["exit_evidence"]["state"] == "pending"
    assert item["exit_evidence"]["errors"]
    path.write_text(content)
    assert observe(item, now + 3)
    assert item["exit_evidence"]["state"] == "complete"


def test_no_report_is_explicit_and_exit_zero_can_complete_normally(tmp_path):
    item = instance(tmp_path)
    item["exit_code"] = 0
    assert observe(item, item["exited_wall_time"] + 31)
    assert item["exit_evidence"]["exit_kind"] == "normal"
    assert item["exit_evidence"]["fatal"] is None


def test_context_arriving_before_dump_keeps_collecting_late_artifacts(tmp_path):
    item = instance(tmp_path)
    directory = report(tmp_path)
    dump = directory / "UEMinidump.dmp"
    dump.unlink()
    now = time.time()
    assert observe(item, now) and item["exit_evidence"]["state"] == "pending"
    assert item["error"]["code"] == "editor_crashed"
    dump.write_bytes(b"late dump")
    assert observe(item, now + 3) and item["exit_evidence"]["state"] == "complete"
    assert item["exit_evidence"]["fatal"]["dump_path"] == str(dump)
