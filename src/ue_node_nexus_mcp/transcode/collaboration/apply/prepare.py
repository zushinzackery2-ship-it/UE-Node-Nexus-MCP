"""Observe, merge and plan a publication until every precondition was measured."""

from __future__ import annotations

from dataclasses import dataclass, field

from ...sync_project import SyncError
from ..merge.sessions import Sessions
from . import planning
from .observe import capture


@dataclass
class Prepared:
    """A planned publication, or the answer that ends it before execution."""

    observation: dict = field(default_factory=dict)
    merged: dict = field(default_factory=dict)
    batch: dict = field(default_factory=dict)
    session: dict | None = None
    draft: dict | None = None
    answer: dict | None = None


def prepare(bridge, context, workspace, source: str, assets: list[str], paths, options: dict, proposal, original: dict) -> Prepared:
    """The plan decides which revisions UE is asked to verify; those come from UE.

    Remembered evidence describes the project cheaply, but it cannot see a
    compile, which rewrites memory without dirtying a package. When the plan
    guards an asset the observation only carried, the observation is taken again
    with that asset exported. The measured set only grows, so this ends, and
    when the estimate from the submitted state is exact it runs once.
    """
    session = None
    if options.get("merge_id"):
        session = Sessions(workspace).get(options["merge_id"])
        if session["operation"] != "push" or session["metadata"]["source"] != source:
            raise SyncError("stale_session", "push session belongs to another source")
        Sessions(workspace).check(session)
        if session["status"] != "ready":
            return Prepared(answer=Sessions(workspace).report(session))
    dependencies, fresh = planning.scope(workspace, source, assets, explicit=paths is not None)
    while True:
        observation = capture(bridge, context, workspace.store, sorted(set(assets) | dependencies), reference=source,
                              persist=not options.get("dry_run", True), fresh=fresh)
        prepared = plan(workspace, source, assets, paths, options, original, observation, session)
        if prepared.answer is not None:
            return prepared
        missing = planning.guarded(prepared.batch) - set(observation["measured"])
        if not missing:
            break
        fresh |= missing
    confirm(workspace, prepared, proposal, source)
    # Everything this observation measured is current until the first apply
    # commits; from then on only what was measured again after it is.
    observation["current"] = set(observation["measured"])
    return prepared


def plan(workspace, source: str, assets: list[str], paths, options: dict, original: dict, observation: dict, session) -> Prepared:
    merged = planning.merge(workspace, source, observation["commit"], assets)
    identity = dict(deferred=True, source=source, revisions=observation["revisions"], paths=paths, options=options,
                    refs=dict(((original["branch"], original["head"]),)))
    if merged.get("base_pair"):
        return Prepared(answer=ancestors(workspace, merged, source, options, identity, assets))
    prepared = Prepared(observation=observation, merged=merged, session=session)
    if session:
        merged.update(candidate=session["candidates"]["head"], conflicts=[])
    else:
        # The preview reads the very session a real push would open, so the two
        # cannot disagree about scope or count.
        prepared.draft = Sessions(workspace).draft("push", merged["base"], observation["commit"], [observation["commit"], source],
                                                   ours=merged["ours"], selected=assets, **identity)
        merged.update(candidate=prepared.draft["candidates"]["head"], conflicts=prepared.draft["conflicts"])
    prepared.batch = planning.preflight(workspace, merged, observation, assets, options)
    return prepared


def confirm(workspace, prepared: Prepared, proposal, source: str) -> None:
    """The inputs a preview or an open session was computed from are still the ones UE holds."""
    observation = prepared.observation
    if proposal:
        expected = proposal["preview"]
        if expected.get("source") != source or expected.get("target_tree") != observation["tree"] or expected.get("revisions") != observation["revisions"]:
            raise SyncError("stale_proposal", "source or observed UE inputs changed since preview")
    session = prepared.session
    if session and session["metadata"]["revisions"] != observation["revisions"]:
        session["status"] = "stale"
        Sessions(workspace).save(session)
        raise SyncError("stale_session", "UE or dependencies changed during conflict resolution")


def ancestors(workspace, merged: dict, source: str, options: dict, identity: dict, assets: list[str]) -> dict:
    """Independent ancestors disagree; resolve them once and reuse the result."""
    open_session = next((item for item in workspace.store.records("session")
                         if item["workspace_id"] == workspace.state["id"] and item["status"] not in ("completed", "aborted", "stale")
                         and item["metadata"].get("base_pair") == merged["base_pair"]), None)
    if open_session and not options.get("dry_run", True):
        return Sessions(workspace).report(open_session)
    previous, left, right = merged["base_inputs"]
    draft = Sessions(workspace).draft("push", previous, right, [left, right], ours=left, selected=assets,
                                      base_pair=merged["base_pair"], roles=dict(ours="ancestor", theirs="ancestor"), **identity)
    if options.get("dry_run", True):
        return dict(action="push", dry_run=True, status="base-conflict", base_ancestors=merged["ancestors"],
                    conflict_count=len(draft["conflicts"]), conflicts=draft["conflicts"][:40])
    return Sessions(workspace).save(draft)
