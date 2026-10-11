"""New durable edges must name a registered object outside the sweep queue."""

from ...errors import SyncError


def require_registered(connection, identifier):
    row = connection.execute("SELECT 1 FROM objects WHERE id=? AND NOT EXISTS "
        "(SELECT 1 FROM tombstones WHERE id=?)", (identifier, identifier)).fetchone()
    if not row:
        raise SyncError("object_not_found", f"object is not registered: {identifier}", dict(object_id=identifier))
