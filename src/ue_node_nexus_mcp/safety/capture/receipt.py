"""Open one durable screenshot receipt without obstructing its producer."""

import ctypes
from ctypes import wintypes
import io
import os
from pathlib import Path
import sys


def open_receipt(path: Path):
    if sys.platform != "win32":
        return path.open("r", encoding="utf-8-sig")
    import msvcrt

    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateFileW.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
                                  wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE)
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
    kernel.CloseHandle.restype = wintypes.BOOL
    # Read/write/delete sharing lets UE replace the receipt during this read.
    handle = kernel.CreateFileW(str(path), 0x80000000, 0x00000007, None, 3, 0x80, None)
    if handle == wintypes.HANDLE(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        descriptor = msvcrt.open_osfhandle(handle, os.O_RDONLY | os.O_BINARY)
    except BaseException:
        kernel.CloseHandle(handle)
        raise
    try:
        return io.open(descriptor, "r", encoding="utf-8-sig")
    except BaseException:
        os.close(descriptor)
        raise
