"""Fold each path component according to its containing Windows directory."""

import ctypes
from ctypes import wintypes as w
from pathlib import Path

from .windows import kernel


def lower(value: str) -> str:
    api = kernel()
    api.LCMapStringEx.argtypes = [w.LPCWSTR, w.DWORD, w.LPCWSTR, ctypes.c_int, w.LPWSTR, ctypes.c_int,
                                 w.LPVOID, w.LPVOID, w.LPARAM]
    api.LCMapStringEx.restype = ctypes.c_int
    size = api.LCMapStringEx("", 0x100, value, -1, None, 0, None, None, 0)
    if not size:
        raise ctypes.WinError(ctypes.get_last_error())
    buffer = ctypes.create_unicode_buffer(size)
    if not api.LCMapStringEx("", 0x100, value, -1, buffer, size, None, None, 0):
        raise ctypes.WinError(ctypes.get_last_error())
    return buffer.value


def case_sensitive(api, directory: Path, inherited: bool, allow_missing: bool) -> bool:
    """Whether names inside directory compare case-sensitively; a missing directory keeps inherited."""
    handle = api.CreateFileW(str(directory), 0, 7, None, 3, 0x02000000, None)
    if handle == ctypes.c_void_p(-1).value:
        error = ctypes.get_last_error()
        if not allow_missing or error not in (2, 3):
            raise ctypes.WinError(error)
        return inherited
    try:
        flags = w.DWORD()
        if api.GetFileInformationByHandleEx(handle, 23, ctypes.byref(flags), 4):
            return bool(flags.value & 1)
        error = ctypes.get_last_error()
        if error not in (1, 50, 87):
            raise ctypes.WinError(error)
        return False
    finally:
        api.CloseHandle(handle)


def normalize(value: str, *, allow_missing=False) -> str:
    api = kernel()
    path = Path(value)
    parent = Path(path.anchor)
    result = lower(path.anchor).replace("\\", "/")
    sensitive = False
    for name in path.parts[1:]:
        sensitive = case_sensitive(api, parent, sensitive, allow_missing)
        result = result.rstrip("/") + "/" + (name if sensitive else lower(name))
        parent /= name
    return result
