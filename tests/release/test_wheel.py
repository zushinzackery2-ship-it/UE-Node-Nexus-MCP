"""Reject stale package members left over from an earlier source layout."""

import json
import zipfile

import pytest

from release import package


def wheel(tmp_path, monkeypatch, extra=None):
    monkeypatch.setattr(package, "ROOT", tmp_path)
    source = tmp_path / "src/ue_node_nexus_mcp"
    files = dict((f"transcode/push/{name}.py", b"pass\n")
                 for name in ("prepare", "apply", "commit", "diagnostics", "recovery"))
    files["__init__.py"] = b""
    files["operations.json"] = json.dumps(dict(files=["operations/instances.json"])).encode()
    files["operations/instances.json"] = b"{}"
    path = tmp_path / "delivery.whl"
    with zipfile.ZipFile(path, "w") as archive:
        archive.writestr("ue_node_nexus_mcp-0.6.0.dist-info/METADATA", "Version: 0.6.0\n")
        archive.writestr("ue_node_nexus_mcp-0.6.0.dist-info/entry_points.txt",
                         "[console_scripts]\nue-node-nexus-mcp = ue_node_nexus_mcp.server:main\n"
                         "ue-node-nexus-manager = ue_node_nexus_mcp.instances.broker.main:main\n")
        for name, value in files.items():
            target = source / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(value)
            archive.writestr("ue_node_nexus_mcp/" + name, value)
        if extra:
            archive.writestr("ue_node_nexus_mcp/" + extra, b"stale build output")
    return path


def test_current_source_wheel_is_accepted(tmp_path, monkeypatch):
    package.verify_wheel(wheel(tmp_path, monkeypatch), "0.6.0")


@pytest.mark.parametrize("extra", ["transcode/old_flat_module.py", "unowned-cache.bin"])
def test_stale_package_files_are_rejected(tmp_path, monkeypatch, extra):
    with pytest.raises(RuntimeError, match="unexpected package files"):
        package.verify_wheel(wheel(tmp_path, monkeypatch, extra), "0.6.0")
