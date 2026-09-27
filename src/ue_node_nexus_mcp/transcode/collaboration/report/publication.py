"""Explain which side contributes to a publication and which side it updates."""

from ..history.query import diff


def describe(workspace, merged, assets, paths, options) -> dict:
    rows = []
    for origin, before, role in (("ue", merged["ours"], "workspace"), ("workspace", merged["theirs"], "ue")):
        for row in diff(workspace.history, before, merged["candidate"], assets):
            row.update(origin=origin, comparison=dict(before=role, after="candidate"))
            rows.append(row)
    adopted = sorted(row["asset"] for row in rows if row["origin"] == "ue")
    warnings = []
    if adopted:
        override = dict((key, item) for key, item in options.items() if key not in ("merge_id", "proposal_id"))
        override.update(workspace_id=workspace.state["id"], force="local", dry_run=True)
        warnings.append(dict(code="ue_drift_adopted", severity="warning", assets=adopted,
                             message="UE changes contribute to the candidate; an empty plan means those values are already in UE",
                             force_local=dict(action="push", paths=paths, options=override)))
    return dict(changes=rows, warnings=warnings)
