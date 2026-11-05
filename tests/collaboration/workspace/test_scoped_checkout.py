"""A fresh scoped checkout projects live assets, excluding retired history entries.

The observation commit keeps unrelated history, so selecting by root against the
commit would bring back an asset that has since moved away under the same root.
"""

from contextlib import nullcontext
from pathlib import Path
from unittest.mock import Mock, patch

from ue_node_nexus_mcp.transcode.collaboration.service import create_workspace

SCOPE = "ue_node_nexus_mcp.transcode.collaboration.service."


def test_live_root_checkout_does_not_reintroduce_retired_history():
    current = "/Game/Characters/M_Character.M_Character"
    retired = "/Game/OldFolder/M_Character.M_Character"
    context = Mock(bridge_available=False, project=Path("project"), schema=None)
    store = Mock(root=Path("repository"), project_id="project")
    store.lock.side_effect = lambda *args, **kwargs: nullcontext()
    store.objects.data.return_value = {"semantic": {"kind": "material"}}
    history = Mock()
    history.entries.return_value = {current: "current-snapshot", retired: "old-snapshot"}
    with patch(SCOPE + "enabled", return_value=True), patch(SCOPE + "History", return_value=history), \
            patch(SCOPE + "checkout_paths", return_value=[current]), \
            patch(SCOPE + "capture", return_value={"commit": "observed"}), \
            patch(SCOPE + "checkout") as checkout, patch(SCOPE + "atomic_write"), \
            patch(SCOPE + "write_project_info"):
        checkout.return_value.info.return_value = {"id": "workspace"}
        create_workspace(Mock(), context, store, ["/Game"], {"dry_run": False})
    assert checkout.call_args.args[-1] == [current]
