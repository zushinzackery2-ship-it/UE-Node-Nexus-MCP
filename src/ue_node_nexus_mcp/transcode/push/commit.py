"""Stage mirror files together; restore replaced files on a filesystem error."""

from __future__ import annotations

from pathlib import Path


def commit_files(contents: dict[Path, str]) -> None:
    originals = dict((path, path.read_bytes() if path.is_file() else None) for path in contents)
    staged: dict[Path, Path] = dict()
    replaced: list[Path] = []
    try:
        for path, text in contents.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            temp = path.with_suffix(path.suffix + ".push-tmp")
            staged[path] = temp
            temp.write_text(text, encoding="utf-8", newline="\n")
        for path, temp in staged.items():
            temp.replace(path)
            replaced.append(path)
    except OSError:
        for path in reversed(replaced):
            previous = originals[path]
            if previous is None:
                path.unlink()
            else:
                staged[path].write_bytes(previous)
                staged[path].replace(path)
        raise
    finally:
        for temp in staged.values():
            temp.unlink(missing_ok=True)
