import json

import pytest

from ue_node_nexus_mcp.build_info.requirements import CONTRACT_VERSION, VERSION
from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.instances.lifecycle.installation import verify


@pytest.fixture
def installation(tmp_path):
    engine = tmp_path / "EngineBuild"
    engine_manifest = engine / "Engine/Binaries/Win64/UnrealEditor.modules"
    engine_manifest.parent.mkdir(parents=True)
    engine_manifest.write_text(json.dumps(dict(BuildId="expected")))
    project = tmp_path / "Demo.uproject"
    project.write_text("{}")
    plugin = tmp_path / "Plugins/UeNodeNexusBridge"
    binary_root = plugin / "Binaries/Win64"
    binary_root.mkdir(parents=True)
    names = ("UeNodeNexusBridge", "UeNodeNexusGuard")
    metadata = dict(VersionName=VERSION, Modules=[dict(Name=name, LoadingPhase="PostConfigInit") for name in names])
    (plugin / "UeNodeNexusBridge.uplugin").write_text(json.dumps(metadata))
    identity = dict(version=VERSION, contract_version=CONTRACT_VERSION, source_fingerprint="a" * 64)
    (plugin / "BuildIdentity.json").write_text(json.dumps(identity))
    manifest = dict(BuildId="expected", Modules=dict((name, name + ".dll") for name in names))
    (binary_root / "UnrealEditor.modules").write_text(json.dumps(manifest))
    for name in names:
        (binary_root / (name + ".dll")).write_bytes(b"MZ" + ("a" * 64).encode("utf-16le"))
    return dict(project_path=str(project)), engine, plugin


def test_matching_guard_and_core_installation_is_accepted(installation):
    project, engine, _ = installation
    verify(project, engine)


@pytest.mark.parametrize("mutation", ["missing_guard", "wrong_guard", "wrong_engine", "old_contract", "disabled_plugin"])
def test_bad_installation_is_rejected_before_launch(installation, mutation):
    project, engine, plugin = installation
    if mutation == "missing_guard":
        (plugin / "Binaries/Win64/UeNodeNexusGuard.dll").unlink()
    elif mutation == "wrong_guard":
        (plugin / "Binaries/Win64/UeNodeNexusGuard.dll").write_bytes(b"MZold-plugin")
    elif mutation == "wrong_engine":
        (engine / "Engine/Binaries/Win64/UnrealEditor.modules").write_text(json.dumps(dict(BuildId="other")))
    elif mutation == "old_contract":
        (plugin / "BuildIdentity.json").write_text(json.dumps(dict(version="0.5.0", contract_version=3)))
    else:
        from pathlib import Path
        Path(project["project_path"]).write_text(json.dumps(dict(Plugins=[dict(Name="UeNodeNexusBridge", Enabled=False)])))
    with pytest.raises(InstanceError) as caught:
        verify(project, engine)
    assert caught.value.code == ("plugin_disabled" if mutation == "disabled_plugin" else "instance_incompatible")
