"""Retain bounded renderer diagnostics and world-leak reference chains as evidence."""

from pathlib import Path
import re


LOG_BUDGET = 256 * 1024
LINE_BUDGET = 4096


def log_details(path: Path) -> dict:
    size = path.stat().st_size
    offset = max(0, size - LOG_BUDGET)
    with path.open("rb") as stream:
        stream.seek(offset)
        payload = stream.read(LOG_BUDGET)
    lines = payload.decode("utf-8", errors="replace").splitlines()
    if offset and lines:
        lines = lines[1:]
    fatal, renderer, references = [], [], []
    for line in lines:
        if any(token in line for token in ("Fatal error:", "appError called", "Failed to create pipeline",
                                           "Failed to create graphics pipeline", "Shader compilation failures are Fatal")):
            fatal.append(line[:LINE_BUDGET])
        if re.search(r"D3D12 (?:ERROR|WARNING):|RESOURCE_BARRIER|COPYRESOURCE|SV_Position|DXGI_ERROR_DEVICE_|Plug.and.play", line, re.I):
            renderer.append(line[:LINE_BUDGET])
        if ("LogReferenceChain:" in line or "FPyReferenceCollector" in line or "World Memory Leaks:" in line
                or "Printing reference chains" in line or re.search(r"Error:\s+(?:\(root\)|->)", line)):
            references.append(line[:LINE_BUDGET])
    return dict(fatal_log_lines=fatal[-20:], rhi_validation_lines=renderer[-32:], reference_chain_lines=references[-64:],
                log_evidence=dict(bytes_read=len(payload), byte_budget=LOG_BUDGET, tail_offset=offset,
                    file_bytes=size, whole_log_scanned=offset == 0,
                    dropped_lines=dict(fatal=max(0, len(fatal) - 20), renderer=max(0, len(renderer) - 32),
                                       references=max(0, len(references) - 64))))


def stacks(properties) -> dict:
    result = dict()
    truncated = []
    for tag, key in (("CallStack", "call_stack"), ("PCallStack", "portable_call_stack")):
        value = properties.findtext(tag, "")
        if len(value) > 16384:
            truncated.append(key)
        result[key] = value[:16384]
    result["truncated_stacks"] = truncated
    return result
