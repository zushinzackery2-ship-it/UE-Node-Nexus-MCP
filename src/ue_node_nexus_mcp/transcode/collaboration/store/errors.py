"""Classify SQLite primary codes while retaining their extended diagnostics."""

import logging
import sqlite3

from ...errors import SyncError

LOG = logging.getLogger(__name__)
CODES = {
    sqlite3.SQLITE_BUSY: "store_busy",
    sqlite3.SQLITE_LOCKED: "store_busy",
    sqlite3.SQLITE_FULL: "store_full",
    sqlite3.SQLITE_READONLY: "store_readonly",
    sqlite3.SQLITE_IOERR: "store_io_error",
    sqlite3.SQLITE_CANTOPEN: "store_io_error",
    sqlite3.SQLITE_CORRUPT: "store_corrupt",
    sqlite3.SQLITE_NOTADB: "store_corrupt",
}


def database_error(error, path):
    native = getattr(error, "sqlite_errorcode", None)
    name = getattr(error, "sqlite_errorname", None)
    code = CODES.get(native & 0xFF if isinstance(native, int) else None, "store_error")
    details = dict(path=str(path), sqlite_errorcode=native, sqlite_errorname=name)
    LOG.error("repository_database_error code=%s path=%s sqlite_errorcode=%s sqlite_errorname=%s message=%s",
              code, path, native, name, error)
    return SyncError(code, str(error), details)
