"""Read bounded evidence from an exact editor log or an unbound project history."""

from pathlib import Path
import re


ABSLOG = re.compile(r'(?:^|\s)(?:"-abslog=([^"]+)"|-abslog="([^"]+)"|-abslog=(\S+))', re.IGNORECASE)


def read_latest_project_log(project_context: dict | None, tail_bytes: int = 512 * 1024) -> tuple[str, Path | None]:
    data = project_context.get("data") if isinstance(project_context, dict) else None
    if not isinstance(data, dict):
        return "", None
    declared = ABSLOG.search(data.get("command_line") or "")
    exact = next((part for part in declared.groups() if part), None) if declared else data.get("log_file_path")
    if exact:
        path = Path(exact)
    else:
        saved = data.get("project_saved_dir")
        project = data.get("project_file_path")
        if not saved and not project:
            return "", None
        directory = Path(saved) if saved else Path(project).parent / "Saved"
        candidates = [path for folder in (directory / "Logs", directory / "Nexus/Logs")
                      for path in folder.glob("*.log") if path.is_file()]
        path = max(candidates, key=lambda entry: entry.stat().st_mtime) if candidates else None
    if path is None:
        return "", None
    try:
        with path.open("rb") as handle:
            handle.seek(max(0, path.stat().st_size - tail_bytes))
            return handle.read(tail_bytes).decode("utf-8", errors="replace"), path
    except FileNotFoundError:
        return "", path
