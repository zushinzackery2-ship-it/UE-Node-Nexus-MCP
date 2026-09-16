"""Exercise Windows filesystem aliases and case-sensitive identity rules."""

import ctypes
from ctypes import wintypes as w
import os
import subprocess

import pytest

from ue_node_nexus_mcp.instances.identity.paths import project_identity
from ue_node_nexus_mcp.instances.identity.windows import kernel

pytestmark = pytest.mark.skipif(os.name != "nt", reason="Windows filesystem semantics")


def test_junction_and_short_names_resolve_to_same_project(tmp_path):
    directory = tmp_path / "Physical Project With Spaces"
    directory.mkdir()
    project = directory / "Project With Spaces.uproject"
    project.touch()
    alias = tmp_path / "Junction"
    result = subprocess.run(["cmd.exe", "/d", "/c", "mklink", "/J", str(alias), str(directory)],
                            capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW)
    if result.returncode and b"Local NTFS volumes" in result.stderr:
        pytest.skip("junction requires NTFS; run with --basetemp on an NTFS volume")
    assert result.returncode == 0, result.stderr
    try:
        expected = project_identity(project)["project_key"]
        assert project_identity(alias / project.name)["project_key"] == expected
        assert project_identity(str(project).upper())["project_key"] == expected
        assert project_identity(str(project).replace("\\", "/"))["project_key"] == expected
        api = kernel()
        api.GetShortPathNameW.argtypes = [w.LPCWSTR, w.LPWSTR, w.DWORD]
        api.GetShortPathNameW.restype = w.DWORD
        buffer = ctypes.create_unicode_buffer(32768)
        assert api.GetShortPathNameW(str(project), buffer, len(buffer))
        assert project_identity(buffer.value)["project_key"] == expected
    finally:
        alias.rmdir()


def set_case_sensitive(directory, enabled):
    api = kernel()
    handle = api.CreateFileW(str(directory), 0x180, 7, None, 3, 0x02000000, None)
    assert handle != ctypes.c_void_p(-1).value
    try:
        api.SetFileInformationByHandle.argtypes = [w.HANDLE, ctypes.c_int, w.LPVOID, w.DWORD]
        api.SetFileInformationByHandle.restype = w.BOOL
        flags = w.DWORD(int(enabled))
        if not api.SetFileInformationByHandle(handle, 23, ctypes.byref(flags), 4):
            pytest.skip("case-sensitive directory flag unavailable: " + str(ctypes.get_last_error()))
    finally:
        api.CloseHandle(handle)


def test_case_sensitive_distinct_files_are_not_merged(tmp_path):
    set_case_sensitive(tmp_path, True)
    first = tmp_path / "Game.uproject"
    second = tmp_path / "game.uproject"
    first.write_text("first", encoding="utf-8")
    second.write_text("second", encoding="utf-8")
    assert first.read_text() == "first" and second.read_text() == "second"
    assert project_identity(first)["project_key"] != project_identity(second)["project_key"]


def test_case_sensitive_ancestor_preserves_distinct_project_directories(tmp_path):
    set_case_sensitive(tmp_path, True)
    projects = []
    for name in ("Project", "project"):
        directory = tmp_path / name
        directory.mkdir()
        set_case_sensitive(directory, False)
        project = directory / "Game.uproject"
        project.touch()
        projects.append(project_identity(project))
    assert projects[0]["project_key"] != projects[1]["project_key"], projects
