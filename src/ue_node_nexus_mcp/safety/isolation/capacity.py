"""Account for the target filesystem's real allocation before copying inputs."""

import ctypes
from ctypes import wintypes
import os
from pathlib import Path

from ...instances.errors import require


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


def estimate(plan: dict, target: Path, factor: float, reserve: int, baseline=None, subset=None) -> dict:
    """Size the target volume for the files that will really be written.

    ``subset`` names the project-relative inputs copied for real in an overlay
    snapshot; every other input is a link and costs no allocation.
    """
    unit = allocation_unit(target)
    paths = plan["files"]
    if subset is not None:
        root = Path(plan["project"]).parent
        paths = [path for path in paths if path.relative_to(root).as_posix() in subset]
    sizes = [path.stat().st_size for path in paths]
    if baseline:
        for row in baseline.get("files", []):
            if row.get("hash") == "absent":
                continue
            source = Path(row["source"])
            require(source.is_file(), "isolation_baseline_changed", "serialized baseline file is missing", path=str(source))
            sizes.append(source.stat().st_size)
    allocated = sum(((size + unit - 1) // unit) * unit for size in sizes)
    # Directory entries and newly exported mirrors also consume allocation units.
    allocated += len(sizes) * unit
    return dict(allocation_unit_bytes=unit, input_allocation_bytes=allocated,
                required_bytes=int(max(sum(sizes), allocated) * factor) + reserve)
