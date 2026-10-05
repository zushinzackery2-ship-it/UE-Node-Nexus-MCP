"""Unreal descriptor syntax must preserve mount enablement and reject malformed input."""

import json

import pytest

from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.safety.isolation.descriptors import normalize, read_descriptor
from ue_node_nexus_mcp.safety.isolation.mounts import plugin_mounts
from ue_node_nexus_mcp.safety.isolation.snapshot import inventory


def test_comments_and_nested_trailing_commas_preserve_literals():
    source = r'''{
        // Unreal permits comments around descriptor members.
        "url": "https://example.test/*literal*/",
        "quote": "escaped\"//string", /* comment */
        "Plugins": [{"Name": "Niagara", "Enabled": true,},],
    }'''
    data = json.loads(normalize(source))
    assert data == dict(url="https://example.test/*literal*/", quote='escaped"//string',
                        Plugins=[dict(Name="Niagara", Enabled=True)])


@pytest.mark.parametrize("source", ['{,}', '[,]', '{"x":,}', '{"x":1,,}',
                                    '{"x":1/*broken}', '{"x":"broken}', '{"x":1 2}', '{"x":1,]'])
def test_invalid_json_is_not_repaired(source, tmp_path):
    path = tmp_path / "Invalid.uplugin"
    path.write_text(source, encoding="utf-8")
    with pytest.raises(InstanceError) as raised:
        read_descriptor(path)
    assert raised.value.code == "isolation_descriptor_invalid"
    assert raised.value.details["path"] == str(path)


def test_real_descriptor_mounts_and_inventory_use_the_same_parser(tmp_path):
    project = tmp_path / "Project/Probe.uproject"
    project.parent.mkdir()
    project.write_text('{"Plugins":[{"Name":"Niagara","Enabled":true,},],}', encoding="utf-8-sig")
    engine = tmp_path / "EngineRoot"
    plugin = engine / "Engine/Plugins/FX/Niagara/Niagara.uplugin"
    plugin.parent.mkdir(parents=True)
    plugin.write_text('{/*content*/"CanContainContent":true,"EnabledByDefault":false,}', encoding="utf-8")
    assert set(plugin_mounts(project, engine)) == {"Niagara"}
    assert inventory(project)["files"] == [project]
