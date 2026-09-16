"""Resolve existing UE bindings before establishing a project's default mirror."""

from contextlib import closing
import json
from pathlib import Path
import sqlite3

from ...coordination.file_lock import FileLock
from ..broker.registry import atomic_json
from ..errors import InstanceError, require
from ..identity.paths import canonical_path, project_identity


def read_json(path: Path) -> dict:
    if not path.is_file():
        return dict()
    try:
        return json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, ValueError) as exc:
        raise InstanceError("repository_unverified", "cannot read existing repository binding", dict(path=str(path))) from exc


def validate_repository(repository: Path, project: dict, expected_id: str) -> None:
    database = repository / "index.sqlite"
    require(database.is_file(), "repository_missing", "existing collaboration history is unavailable", repository=str(repository))
    try:
        with closing(sqlite3.connect(database.as_uri() + "?mode=ro", uri=True, timeout=2)) as connection:
            metadata = dict(connection.execute("SELECT key, value FROM meta"))
    except sqlite3.Error as exc:
        raise InstanceError("repository_unverified", "cannot verify existing collaboration database", dict(repository=str(repository))) from exc
    require(metadata.get("project_id") == expected_id, "repository_mismatch", "collaboration project id differs from UE's binding")
    require(bool(metadata.get("project_file")), "repository_unverified", "repository has no recorded project identity")
    require(project_identity(metadata["project_file"])["project_key"] == project["project_key"],
            "repository_mismatch", "collaboration history belongs to a different .uproject")


def resolve(project: dict, runtime: Path, requested: str | None = None, persist: bool = False) -> dict:
    record_path = runtime / "Projects" / (project["project_key"] + ".json")
    with FileLock(runtime / "ProjectBindings" / (project["project_key"] + ".lock"), timeout=5):
        stored = read_json(record_path)
        ue = read_json(Path(project["project_path"]).parent / "Saved/Nexus/collaboration-binding.json")
        project_name = project["project_name"]
        project_id = None
        if ue:
            repository = Path(ue["repository"]).resolve()
            require(repository.name == "collaboration" and repository.parent.name == ".nexus",
                    "repository_layout_mismatch", "existing repository must use the project's .nexus/collaboration layout")
            validate_repository(repository, project, ue["project_id"])
            mirror = repository.parents[2]
            project_name = repository.parents[1].name
            project_id = ue["project_id"]
        elif stored:
            mirror = Path(stored["mirror_root"])
            repository = Path(stored["repository"])
            project_name = stored["mirror_project_name"]
        else:
            mirror = Path(requested).expanduser().resolve() if requested else Path(project["project_path"]).parent / "Saved/Nexus/Content_Transcoded"
            repository = mirror / project_name / ".nexus/collaboration"
        if requested:
            require(canonical_path(requested, must_exist=False) == canonical_path(mirror, must_exist=False),
                    "repository_mismatch", "this project already uses a different mirror root", mirror_root=str(mirror), repository=str(repository))
        result = dict(project_key=project["project_key"], project_path=project["project_path"], mirror_root=str(mirror),
                      mirror_project_name=project_name, repository=str(repository), collaboration_project_id=project_id)
        if persist and result != stored:
            atomic_json(record_path, result)
        return result
