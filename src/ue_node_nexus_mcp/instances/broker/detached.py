"""Create the shared manager under the user's shell, outside the MCP host's Job."""

import ctypes
from ctypes import wintypes as w
from pathlib import Path
import subprocess

from ..errors import InstanceError
from ..identity.windows import kernel, process_user_sid, user_sid


class Startup(ctypes.Structure):
    _fields_ = [("cb", w.DWORD), ("reserved", w.LPWSTR), ("desktop", w.LPWSTR), ("title", w.LPWSTR)] + [
        (name, w.DWORD) for name in ("x", "y", "width", "height", "columns", "rows", "fill", "flags")] + [
        ("show", w.WORD), ("reserved_size", w.WORD), ("reserved_data", w.LPVOID),
        ("stdin", w.HANDLE), ("stdout", w.HANDLE), ("stderr", w.HANDLE)]


class StartupEx(ctypes.Structure):
    _fields_ = [("startup", Startup), ("attributes", w.LPVOID)]


class Process(ctypes.Structure):
    _fields_ = [("process", w.HANDLE), ("thread", w.HANDLE), ("pid", w.DWORD), ("tid", w.DWORD)]


def api():
    result = kernel()
    for name, arguments, returns in (
        ("InitializeProcThreadAttributeList", [w.LPVOID, w.DWORD, w.DWORD, ctypes.POINTER(ctypes.c_size_t)], w.BOOL),
        ("UpdateProcThreadAttribute", [w.LPVOID, w.DWORD, ctypes.c_size_t, w.LPVOID, ctypes.c_size_t, w.LPVOID, w.LPVOID], w.BOOL),
        ("DeleteProcThreadAttributeList", [w.LPVOID], None),
        ("CreateProcessW", [w.LPCWSTR, w.LPWSTR, w.LPVOID, w.LPVOID, w.BOOL, w.DWORD, w.LPVOID, w.LPCWSTR,
                            ctypes.POINTER(StartupEx), ctypes.POINTER(Process)], w.BOOL),
        ("IsProcessInJob", [w.HANDLE, w.HANDLE, ctypes.POINTER(w.BOOL)], w.BOOL),
        ("ResumeThread", [w.HANDLE], w.DWORD),
        ("TerminateProcess", [w.HANDLE, w.UINT], w.BOOL),
    ):
        function = getattr(result, name)
        function.argtypes, function.restype = arguments, returns
    return result


def shell_parent(native):
    windows = ctypes.WinDLL("user32", use_last_error=True)
    windows.GetShellWindow.restype = w.HWND
    windows.GetWindowThreadProcessId.argtypes = [w.HWND, ctypes.POINTER(w.DWORD)]
    identifier = w.DWORD()
    shell = windows.GetShellWindow()
    if not shell or not windows.GetWindowThreadProcessId(shell, ctypes.byref(identifier)):
        raise InstanceError("manager_launch_unavailable", "a desktop shell is required to host the shared manager outside the MCP Job")
    handle = native.OpenProcess(0x1080, False, identifier.value)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        if process_user_sid(handle) != user_sid():
            raise InstanceError("manager_launch_unavailable", "the desktop shell belongs to a different Windows user")
        # Windows may contain the desktop in its own Job. The extended parent
        # attribute inherits that Job hierarchy, never the caller's MCP Job.
        return handle
    except BaseException:
        native.CloseHandle(handle)
        raise


def launch(command: list[str], root: Path) -> int:
    native = api()
    parent = attributes = None
    process = Process()
    resumed = False
    try:
        parent = shell_parent(native)
        size = ctypes.c_size_t()
        native.InitializeProcThreadAttributeList(None, 1, 0, ctypes.byref(size))
        if not size.value:
            raise ctypes.WinError(ctypes.get_last_error())
        storage = ctypes.create_string_buffer(size.value)
        attributes = ctypes.cast(storage, w.LPVOID)
        if not native.InitializeProcThreadAttributeList(attributes, 1, 0, ctypes.byref(size)):
            attributes = None
            raise ctypes.WinError(ctypes.get_last_error())
        parent_value = w.HANDLE(parent)
        if not native.UpdateProcThreadAttribute(attributes, 0, 0x20000, ctypes.byref(parent_value),
                                                ctypes.sizeof(parent_value), None, None):
            raise ctypes.WinError(ctypes.get_last_error())
        startup = StartupEx()
        startup.startup.cb = ctypes.sizeof(startup)
        startup.startup.flags = 1
        startup.startup.show = 0
        startup.attributes = attributes
        line = ctypes.create_unicode_buffer(subprocess.list2cmdline(command))
        flags = 0x80000 | subprocess.DETACHED_PROCESS | 0x4
        if not native.CreateProcessW(command[0], line, None, None, False, flags, None, str(root),
                                     ctypes.byref(startup), ctypes.byref(process)):
            raise ctypes.WinError(ctypes.get_last_error())
        if process_user_sid(process.process) != user_sid():
            raise InstanceError("manager_launch_unavailable", "the new manager belongs to a different Windows user")
        if native.ResumeThread(process.thread) == 0xFFFFFFFF:
            raise ctypes.WinError(ctypes.get_last_error())
        resumed = True
        return int(process.pid)
    except OSError as error:
        raise InstanceError("manager_launch_unavailable", "Windows refused independent manager creation",
                            dict(winerror=getattr(error, "winerror", None))) from error
    finally:
        if process.process:
            if not resumed:
                native.TerminateProcess(process.process, 78)
                native.WaitForSingleObject(process.process, 5000)
            native.CloseHandle(process.process)
        if process.thread:
            native.CloseHandle(process.thread)
        if attributes:
            native.DeleteProcThreadAttributeList(attributes)
        if parent:
            native.CloseHandle(parent)
