"""An observer waiting for publication must leave its workspace available."""

from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from threading import Event, current_thread

import pytest

from ue_node_nexus_mcp.transcode.collaboration.store import Store
from ue_node_nexus_mcp.transcode.collaboration.history import History
from ue_node_nexus_mcp.transcode.sync.service import run_sync

from ..fake_bridge import ProtocolUe


@pytest.mark.parametrize("action", ["fetch", "pull"])
@pytest.mark.parametrize("dry_run", [False, True], ids=["execute", "preview"])
def test_waiting_observer_allows_publication_to_update_workspace(tmp_path, monkeypatch, action, dry_run):
    ue = ProtocolUe()
    env = dict(UE_NEXUS_TRANSCODE_DIR=str(tmp_path / "decoded"))
    created = run_sync(ue, "checkout", options=dict(dry_run=False, agent_id="A"), env=env)
    asset = "/Game/Materials/M_Glass.M_Glass"
    path = Path(created["file_paths"][asset])
    original = path.read_text(encoding="utf-8")
    assert "BLEND_Translucent" in original
    path.write_text(original.replace("BLEND_Translucent", "BLEND_Opaque"), encoding="utf-8")
    options = dict(workspace_id=created["id"], dry_run=False)
    committed = run_sync(ue, "commit", options=dict(options, all=True, message="publish"), env=env)

    requested = Event()
    publisher = current_thread()
    lock = Store.lock

    def observing_lock(store, name, timeout=0, **holder):
        if name == "publication" and current_thread() is not publisher:
            requested.set()
        return lock(store, name, timeout, **holder)

    monkeypatch.setattr(Store, "lock", observing_lock)
    observation = []
    with ThreadPoolExecutor(max_workers=1) as executor:
        def before_apply():
            observation.append(executor.submit(run_sync, ue, action,
                                               options=dict(options, dry_run=dry_run), env=env))
            assert requested.wait(10), "observer did not request the publication gate"

        ue.before_apply = before_apply
        published = run_sync(ue, "push", options=options, env=env)
        observed = observation[0].result(timeout=10)

    assert published["status"] == "published", published
    assert published["source_commit"] == committed["commit_id"]
    assert published["workspace_rebase_required"] is False
    store = Store(Path(created["files_root"]).parents[2])
    state = store.record("workspace", created["id"])
    assert History(store).is_ancestor(published["published_commit"], state["base"])
    assert next(row["value"] for row in ue.assets[asset]["props"] if row["name"] == "BlendMode") == "BLEND_Opaque"
    assert run_sync(ue, "status", options=options, env=env)["dirty"] is False
    if dry_run:
        proposal = store.record("proposal", observed["proposal_id"])
        assert proposal["original"]["generation"] == state["generation"]
    elif action == "pull":
        assert observed["status"] != "conflict", observed
