"""Lazy, explicitly typed Win32 pipe bindings shared by control and data IPC."""

from __future__ import annotations

import sys

from ..errors import BridgeError

_WIN: _Win32 | None = None


class _Win32:
    """Lazily-bound kernel32 entry points used by the pipe client/enumerator."""

    GENERIC_READ = 0x80000000
    GENERIC_WRITE = 0x40000000
    OPEN_EXISTING = 3
    FILE_FLAG_OVERLAPPED = 0x40000000
    INVALID_HANDLE_VALUE = -1
    ERROR_FILE_NOT_FOUND = 2
    ERROR_PIPE_BUSY = 231
    ERROR_IO_PENDING = 997
    ERROR_NO_MORE_FILES = 18
    WAIT_OBJECT_0 = 0

    def __init__(self) -> None:
        import ctypes
        from ctypes import wintypes

        self.ctypes = ctypes
        self.wintypes = wintypes
        k = ctypes.WinDLL("kernel32", use_last_error=True)

        class OVERLAPPED(ctypes.Structure):
            _fields_ = [
                ("Internal", ctypes.c_void_p),
                ("InternalHigh", ctypes.c_void_p),
                ("Offset", wintypes.DWORD),
                ("OffsetHigh", wintypes.DWORD),
                ("hEvent", wintypes.HANDLE),
            ]

        class WIN32_FIND_DATAW(ctypes.Structure):
            _fields_ = [
                ("dwFileAttributes", wintypes.DWORD),
                ("ftCreationTime", wintypes.FILETIME),
                ("ftLastAccessTime", wintypes.FILETIME),
                ("ftLastWriteTime", wintypes.FILETIME),
                ("nFileSizeHigh", wintypes.DWORD),
                ("nFileSizeLow", wintypes.DWORD),
                ("dwReserved0", wintypes.DWORD),
                ("dwReserved1", wintypes.DWORD),
                ("cFileName", wintypes.WCHAR * 260),
                ("cAlternateFileName", wintypes.WCHAR * 14),
            ]

        self.OVERLAPPED = OVERLAPPED
        self.WIN32_FIND_DATAW = WIN32_FIND_DATAW

        self.CreateFileW = k.CreateFileW
        self.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
        self.CreateFileW.restype = wintypes.HANDLE

        self.WaitNamedPipeW = k.WaitNamedPipeW
        self.WaitNamedPipeW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD]
        self.WaitNamedPipeW.restype = wintypes.BOOL

        self.CreateEventW = k.CreateEventW
        self.CreateEventW.argtypes = [wintypes.LPVOID, wintypes.BOOL, wintypes.BOOL, wintypes.LPCWSTR]
        self.CreateEventW.restype = wintypes.HANDLE

        self.ReadFile = k.ReadFile
        self.ReadFile.argtypes = [wintypes.HANDLE, wintypes.LPVOID, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
        self.ReadFile.restype = wintypes.BOOL

        self.WriteFile = k.WriteFile
        self.WriteFile.argtypes = [wintypes.HANDLE, wintypes.LPCVOID, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
        self.WriteFile.restype = wintypes.BOOL

        self.WaitForSingleObject = k.WaitForSingleObject
        self.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        self.WaitForSingleObject.restype = wintypes.DWORD

        self.GetOverlappedResult = k.GetOverlappedResult
        self.GetOverlappedResult.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.POINTER(wintypes.DWORD), wintypes.BOOL]
        self.GetOverlappedResult.restype = wintypes.BOOL

        self.CancelIoEx = k.CancelIoEx
        self.CancelIoEx.argtypes = [wintypes.HANDLE, ctypes.c_void_p]
        self.CancelIoEx.restype = wintypes.BOOL

        self.CloseHandle = k.CloseHandle
        self.CloseHandle.argtypes = [wintypes.HANDLE]
        self.CloseHandle.restype = wintypes.BOOL

        self.FindFirstFileW = k.FindFirstFileW
        self.FindFirstFileW.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(WIN32_FIND_DATAW)]
        self.FindFirstFileW.restype = wintypes.HANDLE

        self.FindNextFileW = k.FindNextFileW
        self.FindNextFileW.argtypes = [wintypes.HANDLE, ctypes.POINTER(WIN32_FIND_DATAW)]
        self.FindNextFileW.restype = wintypes.BOOL

        self.FindClose = k.FindClose
        self.FindClose.argtypes = [wintypes.HANDLE]
        self.FindClose.restype = wintypes.BOOL


def _win() -> _Win32:
    global _WIN
    if sys.platform != "win32":
        raise BridgeError("named pipe transport requires Windows")
    if _WIN is None:
        _WIN = _Win32()
    return _WIN
