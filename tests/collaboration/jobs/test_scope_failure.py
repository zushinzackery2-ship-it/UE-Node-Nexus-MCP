"""A rejected sync request must release work that was never handed to a task."""

from unittest.mock import Mock

import pytest

from ue_node_nexus_mcp.transcode.sync import background


def test_invalid_option_releases_unsubmitted_scope(tmp_path):
    scope = Mock()
    with pytest.raises(Exception, match="unsupported options"):
        background.run("pull", None, dict(project="Shadetest", workspace_id="scope-test", force="ue"),
                       Mock(), tmp_path, scope)
    scope.release.assert_called_once_with()


def test_store_creation_failure_releases_unsubmitted_scope(tmp_path, monkeypatch):
    scope = Mock()
    monkeypatch.setattr(background, "Store", Mock(side_effect=OSError("test store failure")))
    with pytest.raises(OSError, match="test store failure"):
        background.run("pull", None, dict(project="Shadetest"), Mock(), tmp_path, scope)
    scope.release.assert_called_once_with()
