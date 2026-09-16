"""Temporarily pause only the GameThread of a verified repository-owned UE fixture."""

from contextlib import contextmanager
import ctypes
from ctypes import wintypes as w
from pathlib import Path
import threading

from ue_node_nexus_mcp.instances.identity.discovery import arguments
from ue_node_nexus_mcp.instances.identity.paths import project_identity
from ue_node_nexus_mcp.instances.identity.processes import is_alive
from ue_node_nexus_mcp.instances.identity.windows import kernel

ROOT = Path(__file__).resolve().parents[3]


class Entry(ctypes.Structure):
    _fields_ = [(name, w.DWORD) for name in ("size", "usage", "thread", "process")] + [
        ("base_priority", w.LONG), ("delta_priority", w.LONG), ("flags", w.DWORD)]


def game_thread(instance):
    native = kernel()
    for name, args, result in (
        ("CreateToolhelp32Snapshot", [w.DWORD, w.DWORD], w.HANDLE),
        ("Thread32First", [w.HANDLE, ctypes.POINTER(Entry)], w.BOOL),
        ("Thread32Next", [w.HANDLE, ctypes.POINTER(Entry)], w.BOOL),
        ("OpenThread", [w.DWORD, w.BOOL, w.DWORD], w.HANDLE),
        ("GetThreadTimes", [w.HANDLE] + [ctypes.POINTER(w.FILETIME)] * 4, w.BOOL),
        ("GetProcessIdOfThread", [w.HANDLE], w.DWORD),
        ("SuspendThread", [w.HANDLE], w.DWORD), ("ResumeThread", [w.HANDLE], w.DWORD),
    ):
        function = getattr(native, name)
        function.argtypes, function.restype = args, result
    snapshot = native.CreateToolhelp32Snapshot(4, 0)
    assert snapshot != ctypes.c_void_p(-1).value
    found = []
    try:
        entry = Entry()
        entry.size = ctypes.sizeof(entry)
        present = native.Thread32First(snapshot, ctypes.byref(entry))
        while present:
            if entry.process == instance["pid"]:
                handle = native.OpenThread(0x802, False, entry.thread)
                if handle:
                    times = [w.FILETIME() for _ in range(4)]
                    if not native.GetThreadTimes(handle, *[ctypes.byref(value) for value in times]):
                        native.CloseHandle(handle)
                        raise ctypes.WinError(ctypes.get_last_error())
                    created = (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime
                    found.append((created, entry.thread, handle))
            present = native.Thread32Next(snapshot, ctypes.byref(entry))
    finally:
        native.CloseHandle(snapshot)
    assert found, "the owned fixture has no inspectable threads"
    found.sort()
    for _, _, handle in found[1:]:
        native.CloseHandle(handle)
    created, identifier, handle = found[0]
    # UE 5.5's Windows entry point calls GuardedMain/PreInit on its initial
    # thread, where GGameThreadId is assigned. Launcher builds do not publish
    # Win32 thread descriptions, so identify that initial thread by FILETIME.
    if abs(created - int(instance["process_created"])) > 10_000_000:
        native.CloseHandle(handle)
        raise AssertionError("the initial UE thread could not be verified")
    return native, (identifier, handle)


@contextmanager
def paused_game_thread(instance):
    Path(instance["project_path"]).resolve().relative_to((ROOT / "build").resolve())
    assert is_alive(instance)
    projects = [project_identity(arg)["project_key"] for arg in arguments(instance["pid"]) if arg.lower().endswith(".uproject")]
    assert projects == [instance["project_key"]]
    native, (identifier, handle) = game_thread(instance)
    assert native.GetProcessIdOfThread(handle) == instance["pid"]
    previous = native.SuspendThread(handle)
    if previous != 0:
        if previous != 0xFFFFFFFF:
            native.ResumeThread(handle)
        native.CloseHandle(handle)
        raise AssertionError(dict(thread_id=identifier, previous_suspend_count=previous))
    lock = threading.Lock()
    state = dict(thread_id=identifier, resumed=False, deadline_resume=False)

    def resume(deadline=False):
        with lock:
            if not state["resumed"]:
                state.update(resumed=True, deadline_resume=deadline, resume_count=native.ResumeThread(handle))

    timer = threading.Timer(20, resume, args=(True,))
    timer.start()
    try:
        yield state
    finally:
        timer.cancel()
        resume()
        timer.join()
        native.CloseHandle(handle)
        assert state["resume_count"] == 1 and not state["deadline_resume"], state
