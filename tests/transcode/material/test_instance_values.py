"""Reject malformed instance numbers/masks offline and preserve valid spelling variants."""

import pytest

from ue_node_nexus_mcp.transcode.lint.service import lint_document
from ue_node_nexus_mcp.transcode.text.parser import parse


@pytest.mark.parametrize("section,text,valid", [
    ("scalar", "1e+", False), ("scalar", ".", False), ("scalar", "1e1000", False), ("scalar", "-2.5", True),
    ("vector", "(R=1,G=2,B=3,A=1e+)", False), ("vector", "(R=1,G=2,B=3,A=1)", True),
    ("component_mask", "(R=True)", False), ("component_mask", "(R=True,R=False,B=False,A=False)", False),
    ("component_mask", "(R=True,G=typo,B=False,A=False)", False),
    ("component_mask", "(R=1,G=0,B=1,A=0)", True), ("switch", "typo", False),
    ("scalar layer -1", "0.5", False), ("scalar blend 0", "0.5", True),
])
def test_parameter_grammar(section, text, valid):
    document, errors = parse(f"nexus: 1\nasset: /Game/MI\nclass: MaterialInstanceConstant\n\n[{section}]\nValue={text}\n")
    assert not errors.has_errors
    assert lint_document(document, "material_instance", None).has_errors is not valid
