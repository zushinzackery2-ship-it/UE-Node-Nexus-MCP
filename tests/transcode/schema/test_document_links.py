"""Schema documentation renders references without opening immutable records."""

from pathlib import Path
from unittest.mock import patch

import pytest

from ue_node_nexus_mcp.transcode.errors import SyncError
from ue_node_nexus_mcp.transcode.schema import catalog, objects


def manifest(entry):
    return dict(key="test", generation=1, tables=dict(asset=dict(Example=entry)))


def entry(file, storage="shared"):
    return dict(file=file, storage=storage, path="/Script/Engine.Example",
                coverage="registered_types", bridge=dict(write="supported"))


def test_documentation_does_not_resolve_or_open_record_paths(tmp_path):
    directory = tmp_path / "catalog"
    row = entry("../.objects/asset/example.json")
    with patch.object(objects, "confined", side_effect=AssertionError("record filesystem access")):
        catalog.markdown(directory, manifest(row))
    content = (directory / "asset/index.md").read_text(encoding="utf-8")
    assert "../../.objects/asset/example.json" in content


def test_legacy_document_links_remain_relative_to_the_category(tmp_path):
    directory = tmp_path / "catalog"
    catalog.markdown(directory, manifest(entry("asset/example.json", "local")))
    assert "(example.json)" in (directory / "asset/index.md").read_text(encoding="utf-8")


@pytest.mark.parametrize("file", ["../.objects/../../outside.json", "C:/outside.json", "/outside.json"])
def test_documentation_rejects_paths_outside_the_object_pool(tmp_path, file):
    with pytest.raises(SyncError):
        catalog.markdown(tmp_path / "catalog", manifest(entry(file)))


def test_real_record_access_still_checks_symlink_confinement(tmp_path):
    pool = tmp_path / ".objects/asset"
    pool.mkdir(parents=True)
    outside = tmp_path / "outside.json"
    outside.write_text("{}", encoding="utf-8")
    target = pool / "escape.json"
    try:
        target.symlink_to(outside)
    except OSError as exc:
        pytest.skip(str(exc))
    with pytest.raises(SyncError, match="escapes"):
        objects.path(tmp_path / "catalog", entry("../.objects/asset/escape.json"))
