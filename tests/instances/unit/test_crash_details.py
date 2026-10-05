"""Exit evidence keeps the renderer's contracts and Python GC roots without unbounded logs."""

import xml.etree.ElementTree as ET

from ue_node_nexus_mcp.instances.lifecycle.crashes.details import LOG_BUDGET, log_details, stacks


def test_renderer_errors_and_reference_chains_survive(tmp_path):
    path = tmp_path / "Editor.log"
    lines = ["D3D12 ERROR: COPYRESOURCE_INVALIDSOURCESTATE #278 Shoudun.DepthStencil",
             "D3D12 ERROR: VS/PS SV_Position signature mismatch",
             "LogD3D12RHI: DXGI_ERROR_DEVICE_REMOVED Plug-and-play stop",
             "LogReferenceChain: Display: GCObjectReferencer -> FPyReferenceCollector::AddReferencedObjects",
             "[2026.10.04-22.18.24:637][219]Error:   -> UBPC_CP_CameraFrame_C::BottomCollision = World",
             "LogWindows: Error: Fatal error: World Memory Leaks: 2 leaks objects and packages"]
    path.write_text("\n".join(lines), encoding="utf-8")
    evidence = log_details(path)
    assert evidence["rhi_validation_lines"] == lines[:3]
    assert evidence["reference_chain_lines"] == lines[3:]
    assert evidence["fatal_log_lines"] == lines[-1:]
    assert evidence["log_evidence"]["whole_log_scanned"]


def test_budgets_report_scan_and_retention_limits(tmp_path):
    path = tmp_path / "Large.log"
    path.write_bytes(b"old evidence\n" * LOG_BUDGET + b"D3D12 ERROR: bad state\n" * 50)
    result = log_details(path)
    assert len(result["rhi_validation_lines"]) == 32
    assert result["log_evidence"]["bytes_read"] == LOG_BUDGET
    assert not result["log_evidence"]["whole_log_scanned"]
    assert result["log_evidence"]["dropped_lines"]["renderer"] == 18


def test_symbolized_and_portable_stacks_have_explicit_limits():
    properties = ET.fromstring("<RuntimeProperties><CallStack>ParameterValueText\nModuleInputs</CallStack>"
                               "<PCallStack>" + "A" * 20000 + "</PCallStack></RuntimeProperties>")
    result = stacks(properties)
    assert "ParameterValueText" in result["call_stack"]
    assert len(result["portable_call_stack"]) == 16384
    assert result["truncated_stacks"] == ["portable_call_stack"]
