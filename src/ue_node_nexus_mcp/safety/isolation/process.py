"""Bounded cancellation of only the process belonging to an isolated project."""

from __future__ import annotations

import ctypes
from ctypes import wintypes
from pathlib import Path

from ...instances.errors import require
from ...instances.identity.processes import is_alive
from ...instances.identity.windows import kernel


def stop_owned(identity: dict, project: Path) -> dict:
    require(Path(identity.get("project_path", "")).resolve() == project.resolve(),
            "isolation_wrong_project", "cancellation target is not the validation project")
    require(isinstance(identity.get("pid"), int) and identity.get("process_created") and identity.get("executable"),
            "isolation_identity_missing", "process creation identity is required for cancellation")
    if not is_alive(identity):
        return dict(exit_confirmed=True, forced=False, state="EXITED")
    api = kernel()
    handle = api.OpenProcess(0x1000 | 0x100000 | 1, False, identity["pid"])
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        times = [wintypes.FILETIME() for _ in range(4)]
        require(api.GetProcessTimes(handle, *[ctypes.byref(value) for value in times]),
                "isolation_identity_missing", "could not verify the opened process handle")
        created = str((times[0].dwHighDateTime << 32) | times[0].dwLowDateTime)
        require(created == identity["process_created"], "isolation_identity_mismatch", "PID was reused before cancellation")
        api.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
        api.TerminateProcess.restype = wintypes.BOOL
        if not api.TerminateProcess(handle, 88):
            raise ctypes.WinError(ctypes.get_last_error())
        require(api.WaitForSingleObject(handle, 5000) == 0,
                "isolation_cleanup_failed", "validation process did not exit after cancellation")
        return dict(exit_confirmed=True, forced=True, state="EXITED", exit_code=88, pid=identity["pid"])
    finally:
        api.CloseHandle(handle)
