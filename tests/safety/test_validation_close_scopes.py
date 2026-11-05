"""A copied-map save can leave a releasing work scope after its receipt returns."""

from pathlib import Path
import threading
import unittest
from unittest.mock import Mock, patch

from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.safety.isolation.session import ValidationSession, drain_released_scopes


MODULE = "ue_node_nexus_mcp.safety.isolation.session"


class ReleasingScope:
    def __init__(self, manager):
        self.manager = manager
        self.scope_id = "copied-map-save"
        self.releasing = True
        self.attempts = 0

    def release(self):
        self.attempts += 1
        if self.attempts >= 2:
            self.manager.scopes.pop(self.scope_id, None)


class CloseScopeTests(unittest.TestCase):
    def test_active_scope_is_preserved_and_rejected(self):
        manager = Mock()
        manager.lock = threading.RLock()
        scope = ReleasingScope(manager)
        scope.releasing = False
        manager.scopes = {scope.scope_id: scope}
        with self.assertRaises(InstanceError) as raised:
            drain_released_scopes(manager, 0)
        self.assertEqual(raised.exception.code, "isolation_active_work")
        self.assertEqual(scope.attempts, 0)
        self.assertIn(scope.scope_id, manager.scopes)

    def test_pending_release_has_a_bounded_deadline(self):
        manager = Mock()
        manager.lock = threading.RLock()
        scope = ReleasingScope(manager)
        manager.scopes = {scope.scope_id: scope}
        with self.assertRaises(InstanceError) as raised:
            drain_released_scopes(manager, 0)
        self.assertEqual(raised.exception.code, "isolation_cleanup_failed")
        self.assertEqual(scope.attempts, 1)
        self.assertIn(scope.scope_id, manager.scopes)

    def test_copied_map_save_drains_its_releasing_scope_before_normal_close(self):
        state = dict(instance_id="copy", pid=123, state="READY", dirty_packages=["/Game/Map"])
        manager = Mock()
        manager.lock = threading.RLock()
        manager.scopes = {}
        manager.current.side_effect = lambda: dict(state)
        manager.status.side_effect = lambda *args, **kwargs: dict(state)
        scope = ReleasingScope(manager)
        saves = []

        def native_call(operation, payload):
            self.assertEqual(operation, "asset_save")
            saves.append(payload["asset_path"])
            state["dirty_packages"] = []
            manager.scopes[scope.scope_id] = scope
            return dict(ok=True)

        def close_editor(current, payload):
            if payload.get("dry_run", True):
                blockers = ["instance_dirty"] if state["dirty_packages"] else []
                if manager.scopes:
                    blockers.append("work_in_progress")
                return dict(instance=dict(state), blockers=blockers)
            if manager.scopes:
                raise InstanceError("instance_in_use", "editor cannot be closed", dict(blockers=["work_in_progress"]))
            return dict(exit_confirmed=True, state="EXITED", exit_code=0)

        session = object.__new__(ValidationSession)
        session.manager, session.project, session.timeout = manager, Path("copied/Example.uproject"), 30
        session.call = native_call
        forced = Mock(return_value=dict(exit_confirmed=True, forced=True, exit_code=88))
        with patch(MODULE + ".close_instance", close_editor), patch(
                "ue_node_nexus_mcp.safety.isolation.process.stop_owned", forced):
            result = session.close()
        self.assertEqual(result.get("exit_code"), 0, result)
        self.assertTrue(result["exit_confirmed"])
        self.assertEqual(saves, ["/Game/Map"])
        self.assertGreaterEqual(scope.attempts, 2)
        forced.assert_not_called()
        manager.close.assert_called_once()


if __name__ == "__main__":
    unittest.main()
