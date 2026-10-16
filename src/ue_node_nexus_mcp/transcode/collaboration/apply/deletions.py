"""Delete live referrers before their dependencies, after surviving consumers update."""

from ...sync.dependencies import document_dependencies, order_assets
from ..semantic.decode import to_document


def prerequisites(item):
    return set(item["dependencies"]) | set(item.get("deletion_prerequisites", ()))


def execution_order(workspace, units, current, documents, required, selected):
    deleted = set(asset for asset, item in units.items()
                  if item["payload"].get("delete_asset") or item["payload"].get("delete_scene"))
    before = dict()
    if deleted:
        for asset in sorted(current.keys() & set(selected)):
            identifier = current[asset]
            snapshot = workspace.store.objects.data(identifier, "snapshot")
            kind, document = snapshot["semantic"]["kind"], to_document(snapshot)
            if asset in deleted:
                before[asset] = (kind, document)
            # These are ordering constraints, separate from the revisions read by an apply.
            # A successful earlier deletion has no revision left to put in a read_set.
            for dependency in document_dependencies(document, kind, workspace.schema) & deleted:
                units[dependency].setdefault("deletion_prerequisites", []).append(asset)
    # An asset the editor already holds constrains no creation order unless its
    # interface changes; that is what lets newly declared object contracts be
    # referenced before they are republished in full.
    available = set(current) - set(asset for asset, item in units.items() if item.get("interface_changed"))
    ordered = order_assets(documents, required, available, workspace.schema)
    ordered.extend(asset for asset in units if asset not in documents and asset not in deleted)
    ordered.extend(reversed(order_assets(before, available=available, schema=workspace.schema)))
    return ordered
