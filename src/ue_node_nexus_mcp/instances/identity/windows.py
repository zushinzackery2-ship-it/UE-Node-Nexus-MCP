"""Small, explicitly typed Win32 identity bindings."""

from __future__ import annotations

import ctypes
from ctypes import wintypes as w
from functools import lru_cache
from pathlib import Path


@lru_cache(maxsize=1)
def kernel():
    api = ctypes.WinDLL("kernel32", use_last_error=True)
    signatures = [
        ("OpenProcess", [w.DWORD, w.BOOL, w.DWORD], w.HANDLE),
        ("CloseHandle", [w.HANDLE], w.BOOL),
        ("GetProcessTimes", [w.HANDLE] + [ctypes.POINTER(w.FILETIME)] * 4, w.BOOL),
        ("QueryFullProcessImageNameW", [w.HANDLE, w.DWORD, w.LPWSTR, ctypes.POINTER(w.DWORD)], w.BOOL),
        ("WaitForSingleObject", [w.HANDLE, w.DWORD], w.DWORD),
        ("CreateFileW", [w.LPCWSTR, w.DWORD, w.DWORD, w.LPVOID, w.DWORD, w.DWORD, w.HANDLE], w.HANDLE),
        ("GetFinalPathNameByHandleW", [w.HANDLE, w.LPWSTR, w.DWORD, w.DWORD], w.DWORD),
        ("GetFileInformationByHandleEx", [w.HANDLE, ctypes.c_int, w.LPVOID, w.DWORD], w.BOOL),
        ("GetCurrentProcess", [], w.HANDLE),
        ("LocalFree", [w.HLOCAL], w.HLOCAL),
    ]
    for name, args, result in signatures:
        function = getattr(api, name)
        function.argtypes, function.restype = args, result
    return api


def final_path(path: Path, *, must_exist: bool = True) -> str:
    from .path_case import normalize
    api = kernel()
    handle = api.CreateFileW(str(path), 0, 7, None, 3, 0x02000000, None)
    if handle == ctypes.c_void_p(-1).value:
        if not must_exist and not path.exists():
            return normalize(str(path), allow_missing=True)
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        buffer = ctypes.create_unicode_buffer(32768)
        size = api.GetFinalPathNameByHandleW(handle, buffer, len(buffer), 0)
        if not size or size >= len(buffer):
            raise ctypes.WinError(ctypes.get_last_error())
        result = buffer.value
    finally:
        api.CloseHandle(handle)
    if result.startswith("\\\\?\\UNC\\"):
        result = "\\\\" + result[8:]
    elif result.startswith("\\\\?\\"):
        result = result[4:]
    return normalize(result)


def process_info(pid: int) -> dict | None:
    api = kernel()
    handle = api.OpenProcess(0x1000 | 0x100000, False, pid)
    if not handle:
        if ctypes.get_last_error() == 87:
            return None
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        if api.WaitForSingleObject(handle, 0) == 0:
            return None
        times = [w.FILETIME() for _ in range(4)]
        if not api.GetProcessTimes(handle, *[ctypes.byref(item) for item in times]):
            raise ctypes.WinError(ctypes.get_last_error())
        created = str((times[0].dwHighDateTime << 32) | times[0].dwLowDateTime)
        buffer, size = ctypes.create_unicode_buffer(32768), w.DWORD(32768)
        if not api.QueryFullProcessImageNameW(handle, 0, buffer, ctypes.byref(size)):
            raise ctypes.WinError(ctypes.get_last_error())
        return dict(pid=pid, process_created=created, executable=buffer.value.replace("\\", "/").lower())
    finally:
        api.CloseHandle(handle)


@lru_cache(maxsize=1)
def user_sid() -> str:
    return process_user_sid(kernel().GetCurrentProcess())


def process_user_sid(process) -> str:
    api = kernel()
    advapi = ctypes.WinDLL("advapi32", use_last_error=True)
    advapi.OpenProcessToken.argtypes = [w.HANDLE, w.DWORD, ctypes.POINTER(w.HANDLE)]
    advapi.GetTokenInformation.argtypes = [w.HANDLE, ctypes.c_int, w.LPVOID, w.DWORD, ctypes.POINTER(w.DWORD)]
    advapi.ConvertSidToStringSidW.argtypes = [w.LPVOID, ctypes.POINTER(w.LPWSTR)]
    token = w.HANDLE()
    if not advapi.OpenProcessToken(process, 8, ctypes.byref(token)):
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        size = w.DWORD()
        advapi.GetTokenInformation(token, 1, None, 0, ctypes.byref(size))
        data = ctypes.create_string_buffer(size.value)
        if not advapi.GetTokenInformation(token, 1, data, size, ctypes.byref(size)):
            raise ctypes.WinError(ctypes.get_last_error())
        sid = ctypes.cast(data, ctypes.POINTER(ctypes.c_void_p))[0]
        text = w.LPWSTR()
        if not advapi.ConvertSidToStringSidW(sid, ctypes.byref(text)):
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            return text.value
        finally:
            api.LocalFree(ctypes.cast(text, w.HLOCAL))
    finally:
        api.CloseHandle(token)
