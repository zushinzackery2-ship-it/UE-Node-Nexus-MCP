"""Install a self-contained stdlib-only broker archive outside agent workspaces."""

from __future__ import annotations

import hashlib
from pathlib import Path
import sys
import uuid
import zipfile
import json

from ...coordination.file_lock import FileLock
from .registry import atomic_json


def sources(package: Path) -> list[Path]:
    return sorted(path for path in package.rglob("*.py") if "__pycache__" not in path.parts)


def digest(package: Path) -> str:
    """The archive name a package's sources install under; equal sources share one archive."""
    value = hashlib.sha256()
    for path in sources(package):
        value.update(path.relative_to(package).as_posix().encode())
        value.update(path.read_bytes())
    return value.hexdigest()


def install(root: Path) -> list[str]:
    source = Path(__file__).resolve().parents[2]
    files = sources(source)
    archive = root / "Packages" / (digest(source) + ".pyz")
    with FileLock(root / "install.lock", timeout=15):
        launcher = root / "launcher.json"
        previous = json.loads(launcher.read_text(encoding="utf-8")) if launcher.is_file() else dict()
        if not archive.is_file():
            archive.parent.mkdir(parents=True, exist_ok=True)
            temporary = archive.with_suffix("." + uuid.uuid4().hex + ".tmp")
            with zipfile.ZipFile(temporary, "x", compression=zipfile.ZIP_DEFLATED) as package:
                for path in files:
                    package.write(path, "ue_node_nexus_mcp/" + path.relative_to(source).as_posix())
                package.writestr("__main__.py", "from ue_node_nexus_mcp.instances.broker.main import main\nmain()\n")
            temporary.replace(archive)
        command = [str(Path(sys._base_executable).resolve()), "-I", str(archive), "--runtime-dir", str(root)]
        atomic_json(root / "launcher.json", dict(command=command + ["--detached"], protocol=1))
        protected = set(previous.get("command", [])) | set(command)
        packages = sorted(archive.parent.glob("*.pyz"), key=lambda path: path.stat().st_mtime, reverse=True)
        for old in packages[2:]:
            if str(old) not in protected:
                old.unlink()
    return command
