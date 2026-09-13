"""Import the recorded legacy base and preserve every local byte separately."""

from __future__ import annotations

from ...paths import base_path, iter_text_files, parse_text_path, state_path
from ...scene.state import SceneState
from ...sync_project import SyncError, write_project_info
from ..history import History
from ..semantic.snapshot import from_raw
from ..workspace.files import filename
from ..workspace.projection import blob, execute, prepare
from ..workspace.service import Workspace, checkout
from .io import confined, read_json


def enabled(context) -> bool:
    return (context.project / ".nexus" / "collaboration" / "active.json").is_file()


def migrate(store, context) -> dict | None:
    previous = store.record("migration", "legacy")
    if previous and previous.get("phase") == "completed":
        return previous
    history = History(store)
    entries, files, originals = dict(), dict(), dict()
    legacy_state = read_json(state_path(context.project)) if state_path(context.project).is_file() else dict(assets=dict())
    if not isinstance(legacy_state.get("assets"), dict):
        raise SyncError("store_corrupt", "legacy state does not contain an asset index")
    for asset, item in legacy_state["assets"].items():
        base = base_path(context.project, asset)
        if not base.is_file():
            raise SyncError("base_missing", "migration needs the recorded base", dict(asset=asset, file=str(base)))
        snapshot = from_raw(read_json(base), schema=context.schema)
        entries[asset] = store.snapshot(snapshot, context.schema)
        files[asset] = filename(asset, snapshot)
        source = confined(context.project, item["file"])
        originals[files[asset]] = source.read_bytes() if source.is_file() else None
    for item in SceneState.load(context.project).records.values():
        base = confined(context.project, item.base_file)
        if not base.is_file():
            raise SyncError("base_missing", "scene migration needs its recorded base", dict(scene=item.key))
        snapshot = from_raw(read_json(base), schema=context.schema)
        entries[item.key] = store.snapshot(snapshot, context.schema)
        files[item.key] = filename(item.key, snapshot)
        source = confined(context.project, item.file)
        originals[files[item.key]] = source.read_bytes() if source.is_file() else None
    for path in iter_text_files(context.project):
        parsed = parse_text_path(context.project, path)
        if parsed and parsed[0] not in files:
            files[parsed[0]] = path.relative_to(context.project).as_posix()
            originals[files[parsed[0]]] = path.read_bytes()
    if not entries and not files:
        return None
    if previous is None:
        head = history.create(history.tree(entries), [], "Import recorded mirror baseline", operation="migration", schema_key=context.schema_key)
        evidence = dict()
        for directory in (context.project / ".nexus" / "recovery", context.project / ".nexus" / "scenes" / "recovery"):
            for path in directory.glob("*.json"):
                evidence[path.relative_to(context.project).as_posix()] = blob(store, path.read_bytes())
        previous = dict(id="legacy", phase="prepared", head=head, files=files,
                        originals=dict((path, blob(store, value)) for path, value in originals.items()), evidence=evidence,
                        workspace_id="imported", generation=0)
        roots = [head, *filter(None, previous["originals"].values()), *evidence.values()]
        previous["roots"] = roots
        previous["generation"] = store.put_record("migration", "legacy", previous, roots, expected=0)
    if store.record("workspace", "imported"):
        workspace = Workspace(store, "imported", context.schema)
    else:
        workspace = checkout(store, previous["head"], context.schema, agent_id="legacy-import", identifier="imported")
    from ..workspace.projection import content

    journal = store.record("projection", "legacy-import")
    if journal is None:
        after = dict(workspace.state, files=previous["files"])
        texts = dict((path, content(store, value)) for path, value in previous["originals"].items())
        journal = prepare(workspace, after, texts, "migration", "legacy-import")
    execute(workspace, journal)
    if not store.ref("refs/ue/observed"):
        store.move("refs/ue/observed", previous["head"], None, "migration", "recorded base; fetch observes current UE")
    previous["phase"] = "completed"
    store.put_record("migration", "legacy", previous, previous["roots"], previous["generation"])
    write_project_info(context, dict(collaboration_format=1, project_id=store.project_id, imported_workspace="imported"))
    store.event("migration", workspace_id="imported", evidence=len(previous["evidence"]))
    return previous
