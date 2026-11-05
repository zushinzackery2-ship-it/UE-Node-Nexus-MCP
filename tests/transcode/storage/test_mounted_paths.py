"""Project mounts other than ``/Game`` share one path, selection, lint and dependency codec.

``/Game`` keeps its historical file names; every other project-local mount lives
under ``@mounts/<Root>/``. ``@`` is illegal in UE package names, so the two
namespaces cannot collide.
"""

import pytest

from ue_node_nexus_mcp.transcode.collaboration.workspace.files import filename, select
from ue_node_nexus_mcp.transcode.errors import SyncError
from ue_node_nexus_mcp.transcode.lint.service import lint_document
from ue_node_nexus_mcp.transcode.storage.io import confined
from ue_node_nexus_mcp.transcode.storage.paths import base_path, parse_text_path, text_path
from ue_node_nexus_mcp.transcode.sync.dependencies.values import value_dependencies
from ue_node_nexus_mcp.transcode.text.parser import parse

GAME = "/Game/Rendering/M_Probe.M_Probe"
TESTS = "/Tests/Rendering/M_Probe.M_Probe"
FILES = {
    GAME: "Rendering/M_Probe.mat.nexus",
    TESTS: "@mounts/Tests/Rendering/M_Probe.mat.nexus",
}


@pytest.mark.parametrize("root, expected", [("/Game", GAME), ("/Tests", TESTS)])
@pytest.mark.parametrize("suffix", ["", "/"])
def test_bare_roots_select_registered_assets(tmp_path, root, expected, suffix):
    assert select(tmp_path, FILES, [root + suffix]) == [expected]


@pytest.mark.parametrize("root, expected", [("/Game", GAME), ("/Tests", TESTS)])
def test_package_object_folder_and_disk_selection(tmp_path, root, expected):
    for spelling in (root + "/Rendering", root + "/Rendering/M_Probe", expected,
                     FILES[expected], str(tmp_path / FILES[expected])):
        assert select(tmp_path, FILES, [spelling]) == [expected], spelling


def test_roundtrip_has_no_game_collision(tmp_path):
    for asset, relative in FILES.items():
        path = text_path(tmp_path, asset, "material")
        assert path == tmp_path / relative
        assert parse_text_path(tmp_path, path) == (asset, "material")
        assert base_path(tmp_path, asset).is_relative_to(tmp_path / ".nexus/base")
    assert base_path(tmp_path, GAME) != base_path(tmp_path, TESTS)


def test_other_mounts_share_the_same_codec(tmp_path):
    asset = "/Authoring/References/M_Shared.M_Shared"
    path = text_path(tmp_path, asset, "material")
    assert path.relative_to(tmp_path).as_posix() == "@mounts/Authoring/References/M_Shared.mat.nexus"
    assert parse_text_path(tmp_path, path) == (asset, "material")


@pytest.mark.parametrize("path", ["/Gam", "/Games", "/Testing", "/Missing"])
def test_unknown_root_does_not_select_prefix_neighbours(tmp_path, path):
    with pytest.raises(SyncError):
        select(tmp_path, FILES, [path])


@pytest.mark.parametrize("path", [
    "/Tests/../Escape", "/Game/Rendering/../../Escape", "/Tests//Escape", "/Tests/./Escape",
    "/Tests/C:/Escape", "/Tests/Rendering/M:stream", "/Tests/Rendering/", "/Game/@mounts/Tests/M",
    "/Tests/Rendering\\Escape",
])
def test_invalid_package_cannot_escape_or_alias_a_valid_asset(tmp_path, path):
    with pytest.raises(ValueError):
        text_path(tmp_path, path, "material")


@pytest.mark.parametrize("path", ["/Engine/EngineMaterials/DefaultMaterial", "/Script/Engine.Actor", "/Temp/Probe"])
def test_reserved_engine_roots_remain_external_dependencies(path):
    assert value_dependencies(path, TESTS, "reference") == set()


def test_workspace_confinement_is_preserved(tmp_path):
    for path in ("../outside", str(tmp_path.parent / "outside"), "/Tests/../../outside"):
        with pytest.raises(SyncError):
            confined(tmp_path, path)
    assert parse_text_path(tmp_path, tmp_path.parent / "Outside.mat.nexus") is None


def test_mounted_header_passes_structural_lint():
    text = "nexus: 1\nasset: /Tests/Rendering/M_Probe\nclass: Material\n\n[asset]\n\n[graph]\n"
    document, parsed = parse(text)
    assert not parsed.has_errors
    sink = lint_document(document, "material", None)
    assert not sink.has_errors, [item.format() for item in sink.errors()]


@pytest.mark.parametrize("spelling", [TESTS, "Material'" + TESTS + "'", "Object(" + TESTS + ")"])
def test_mounted_dependencies_and_typed_plain_values(spelling):
    assert value_dependencies(spelling, GAME, "reference") == {TESTS}
    assert value_dependencies(spelling, GAME, "plain") == set()


def test_own_subobject_reference_is_not_a_dependency():
    assert value_dependencies(TESTS + ":MaterialExpression_0", TESTS) == set()


@pytest.mark.parametrize("root, relative", [
    ("/Game", "Scenes/Maps/L_Probe/Actors.scene.nexus"),
    ("/Tests", "Scenes/@mounts/Tests/Maps/L_Probe/Actors.scene.nexus"),
])
def test_scene_names_use_the_same_mount_namespace(tmp_path, root, relative):
    key = root + "/Maps/L_Probe#Actors"
    assert filename(key, {"semantic": {"kind": "scene"}}) == relative
    assert select(tmp_path, {key: relative}, [root]) == [key]
