from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOTS = (ROOT / "src", ROOT / "Plugins", ROOT / "tests")
SOURCE_SUFFIXES = {".py", ".cpp", ".h", ".cs", ".inl"}
MAX_SOURCE_LINES = 300


def source_files() -> list[Path]:
    return sorted(
        path
        for source_root in SOURCE_ROOTS
        for path in source_root.rglob("*")
        if path.is_file() and path.suffix in SOURCE_SUFFIXES
    )


def test_source_modules_stay_within_line_budget() -> None:
    oversized: dict[str, int] = {}
    for path in source_files():
        line_count = len(path.read_text(encoding="utf-8").splitlines())
        if line_count > MAX_SOURCE_LINES:
            oversized[path.relative_to(ROOT).as_posix()] = line_count

    assert oversized == {}, f"split source files over {MAX_SOURCE_LINES} lines: {oversized}"
