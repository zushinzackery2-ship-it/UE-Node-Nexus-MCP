"""Indexed record metadata keeps observers independent of historical payload size."""

from __future__ import annotations

import json

from ...storage.io import canonical

FIELDS = ("workspace_id", "operation", "status", "phase", "asset", "source", "candidate", "target",
          "result_commit", "agent_id", "head", "index", "base", "branch", "sparse", "closed", "schema_key")
DDL = """
CREATE TABLE IF NOT EXISTS record_metadata
    (category TEXT NOT NULL, id TEXT NOT NULL, workspace_id TEXT NOT NULL,
     status TEXT NOT NULL, phase TEXT NOT NULL, summary TEXT NOT NULL,
     PRIMARY KEY(category, id),
     FOREIGN KEY(category, id) REFERENCES records(category, id) ON DELETE CASCADE);
CREATE INDEX IF NOT EXISTS record_metadata_workspace ON record_metadata(category, workspace_id, status, phase);
CREATE INDEX IF NOT EXISTS record_metadata_status ON record_metadata(category, status);
CREATE INDEX IF NOT EXISTS record_metadata_phase ON record_metadata(category, phase);
"""


def migration_sql() -> str:
    fields = ", ".join(f"'{name}', json_extract(payload, '$.{name}')" for name in FIELDS)
    summary = f"json_object({fields}, 'conflict_count', COALESCE(json_array_length(payload, '$.conflicts'), 0), "
    summary += "'file_count', (SELECT count(*) FROM json_each(records.payload, '$.files')))"
    return ("INSERT OR REPLACE INTO record_metadata "
            "SELECT category, id, COALESCE(json_extract(payload, '$.workspace_id'), ''), "
            "COALESCE(json_extract(payload, '$.status'), ''), COALESCE(json_extract(payload, '$.phase'), ''), "
            + summary + " FROM records;\n")


def metadata(payload: dict) -> dict:
    result = dict((name, payload[name]) for name in FIELDS if name in payload)
    result.update(conflict_count=len(payload.get("conflicts") or ()), file_count=len(payload.get("files") or ()))
    return result


def put(connection, category: str, identifier: str, payload: dict) -> None:
    connection.execute("INSERT INTO record_metadata VALUES (?, ?, ?, ?, ?, ?) "
                       "ON CONFLICT(category,id) DO UPDATE SET workspace_id=excluded.workspace_id, "
                       "status=excluded.status, phase=excluded.phase, summary=excluded.summary",
                       (category, identifier, payload.get("workspace_id") or "", payload.get("status") or "",
                        payload.get("phase") or "", canonical(metadata(payload)).decode()))


def query(connection, category: str, *, workspace_id=None, exclude_status=(), exclude_phase=(), summary=False) -> list[dict]:
    conditions, parameters = ["r.category=?"], [category]
    if workspace_id is not None:
        conditions.append("m.workspace_id=?")
        parameters.append(workspace_id)
    for field, excluded in (("status", exclude_status), ("phase", exclude_phase)):
        if excluded:
            conditions.append(f"m.{field} NOT IN ({','.join('?' for _ in excluded)})")
            parameters.extend(excluded)
    projection = "m.summary" if summary else "r.payload"
    sql = f"SELECT r.id, {projection}, r.generation FROM records r "
    sql += "JOIN record_metadata m ON m.category=r.category AND m.id=r.id WHERE "
    sql += " AND ".join(conditions) + " ORDER BY r.updated, r.id"
    return [dict(json.loads(row[1]), id=row[0], generation=row[2]) for row in connection.execute(sql, parameters)]
