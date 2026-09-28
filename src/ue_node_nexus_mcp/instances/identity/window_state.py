"""Titled editor windows, visible or not, without loading UI frameworks.

A prompt of a hidden launch never shows, so visible captions alone report a
blocked startup as an editor with no windows at all. Hidden windows count when
they are editor (``UnrealWindow``) or native dialog (``#32770``) windows; every
GUI process also owns hidden input-method windows, which say nothing.
"""

import ctypes
from ctypes import wintypes
from functools import lru_cache

DIALOG_CLASS = "#32770"
EDITOR_CLASS = "UnrealWindow"
CALLBACK = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)


@lru_cache(maxsize=1)
def user32():
    api = ctypes.WinDLL("user32", use_last_error=True)
    api.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    api.GetWindowTextLengthW.argtypes = [wintypes.HWND]
    api.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    api.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    api.IsWindowVisible.argtypes = [wintypes.HWND]
    api.EnumWindows.argtypes = [CALLBACK, wintypes.LPARAM]
    api.EnumChildWindows.argtypes = [wintypes.HWND, CALLBACK, wintypes.LPARAM]
    return api


def text(handle) -> str:
    api = user32()
    buffer = ctypes.create_unicode_buffer(api.GetWindowTextLengthW(handle) + 1)
    api.GetWindowTextW(handle, buffer, len(buffer))
    return buffer.value


def class_name(handle) -> str:
    buffer = ctypes.create_unicode_buffer(256)
    user32().GetClassNameW(handle, buffer, len(buffer))
    return buffer.value


def dialog_text(handle) -> str:
    """The message of a native MessageBox: the text of its static controls."""
    parts = []

    @CALLBACK
    def child(item, _parameter):
        if class_name(item) == "Static" and text(item).strip():
            parts.append(text(item).strip())
        return True

    user32().EnumChildWindows(handle, child, 0)
    return "\n".join(parts)


def windows(pid: int) -> list[dict]:
    api = user32()
    found = []

    @CALLBACK
    def inspect(handle, _parameter):
        owner = wintypes.DWORD()
        api.GetWindowThreadProcessId(handle, ctypes.byref(owner))
        if owner.value != pid:
            return True
        title = text(handle)
        kind = class_name(handle)
        visible = bool(api.IsWindowVisible(handle))
        if title and (visible or kind in (DIALOG_CLASS, EDITOR_CLASS)):
            row = dict(title=title, visible=visible, class_name=kind, dialog=kind == DIALOG_CLASS)
            if row["dialog"]:
                row["message"] = dialog_text(handle)
            found.append(row)
        return True

    api.EnumWindows(inspect, 0)
    return found
