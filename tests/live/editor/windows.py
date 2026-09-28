"""Inspect native windows belonging to an isolated acceptance editor."""

import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import sys
import time


def visible_windows(pid):
    api = ctypes.WinDLL("user32", use_last_error=True)
    api.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    api.GetWindowTextLengthW.argtypes = [wintypes.HWND]
    api.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    api.IsWindowVisible.argtypes = [wintypes.HWND]
    api.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    api.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    found = []

    @callback_type
    def inspect(handle, parameter):
        owner = wintypes.DWORD()
        api.GetWindowThreadProcessId(handle, ctypes.byref(owner))
        if owner.value == pid and api.IsWindowVisible(handle):
            text = ctypes.create_unicode_buffer(api.GetWindowTextLengthW(handle) + 1)
            api.GetWindowTextW(handle, text, len(text))
            rect = wintypes.RECT()
            api.GetWindowRect(handle, ctypes.byref(rect))
            found.append(dict(handle=handle, title=text.value, rect=[rect.left, rect.top, rect.right, rect.bottom]))
        return True

    api.EnumWindows(inspect, 0)
    return found


def verify_visible(session):
    deadline = time.monotonic() + 60
    project_name = session.project.stem.casefold()
    while True:
        status = session.session.status()
        assert status["launch_profile"] == "interactive", status
        windows = visible_windows(status["pid"])
        project_windows = [row for row in windows if project_name in row["title"].casefold()]
        if project_windows:
            session.report("windows", dict(pid=status["pid"], windows=project_windows))
            return
        if status["state"] in ("EXITED", "UNRESPONSIVE") or time.monotonic() >= deadline:
            session.report("windows", dict(pid=status["pid"], windows=windows, instance=status))
            raise AssertionError(f"editor project window did not become visible: {status}")
        time.sleep(0.5)


def answer_dialog(pid, project, dialog):
    """Close the prompt the product reported waiting, as the person at the editor would.

    Only an isolated validation editor is answered, and only the dialog whose
    title the Guard named; an OK-only advisory treats closing as its answer.
    """
    project = Path(project).resolve(strict=True)
    project.relative_to(Path(__file__).resolve().parents[3] / "build")
    rows = [row for row in visible_windows(pid) if row["title"] == dialog["title"]]
    for row in rows:
        ctypes.windll.user32.PostMessageW(row["handle"], 0x0010, 0, 0)
    print(json.dumps(dict(event="dialog_answered", pid=pid, title=dialog["title"], windows=len(rows)), ensure_ascii=False), flush=True)
    return rows


if __name__ == "__main__":
    print(json.dumps(visible_windows(int(sys.argv[1])), ensure_ascii=False))
