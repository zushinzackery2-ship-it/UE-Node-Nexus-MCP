"""Retain a verified process handle until its final exit code is observed."""

import ctypes
from ctypes import wintypes as w

from .windows import kernel


class ProcessWatch:
    def __init__(self, identity: dict) -> None:
        self.api = kernel()
        self.handle = self.api.OpenProcess(0x1000 | 0x100000, False, identity["pid"])
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())
        times = [w.FILETIME() for _ in range(4)]
        ok = self.api.GetProcessTimes(self.handle, *[ctypes.byref(item) for item in times])
        created = str((times[0].dwHighDateTime << 32) | times[0].dwLowDateTime)
        if not ok or created != identity["process_created"]:
            self.close()
            raise OSError("process creation identity changed before observation")

    def poll(self) -> int | None:
        state = self.api.WaitForSingleObject(self.handle, 0)
        if state == 0x102:
            return None
        if state != 0:
            raise ctypes.WinError(ctypes.get_last_error())
        result = w.DWORD()
        self.api.GetExitCodeProcess.argtypes = [w.HANDLE, ctypes.POINTER(w.DWORD)]
        if not self.api.GetExitCodeProcess(self.handle, ctypes.byref(result)):
            raise ctypes.WinError(ctypes.get_last_error())
        return int(result.value)

    def close(self) -> None:
        if self.handle:
            self.api.CloseHandle(self.handle)
            self.handle = None
