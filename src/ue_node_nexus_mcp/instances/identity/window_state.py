"""Visible editor captions explain startup dialogs without loading UI frameworks."""

import ctypes
from ctypes import wintypes


def captions(pid: int) -> list[str]:
    api = ctypes.WinDLL("user32", use_last_error=True)
    api.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    api.GetWindowTextLengthW.argtypes = [wintypes.HWND]
    api.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    api.IsWindowVisible.argtypes = [wintypes.HWND]
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    api.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    result = []

    @callback_type
    def inspect(handle, parameter):
        owner = wintypes.DWORD()
        api.GetWindowThreadProcessId(handle, ctypes.byref(owner))
        if owner.value == pid and api.IsWindowVisible(handle):
            text = ctypes.create_unicode_buffer(api.GetWindowTextLengthW(handle) + 1)
            api.GetWindowTextW(handle, text, len(text))
            result.append(text.value)
        return True

    api.EnumWindows(inspect, 0)
    return result
