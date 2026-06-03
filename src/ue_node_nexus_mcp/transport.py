r"""Windows named-pipe transport for the UE bridge.

Each UE editor instance exposes a pipe at ``\\.\pipe\UeNodeNexusBridge.<pid>``.
This module owns: the length-prefixed JSON framing (pure functions), pipe-name
helpers, live-instance enumeration, and a ctypes client that does one stateless
request/response per call (connect -> write frame -> read frame -> close).

No third-party dependency: only ``ctypes`` + stdlib. The Win32 bindings load
lazily, so the pure helpers remain importable and testable on any platform.
"""

from __future__ import annotations

import json
import struct
import sys
from typing import Any

from .errors import BridgeError

PIPE_PREFIX = "UeNodeNexusBridge."
PIPE_ROOT = "\\\\.\\pipe\\"
# Reject absurd frame lengths before allocating (mirrors the C++ server cap).
MAX_FRAME_BYTES = 64 * 1024 * 1024


# --- Framing (pure) --------------------------------------------------------

def encode_frame(envelope: dict[str, Any]) -> bytes:
    """4-byte little-endian length prefix + compact UTF-8 JSON body."""
    body = json.dumps(envelope, separators=(",", ":")).encode("utf-8")
    return struct.pack("<I", len(body)) + body


def decode_body(raw: bytes) -> dict[str, Any]:
    """Parse a response frame body into a JSON object."""
    try:
        decoded = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise BridgeError("bridge returned invalid JSON") from exc
    if not isinstance(decoded, dict):
        raise BridgeError("bridge returned JSON that is not an object")
    return decoded


# --- Pipe-name helpers -----------------------------------------------------

def make_pipe_name(pid: int) -> str:
    return f"{PIPE_ROOT}{PIPE_PREFIX}{pid}"


def parse_pid_from_pipe_name(name: str) -> int | None:
    """Extract the trailing pid from a bare pipe name or full path."""
    short = name.rsplit("\\", 1)[-1]
    if not short.startswith(PIPE_PREFIX):
        return None
    suffix = short[len(PIPE_PREFIX):]
    return int(suffix) if suffix.isdigit() else None


# --- Win32 bindings (lazy) -------------------------------------------------

_WIN: "_Win32 | None" = None


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


# --- Enumeration -----------------------------------------------------------

def enumerate_pipe_names() -> list[tuple[int, str]]:
    """Return [(pid, full_pipe_name)] for every live UeNodeNexusBridge pipe.

    The pipe namespace IS the registry: a dead editor's pipe vanishes on process
    exit, so there is no stale state to prune.
    """
    win = _win()
    ctypes = win.ctypes
    data = win.WIN32_FIND_DATAW()
    handle = win.FindFirstFileW(f"{PIPE_ROOT}*", ctypes.byref(data))
    if handle == ctypes.c_void_p(win.INVALID_HANDLE_VALUE).value or handle in (0, None):
        return []

    found: list[tuple[int, str]] = []
    try:
        while True:
            pid = parse_pid_from_pipe_name(data.cFileName)
            if pid is not None:
                found.append((pid, f"{PIPE_ROOT}{data.cFileName}"))
            if not win.FindNextFileW(handle, ctypes.byref(data)):
                break
    finally:
        win.FindClose(handle)
    return found


# --- Client ----------------------------------------------------------------

class NamedPipeTransport:
    """Stateless request/response over a named pipe (one connect per call)."""

    def send(self, pipe_name: str, envelope: dict[str, Any], timeout_seconds: float) -> dict[str, Any]:
        win = _win()
        ctypes = win.ctypes
        timeout_ms = max(1, int(timeout_seconds * 1000))

        handle = self._connect(win, pipe_name, timeout_ms)
        try:
            self._write_all(win, handle, encode_frame(envelope), timeout_ms)
            length_bytes = self._read_exact(win, handle, 4, timeout_ms)
            (length,) = struct.unpack("<I", length_bytes)
            if length == 0 or length > MAX_FRAME_BYTES:
                raise BridgeError(f"bridge returned invalid frame length {length}")
            body = self._read_exact(win, handle, length, timeout_ms)
        finally:
            win.CloseHandle(handle)
        return decode_body(body)

    def _connect(self, win: _Win32, pipe_name: str, timeout_ms: int):
        ctypes = win.ctypes
        while True:
            handle = win.CreateFileW(
                pipe_name,
                win.GENERIC_READ | win.GENERIC_WRITE,
                0,
                None,
                win.OPEN_EXISTING,
                win.FILE_FLAG_OVERLAPPED,
                None,
            )
            if handle != ctypes.c_void_p(win.INVALID_HANDLE_VALUE).value and handle not in (0, None):
                return handle
            err = ctypes.get_last_error()
            if err == win.ERROR_FILE_NOT_FOUND:
                raise BridgeError(f"UE instance pipe not found (instance gone?): {pipe_name}")
            if err == win.ERROR_PIPE_BUSY:
                if not win.WaitNamedPipeW(pipe_name, timeout_ms):
                    raise BridgeError(f"UE instance busy, all pipe slots in use: {pipe_name}")
                continue
            raise BridgeError(f"failed to open pipe {pipe_name}: win32 error {err}")

    def _overlapped_io(self, win: _Win32, handle, func, buf_ptr, num_bytes: int, timeout_ms: int) -> int:
        ctypes = win.ctypes
        wintypes = win.wintypes
        ov = win.OVERLAPPED()
        event = win.CreateEventW(None, True, False, None)
        if not event:
            raise BridgeError("failed to create overlapped event")
        ov.hEvent = event
        transferred = wintypes.DWORD(0)
        try:
            ok = func(handle, buf_ptr, num_bytes, ctypes.byref(transferred), ctypes.byref(ov))
            if not ok:
                err = ctypes.get_last_error()
                if err != win.ERROR_IO_PENDING:
                    raise BridgeError(f"bridge pipe I/O failed: win32 error {err}")
                if win.WaitForSingleObject(event, timeout_ms) != win.WAIT_OBJECT_0:
                    win.CancelIoEx(handle, ctypes.byref(ov))
                    raise BridgeError("bridge I/O timed out")
                if not win.GetOverlappedResult(handle, ctypes.byref(ov), ctypes.byref(transferred), False):
                    raise BridgeError(f"bridge pipe I/O failed: win32 error {ctypes.get_last_error()}")
            return int(transferred.value)
        finally:
            win.CloseHandle(event)

    def _read_exact(self, win: _Win32, handle, num_bytes: int, timeout_ms: int) -> bytes:
        ctypes = win.ctypes
        buffer = (ctypes.c_char * num_bytes)()
        total = 0
        while total < num_bytes:
            got = self._overlapped_io(win, handle, win.ReadFile, ctypes.byref(buffer, total), num_bytes - total, timeout_ms)
            if got == 0:
                raise BridgeError("bridge closed the connection")
            total += got
        return bytes(buffer)

    def _write_all(self, win: _Win32, handle, data: bytes, timeout_ms: int) -> None:
        ctypes = win.ctypes
        buffer = (ctypes.c_char * len(data)).from_buffer_copy(data)
        total = 0
        size = len(data)
        while total < size:
            put = self._overlapped_io(win, handle, win.WriteFile, ctypes.byref(buffer, total), size - total, timeout_ms)
            if put == 0:
                raise BridgeError("bridge closed the connection")
            total += put


named_pipe_transport = NamedPipeTransport()
