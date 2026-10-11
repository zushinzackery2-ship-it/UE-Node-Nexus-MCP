"""Disk exhaustion and connection failures preserve healthy repository history."""

import sqlite3

import pytest

from ue_node_nexus_mcp.transcode.collaboration.store.database import Database
from ue_node_nexus_mcp.transcode.errors import SyncError


def test_real_sqlite_full_is_distinct_from_corruption_and_keeps_history(tmp_path):
    database = Database(tmp_path / "index.sqlite")
    with database.connection(write=True) as connection:
        connection.execute("INSERT INTO meta VALUES ('history', 'keep')")
    with database.connection() as connection:
        pages = connection.execute("PRAGMA page_count").fetchone()[0]
        connection.execute(f"PRAGMA max_page_count={pages}")
    try:
        with pytest.raises(SyncError) as failed:
            with database.connection(write=True) as connection:
                connection.execute("INSERT INTO meta VALUES ('large', zeroblob(1048576))")
        assert failed.value.code == "store_full"
        assert failed.value.details["sqlite_errorcode"] == sqlite3.SQLITE_FULL
        assert failed.value.details["sqlite_errorname"] == "SQLITE_FULL"
        assert failed.value.details["path"] == str(database.path)
        with database.connection() as connection:
            assert connection.execute("SELECT value FROM meta WHERE key='history'").fetchone()[0] == "keep"
            assert connection.execute("PRAGMA quick_check").fetchone()[0] == "ok"
            assert not connection.in_transaction
    finally:
        database.discard()


@pytest.mark.parametrize(("native", "code"), [
    (sqlite3.SQLITE_BUSY, "store_busy"),
    (sqlite3.SQLITE_LOCKED | (1 << 8), "store_busy"),
    (sqlite3.SQLITE_READONLY, "store_readonly"),
    (sqlite3.SQLITE_IOERR | (2 << 8), "store_io_error"),
    (sqlite3.SQLITE_CANTOPEN, "store_io_error"),
    (sqlite3.SQLITE_CORRUPT, "store_corrupt"),
    (sqlite3.SQLITE_NOTADB, "store_corrupt"),
])
def test_extended_sqlite_codes_are_preserved(tmp_path, native, code):
    database = Database(tmp_path / "index.sqlite")
    error = sqlite3.OperationalError("native database failure")
    error.sqlite_errorcode = native
    error.sqlite_errorname = "NATIVE_EXTENDED_CODE"
    try:
        with pytest.raises(SyncError) as failed:
            with database.connection():
                raise error
        assert failed.value.code == code
        assert failed.value.details["sqlite_errorcode"] == native
        assert failed.value.details["sqlite_errorname"] == error.sqlite_errorname
    finally:
        database.discard()


def test_connection_open_failure_is_structured(tmp_path, monkeypatch):
    database = Database(tmp_path / "index.sqlite")
    database.discard()
    error = sqlite3.OperationalError("unable to open database file")
    error.sqlite_errorcode = sqlite3.SQLITE_CANTOPEN
    error.sqlite_errorname = "SQLITE_CANTOPEN"

    def denied(*args, **kwargs):
        raise error

    monkeypatch.setattr(sqlite3, "connect", denied)
    with pytest.raises(SyncError) as failed:
        with database.connection():
            pass
    assert failed.value.code == "store_io_error"
