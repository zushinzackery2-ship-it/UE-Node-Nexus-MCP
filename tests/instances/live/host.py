"""Stage the verified plugins and a blank project without touching user editors."""

from pathlib import Path
import shutil

from tests.live.editor.prepare import stage_plugins

ROOT = Path(__file__).resolve().parents[3]


def prepare(name: str) -> Path:
    host = (ROOT / "build" / name).resolve()
    host.relative_to((ROOT / "build").resolve())
    if host.exists():
        raise ValueError("choose a new host to preserve previous evidence")
    host.mkdir(parents=True)
    (host / "Content").mkdir()
    (host / "Config").mkdir()
    shutil.copy2(ROOT / "tests/compile_check/fixtures/NexusValidation.uproject", host / "NexusValidation.uproject")
    stage_plugins(ROOT / "build/validation", host)
    return host / "NexusValidation.uproject"
