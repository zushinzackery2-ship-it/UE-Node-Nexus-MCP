"""Only the native default execution edge is implicit in a Pure function."""

from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import capture


def state(flags="Public, Pure", links=""):
    text = ("nexus: 1\nasset: /Game/BP_Pure\nclass: Blueprint\n"
            "[function GetState() -> (State: int) {" + flags + "}]\n" + links)
    return capture(text, None, "pure-regression", "blueprint")["semantic"]


def test_native_default_edge_and_omission_have_one_pure_representation():
    assert state() == state(links="entry.then -> result.execute\n")


def test_impure_default_edge_stays_significant():
    assert state("Public") != state("Public", "entry.then -> result.execute\n")


def test_pure_nondefault_execution_edge_stays_significant():
    assert state() != state(links="entry.then -> other.execute\n")
