"""Discover editors before their bridge or startup guard is ready."""

from __future__ import annotations

import ctypes
from ctypes import wintypes as w

from .windows import kernel, process_info


def process_ids(names: tuple[str, ...]) -> list[int]:
    api = kernel()

    class Entry(ctypes.Structure):
        _fields_ = [("size", w.DWORD), ("usage", w.DWORD), ("pid", w.DWORD), ("heap", ctypes.c_size_t),
                    ("module", w.DWORD), ("threads", w.DWORD), ("parent", w.DWORD), ("priority", w.LONG),
                    ("flags", w.DWORD), ("filename", w.WCHAR * 260)]

    api.CreateToolhelp32Snapshot.argtypes = [w.DWORD, w.DWORD]
    api.CreateToolhelp32Snapshot.restype = w.HANDLE
    api.Process32FirstW.argtypes = [w.HANDLE, ctypes.POINTER(Entry)]
    api.Process32NextW.argtypes = [w.HANDLE, ctypes.POINTER(Entry)]
    snapshot = api.CreateToolhelp32Snapshot(2, 0)
    if snapshot == ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    result = []
    try:
        entry = Entry()
        entry.size = ctypes.sizeof(entry)
        valid = api.Process32FirstW(snapshot, ctypes.byref(entry))
        while valid:
            if entry.filename.lower() in names:
                result.append(int(entry.pid))
            valid = api.Process32NextW(snapshot, ctypes.byref(entry))
    finally:
        api.CloseHandle(snapshot)
    return result


def editor_pids() -> list[int]:
    return process_ids(("unrealeditor.exe", "unrealeditor-cmd.exe"))


def arguments(pid: int) -> list[str]:
    api = kernel()
    handle = api.OpenProcess(0x1000, False, pid)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    native = ctypes.WinDLL("ntdll")
    query = native.NtQueryInformationProcess
    query.argtypes = [w.HANDLE, ctypes.c_int, w.LPVOID, w.ULONG, ctypes.POINTER(w.ULONG)]
    query.restype = w.LONG
    try:
        size = w.ULONG()
        query(handle, 60, None, 0, ctypes.byref(size))
        if not 0 < size.value <= 1024 * 1024:
            raise OSError("process command line is unavailable")
        buffer = ctypes.create_string_buffer(size.value)
        if query(handle, 60, buffer, size, ctypes.byref(size)) < 0:
            raise OSError("process command line query failed")

        class UnicodeString(ctypes.Structure):
            _fields_ = [("length", w.USHORT), ("capacity", w.USHORT), ("buffer", w.LPWSTR)]

        text = ctypes.cast(buffer, ctypes.POINTER(UnicodeString)).contents.buffer
        shell = ctypes.WinDLL("shell32", use_last_error=True)
        shell.CommandLineToArgvW.argtypes = [w.LPCWSTR, ctypes.POINTER(ctypes.c_int)]
        shell.CommandLineToArgvW.restype = ctypes.POINTER(w.LPWSTR)
        count = ctypes.c_int()
        argv = shell.CommandLineToArgvW(text, ctypes.byref(count))
        if not argv:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            return [argv[index] for index in range(count.value)]
        finally:
            api.LocalFree(ctypes.cast(argv, w.HLOCAL))
    finally:
        api.CloseHandle(handle)


def commandlet(argv: list[str]) -> bool:
    return any(value.lower().startswith("-run=") or value.lower() in ("-cook", "-commandlet") for value in argv)


def memory_status() -> dict:
    class Status(ctypes.Structure):
        _fields_ = [("length", w.DWORD), ("load", w.DWORD)] + [(name, ctypes.c_ulonglong) for name in
                    ("total", "available", "page_total", "page_available", "virtual_total", "virtual_available", "extended")]

    status = Status()
    status.length = ctypes.sizeof(status)
    api = kernel()
    api.GlobalMemoryStatusEx.argtypes = [ctypes.POINTER(Status)]
    if not api.GlobalMemoryStatusEx(ctypes.byref(status)):
        raise ctypes.WinError(ctypes.get_last_error())
    return dict(total=int(status.total), available=int(status.available))
