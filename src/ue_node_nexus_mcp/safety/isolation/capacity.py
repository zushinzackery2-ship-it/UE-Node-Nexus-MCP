"""Account for the target filesystem's real allocation before copying inputs."""

import ctypes
import os
import shutil
import time
from ctypes import wintypes
from pathlib import Path

from ...instances.errors import require
from .layout import copy_layout


def allocation_unit(path: Path) -> int:
    existing = path.resolve()
    while not existing.exists():
        existing = existing.parent
    if os.name != "nt":
        return os.statvfs(existing).f_frsize
    from ...instances.identity.windows import kernel

    api = kernel()
    volume = ctypes.create_unicode_buffer(32768)
    api.GetVolumePathNameW.argtypes = [wintypes.LPCWSTR, wintypes.LPWSTR, wintypes.DWORD]
    api.GetVolumePathNameW.restype = wintypes.BOOL
    if not api.GetVolumePathNameW(str(existing), volume, len(volume)):
        raise ctypes.WinError(ctypes.get_last_error())
    values = [wintypes.DWORD() for _ in range(4)]
    api.GetDiskFreeSpaceW.argtypes = [wintypes.LPCWSTR] + [ctypes.POINTER(wintypes.DWORD)] * 4
    api.GetDiskFreeSpaceW.restype = wintypes.BOOL
    if not api.GetDiskFreeSpaceW(volume.value, *[ctypes.byref(value) for value in values]):
        raise ctypes.WinError(ctypes.get_last_error())
    return values[0].value * values[1].value


def estimate(plan: dict, target: Path, factor: float, reserve: int, baseline=None, subset=None, *, layout=None) -> dict:
    """Count mandatory real files, each planned link and target allocation units."""
    unit = allocation_unit(target)
    layout = layout if layout is not None else copy_layout(plan, subset)
    paths = [row["source"] for row in layout["rows"] if row["mode"] == "copy"]
    sizes = [path.stat().st_size for path in paths]
    copy_bytes = sum(sizes)
    if baseline:
        for row in baseline.get("files", []):
            if row.get("hash") == "absent":
                continue
            source = Path(row["source"])
            require(source.is_file(), "isolation_baseline_changed", "serialized baseline file is missing", path=str(source))
            sizes.append(source.stat().st_size)
    allocated = sum(((size + unit - 1) // unit) * unit for size in sizes)
    linked = [row for row in layout["rows"] if row["mode"] == "link"]
    link_entries = len(layout["linked_directories"]) + sum(not row["link_directory"] for row in linked)
    allocated += (len(sizes) + len(layout["directories"]) + link_entries) * unit
    existing = target.absolute()
    while not existing.exists():
        existing = existing.parent
    measured_at = time.time()
    free = shutil.disk_usage(existing).free
    source_root = Path(plan["project"]).anchor if plan.get("project") else target.anchor
    return dict(allocation_unit_bytes=unit, input_allocation_bytes=allocated,
                required_bytes=int(max(sum(sizes), allocated) * factor) + reserve,
                copy_bytes=copy_bytes, copied_files=len(paths), linked_files=len(linked),
                linked_directories=len(layout["linked_directories"]), link_entries=link_entries,
                directory_entries=len(layout["directories"]), cross_volume_copy_bytes=copy_bytes
                if source_root.casefold() != target.anchor.casefold() else 0,
                target_path=str(target.absolute()), measured_path=str(existing),
                measured_at=measured_at, free_bytes=free)
