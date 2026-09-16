"""Sample the verified process, keeping resident and committed memory separate."""

import ctypes
from ctypes import wintypes as w
import time

from .windows import kernel


class Counters(ctypes.Structure):
    _fields_ = [("size", w.DWORD), ("faults", w.DWORD)] + [(name, ctypes.c_size_t) for name in (
        "peak_working", "working", "peak_paged", "paged", "peak_nonpaged", "nonpaged",
        "pagefile", "peak_pagefile", "private", "private_working")] + [("shared_commit", ctypes.c_ulonglong)]


def sample(identity: dict) -> dict:
    api = kernel()
    handle = api.OpenProcess(0x1000 | 0x100000, False, identity["pid"])
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        times = [w.FILETIME() for _ in range(4)]
        if not api.GetProcessTimes(handle, *[ctypes.byref(item) for item in times]):
            raise ctypes.WinError(ctypes.get_last_error())
        ticks = [(item.dwHighDateTime << 32) | item.dwLowDateTime for item in times]
        if str(ticks[0]) != identity["process_created"] or api.WaitForSingleObject(handle, 0) == 0:
            return dict()

        counters = Counters()
        counters.size = ctypes.sizeof(counters)
        api.K32GetProcessMemoryInfo.argtypes = [w.HANDLE, ctypes.POINTER(Counters), w.DWORD]
        api.K32GetProcessMemoryInfo.restype = w.BOOL
        if not api.K32GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.size):
            raise ctypes.WinError(ctypes.get_last_error())
        handles = w.DWORD()
        api.GetProcessHandleCount.argtypes = [w.HANDLE, ctypes.POINTER(w.DWORD)]
        if not api.GetProcessHandleCount(handle, ctypes.byref(handles)):
            raise ctypes.WinError(ctypes.get_last_error())
        return dict(working_set_bytes=int(counters.working), private_bytes=int(counters.private),
                    private_working_set_bytes=int(counters.private_working), handle_count=int(handles.value),
                    cpu_seconds=(ticks[2] + ticks[3]) / 10000000, resources_sampled_at=time.time())
    finally:
        api.CloseHandle(handle)
