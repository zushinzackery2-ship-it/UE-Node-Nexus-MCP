"""One explicit filesystem layout drives both admission and snapshot writes."""

from pathlib import Path, PurePosixPath

LINKED_ROOTS = frozenset(("Content", "Tools", "Shaders", "Resources"))


def linkable(relative: str) -> bool:
    return relative.split("/", 1)[0] in LINKED_ROOTS


def parent_directories(key):
    return [parent.as_posix() for parent in PurePosixPath(key).parents if parent.as_posix() != "."]


def copy_layout(plan, subset=None):
    source = Path(plan["project"]).parent if plan.get("project") else None
    candidates = set(subset or ())
    content_directories = plan.get("content_directories", [])
    rows = [dict(source=path, path=path.relative_to(source).as_posix() if source else path.name)
            for path in plan["files"]]
    protected = set()
    for key in candidates:
        protected.update(parent_directories(key))
    for directory in content_directories:
        protected.add(directory)
        protected.update(parent_directories(directory))
    for row in rows:
        private_mount = any(row["path"].startswith(directory + "/") for directory in content_directories)
        row["mode"] = "copy" if subset is None or private_mount or row["path"] in candidates or not linkable(row["path"]) else "link"
        if row["mode"] == "copy" and source:
            protected.update(parent_directories(row["path"]))
    directories, linked_directories = set(protected), set()
    for row in rows:
        if row["mode"] != "link":
            continue
        parents = parent_directories(row["path"])
        directory = next((parent for parent in reversed(parents) if parent not in protected), None)
        row["link_directory"] = directory
        if directory:
            linked_directories.add(directory)
            directories.update(parent_directories(directory))
        else:
            directories.update(parents)
    return dict(rows=rows, directories=sorted(directories), linked_directories=sorted(linked_directories))
