"""Rendering options reach the actual process and drift blocks engine calls."""

import json
from unittest.mock import Mock

import pytest

from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.instances.lifecycle.platform import WindowsPlatform
from ue_node_nexus_mcp.safety.isolation.environment import verify_worker
from tests.safety.test_publication_evidence import environment


def test_worker_process_receives_source_rendering_launch_inputs(tmp_path, monkeypatch):
    project = tmp_path / "Project/Probe.uproject"
    project.parent.mkdir()
    project.touch()
    measured = environment()
    measured["render_launch"]["arguments"] = ["-sm6", "-resx=720", "-renderoffscreen"]
    marker = project.parent / "Saved/Nexus/validation-worker.json"
    marker.parent.mkdir(parents=True)
    marker.write_text(json.dumps(dict(project_path=str(project), environment=measured)))
    launch = Mock(return_value=Mock(pid=1234))
    monkeypatch.setattr("ue_node_nexus_mcp.instances.lifecycle.platform.subprocess.Popen", launch)
    platform = WindowsPlatform(tmp_path / "Runtime")
    monkeypatch.setattr(platform, "inspect", lambda _: dict(pid=1234, process_created=1))
    try:
        platform.launch(dict(project_path=str(project), instance_id="isolated", start_intent_id="intent",
                             launch=dict(executable="UnrealEditor.exe", rhi="d3d12", engine_dir="engine", launch_profile="interactive")))
        assert launch.call_args.args[0][-3:] == measured["render_launch"]["arguments"]
        assert "-d3d12" in launch.call_args.args[0]
    finally:
        platform.close()


def test_matching_strings_without_early_bootstrap_are_refused():
    measured = environment()
    with pytest.raises(InstanceError, match="before RHI"):
        verify_worker(measured, dict(environment=measured, reproduction=dict(applied=True, phase="PostEngineInit")))
    verify_worker(measured, dict(environment=measured, reproduction=dict(applied=True, phase="PostConfigInit",
                  callbacks_applied=True, callbacks_phase="EngineLoopInitComplete")))


def test_matching_values_without_safe_callback_completion_are_refused():
    measured = environment()
    with pytest.raises(InstanceError, match="callbacks"):
        verify_worker(measured, dict(environment=measured, reproduction=dict(applied=True, phase="PostConfigInit")))


def test_worker_actual_runtime_cvar_drift_is_refused():
    measured = environment()
    actual = environment()
    actual["render_cvars"]["r.ScreenPercentage"] = "70"
    with pytest.raises(InstanceError, match="variables"):
        verify_worker(measured, dict(environment=actual, reproduction=dict(applied=True, phase="PostConfigInit")))
