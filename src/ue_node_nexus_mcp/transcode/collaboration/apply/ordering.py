"""Batch execution order: sources before their readers, live referrers deleted first.

The constraints are recorded on the units, so blocking, isolation closures and
execution follow the same edges the order was built from: ``sources`` are the
material-domain assets whose applied content a unit reads, and
``deletion_prerequisites`` the referrers a deleted unit waits for.
"""

from ...sync.dependencies import content_sources, document_dependencies, order_assets
from ..semantic.decode import to_document


def prerequisites(item):
    return set(item["dependencies"]) | set(item.get("sources", ())) | set(item.get("deletion_prerequisites", ()))


def execution_order(workspace, units, current, documents, selected):
    decoded = dict()

    def held(asset):
        # What the editor holds: a source outside the batch is read as it is now.
        if asset not in decoded:
            identifier = current.get(asset)
            snapshot = workspace.store.objects.data(identifier, "snapshot") if identifier else None
            decoded[asset] = (snapshot["semantic"]["kind"], to_document(snapshot)) if snapshot else None
        return decoded[asset]

    for asset, found in content_sources(documents, held, workspace.schema).items():
        units[asset]["sources"] = sorted(found)
    deleted = set(asset for asset, item in units.items()
                  if item["payload"].get("delete_asset") or item["payload"].get("delete_scene"))
    before = dict()
    if deleted:
        for asset in sorted(current.keys() & set(selected)):
            kind, document = held(asset)
            if asset in deleted:
                before[asset] = (kind, document)
            # These are ordering constraints, separate from the revisions read by an apply.
            # A successful earlier deletion has no revision left to put in a read_set.
            for dependency in document_dependencies(document, kind, workspace.schema) & deleted:
                units[dependency].setdefault("deletion_prerequisites", []).append(asset)
    # An asset the editor already holds constrains no creation order unless its
    # interface changes; that is what lets newly declared object contracts be
    # referenced before they are republished in full. Content edges stay.
    available = set(current) - set(asset for asset, item in units.items() if item.get("interface_changed"))
    content = dict((asset, set(item["sources"])) for asset, item in units.items() if item.get("sources"))
    ordered = order_assets(documents, available, workspace.schema, content)
    ordered.extend(asset for asset in units if asset not in documents and asset not in deleted)
    # Every deleted object is present now, but its live references still constrain
    # checkpoint serialization. Creation's available-edge pruning is unsafe here.
    ordered.extend(reversed(order_assets(before, schema=workspace.schema)))
    return ordered
