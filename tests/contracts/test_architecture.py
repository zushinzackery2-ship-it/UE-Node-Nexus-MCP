"""Guard module boundaries that keep catalogs usable without sync orchestration."""

from tests.compile_check.check_sources import PACKAGE
from tests.compile_check.import_graph import cycles, dependencies


def test_package_imports_are_acyclic_and_transcode_has_no_deferred_cycles():
    graph = dependencies(PACKAGE.parent)
    assert cycles(graph) == []
    graph = dependencies(PACKAGE.parent, deferred=True)
    prefix = "ue_node_nexus_mcp.transcode."
    mirror = dict((name, set(target for target in targets if target.startswith(prefix)))
                  for name, targets in graph.items() if name.startswith(prefix))
    assert cycles(mirror) == []
    for name, targets in mirror.items():
        if name.startswith((prefix + "schema.", prefix + "storage.", prefix + "text.")):
            assert not any(target.startswith((prefix + "sync.", prefix + "collaboration.")) for target in targets), (name, targets)
