"""Opening the current repository must not request a writer lock."""

import sqlite3
from pathlib import Path
import tempfile
import unittest

from ue_node_nexus_mcp.transcode.collaboration.store.database import Database, FORMAT_VERSION
from ue_node_nexus_mcp.transcode.errors import SyncError
from ue_node_nexus_mcp.transcode.collaboration.store.repository import Store


class DatabaseOpen(unittest.TestCase):
    def test_existing_repository_identity_opens_during_publication_writer(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            project = str(root / "Demo.uproject")
            writer = Store(root / "repo", project)
            with writer.db.connection(write=True) as transaction:
                transaction.execute("INSERT INTO meta VALUES ('inflight', 'pending')")
                reader = Store(root / "repo", project)
                self.assertEqual(reader.project_id, writer.project_id)
                with reader.db.connection() as connection:
                    self.assertIsNone(connection.execute("SELECT value FROM meta WHERE key='inflight'").fetchone())
                reader.db.discard()
            writer.db.discard()

    def test_bound_repository_mismatch_is_rejected_during_writer(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            writer = Store(root / "repo", str(root / "First.uproject"))
            with writer.db.connection(write=True):
                with self.assertRaises(SyncError) as failure:
                    Store(root / "repo", str(root / "Second.uproject"))
                self.assertEqual(failure.exception.code, "project_mismatch")
            writer.db.discard()

    def test_current_schema_reader_opens_during_an_active_publication_writer(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "index.sqlite"
            writer = Database(path)
            with writer.connection(write=True) as transaction:
                transaction.execute("INSERT INTO meta VALUES ('inflight', 'pending')")
                reader = Database(path)
                with reader.connection() as connection:
                    self.assertEqual(connection.execute("PRAGMA user_version").fetchone()[0], FORMAT_VERSION)
                    self.assertIsNone(connection.execute("SELECT value FROM meta WHERE key='inflight'").fetchone())
                reader.discard()
            writer.discard()

    def test_new_repository_creates_complete_current_schema(self):
        with tempfile.TemporaryDirectory() as directory:
            database = Database(Path(directory) / "index.sqlite")
            with database.connection() as connection:
                names = {row[0] for row in connection.execute("SELECT name FROM sqlite_master WHERE type='table'")}
                self.assertTrue({'meta', 'objects', 'refs', 'records', 'events'}.issubset(names))
                self.assertEqual(connection.execute("PRAGMA journal_mode").fetchone()[0], "wal")
            database.discard()

    def test_newer_schema_remains_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "index.sqlite"
            with sqlite3.connect(path) as connection:
                connection.execute(f"PRAGMA user_version={FORMAT_VERSION + 1}")
            connection.close()
            with self.assertRaises(SyncError):
                Database(path)


if __name__ == "__main__":
    unittest.main()
