"""The bridge reports log errors and warnings in one stream; counts keep them apart."""

from ue_node_nexus_mcp.facade_response import diagnostic_counts


def test_log_errors_are_not_counted_as_warnings():
    response = dict(diagnostics=[], warnings=[
        dict(severity="warning", category="LogPython", message="track dropped"),
        dict(severity="error", category="FBXImport", message="import failed"),
        "legacy plain warning",
    ])
    assert diagnostic_counts(response) == {"errors": 1, "warnings": 2}


def test_empty_streams_keep_zero_counts():
    assert diagnostic_counts(dict(diagnostics=[], warnings=[])) == {"errors": 0, "warnings": 0}
