"""Start the isolated editor every way issue 4 #1 names and check what the product reports.

An isolated test plugin opens UE's "Low Drive Space" advisory through the real
editor handler before the engine loop is ready. A launch the Guard knows nobody sees (managed,
hidden, offscreen) is confirmed by the Guard; a visible external launch waits for
its user, is reported as waiting, and continues once answered. Disk locations still
come from real measurements; no system drive is filled to provoke a warning.
"""

from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import time

from ue_node_nexus_mcp.instances.broker.client import BrokerClient
from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.instances.session.binding import EditorSession as ManagedSession
from tests.instances.live.protection import close_clean
from tests.live.editor.windows import answer_dialog

SW_HIDE, SW_SHOWNORMAL = 0, 1
# UUnrealEdEngine::ValidateFreeDiskSpace: engine and project directories 5120 MB, user directory 1024 MB.
RECOMMENDED_MB = dict(engine=5120, project=5120, user=1024)
START_SECONDS = 600


def short_of_space(engine: Path, project: Path) -> set[str]:
    """The locations UE will report, measured where it measures them."""
    places = dict(engine=engine / "Engine/Binaries/Win64", project=project.parent, user=Path.home() / "Documents")
    return set(role for role, path in places.items() if shutil.disk_usage(path).free // 2 ** 20 < RECOMMENDED_MB[role])


class Launches:
    def __init__(self, project: Path, engine: Path) -> None:
        self.project, self.engine = project, engine
        self.root = project.parent / "Runtime"
        self.logs = project.parent / "Logs"
        self.expected = short_of_space(engine, project)

    def session(self) -> ManagedSession:
        return ManagedSession(BrokerClient(self.root, str(self.project.parent)), str(self.project))

    def acknowledged(self, startup: dict, window: str) -> list[dict]:
        """A start nobody sees: ready, with the advisory confirmed and its locations named."""
        assert startup["outcome"] == "ready", startup
        assert startup["progress"]["launch_window"] == window, startup
        notices = [item for item in startup.get("acknowledged_dialogs", ()) if item.get("code") == "low_drive_space"]
        assert notices, startup
        assert any("acceptance fixture" in notice["message"] for notice in notices), notices
        for notice in notices:
            assert set(item["role"] for item in notice["locations"]) == self.expected, (notice, self.expected)
            assert all(item["free_mb"] < item["recommended_mb"] for item in notice["locations"]), notice
        return notices

    def launch(self, name: str, show: int) -> tuple[subprocess.Popen, object]:
        """An editor started outside Nexus, the way ``Start-Process -WindowStyle`` starts it."""
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = show
        output = (self.logs / f"{name}-console.log").open("ab")
        command = [str(self.engine / "Engine/Binaries/Win64/UnrealEditor.exe"), str(self.project), "-d3d12", "-nosplash", "-nosound",
                   "-NexusRuntime=" + str(self.root), "-abslog=" + str(self.logs / f"{name}.log")]
        process = subprocess.Popen(command, cwd=str(self.project.parent), stdout=output, stderr=subprocess.STDOUT,
                                   stdin=subprocess.DEVNULL, startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
        return process, output

    def hold(self, session: ManagedSession, process: subprocess.Popen) -> dict:
        """The call an agent makes: ensure, held until the start is no longer merely loading."""
        deadline = time.monotonic() + START_SECONDS
        while time.monotonic() < deadline:
            assert process.poll() is None, process.returncode
            try:
                result = session.ensure(dict(dry_run=False, wait_seconds=120))
            except InstanceError as error:
                # The process is an editor instance once its Guard published the record.
                assert error.code in ("instance_unverified", "instance_missing"), error.envelope()
                time.sleep(1)
                continue
            if result["startup"]["outcome"] != "starting":
                return result
        raise AssertionError("the external editor did not finish starting")

    def close(self, session: ManagedSession, instance: dict) -> dict:
        if instance["ownership"] == "external":
            session.call("adopt", dict(instance_id=instance["instance_id"], process_created=instance["process_created"],
                                       project_path=str(self.project), reason="owned isolated acceptance fixture",
                                       protected=False, dry_run=False))
        session.release()
        final = close_clean(session, instance["instance_id"])
        assert final["state"] == "EXITED" and final.get("exit_code") == 0, final
        return final

    def external(self, name: str, show: int, scenario) -> dict:
        process, output = self.launch(name, show)
        session = self.session()
        try:
            return scenario(session, process)
        finally:
            # A scenario ends with the editor closed; one that failed leaves it to be stopped here.
            if process.poll() is None:
                process.kill()
                process.wait(60)
            session.close()
            output.close()

    def managed(self, harness) -> dict:
        """Started by Nexus itself (issue case: managed visible launch)."""
        notices = self.acknowledged(harness.startup, "normal")
        return dict(instance=harness.session.current()["instance_id"], startup=harness.startup, acknowledged=notices)

    def hidden(self) -> dict:
        """Started hidden outside Nexus (the reported case): the prompt could never be seen."""
        def scenario(session, process):
            result = self.hold(session, process)
            notices = self.acknowledged(result["startup"], "hidden")
            final = self.close(session, result["instance"])
            return dict(pid=process.pid, startup=result["startup"], acknowledged=notices, final=final)

        return self.external("ExternalHidden", SW_HIDE, scenario)

    def answered(self) -> dict:
        """Started visibly outside Nexus: its user sees the prompt; the agent is told it waits."""
        def scenario(session, process):
            first = self.hold(session, process)
            waiting, instance = first["startup"], first["instance"]
            dialog = waiting["blocking_dialog"]
            assert waiting["outcome"] == "waiting_for_user" and "answer" in waiting["next"], waiting
            assert dialog["code"] == "low_drive_space" and dialog["buttons"] == "ok" and dialog["source"] == "message_dialog", dialog
            assert set(item["role"] for item in dialog["locations"]) == self.expected, dialog
            assert instance["state"] == "STARTING" and instance["waiting_for_user"] is True, instance
            assert dialog["title"] in instance["window_titles"], instance["windows"]
            records = []
            while first["startup"]["outcome"] == "waiting_for_user":
                current = first["startup"]["blocking_dialog"]
                assert current["code"] == "low_drive_space" and current["buttons"] == "ok", current
                assert answer_dialog(process.pid, self.project, current), current
                deadline = time.monotonic() + 30
                while True:
                    status = session.status()
                    record = [item for item in status.get("dialog_notices", ()) if item["id"] == current["id"]]
                    if record and record[0].get("resolution") == "answered":
                        records.append(record[0])
                        break
                    assert process.poll() is None and time.monotonic() < deadline, status
                    time.sleep(0.25)
                first = self.hold(session, process)
            ready = first
            assert ready["startup"]["outcome"] == "ready", ready["startup"]
            assert ready["instance"]["instance_id"] == instance["instance_id"] and ready["instance"]["pid"] == process.pid
            assert any("acceptance fixture" in item["message"] for item in records), records
            final = self.close(session, ready["instance"])
            return dict(pid=process.pid, waiting=waiting, windows=instance["windows"], answered=records, final=final)

        return self.external("ExternalVisible", SW_SHOWNORMAL, scenario)

    def exited_while_waiting(self) -> dict:
        """The user closes the editor instead of answering: the end names the prompt it waited on."""
        def scenario(session, process):
            first = self.hold(session, process)
            dialog = first["startup"].get("blocking_dialog")
            assert first["startup"]["outcome"] == "waiting_for_user" and dialog, first["startup"]
            process.kill()
            process.wait(60)
            deadline = time.monotonic() + 30
            while True:
                status = session.status(dict(wait_seconds=0))
                if status["state"] == "EXITED":
                    break
                assert time.monotonic() < deadline, status
                time.sleep(0.25)
            assert status["state"] == "EXITED" and status["startup"]["outcome"] == "exited", status
            assert status["startup"]["exited_while_waiting"]["title"] == dialog["title"], status["startup"]
            assert status["waiting_for_user"] is False and status["blocking_dialog"] is None, status
            return dict(pid=process.pid, dialog=dialog, status=status["startup"])

        return self.external("ExternalExit", SW_SHOWNORMAL, scenario)
