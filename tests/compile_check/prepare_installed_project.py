"""Stage a C++ host that builds against the engine-installed plugins."""

from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tests.compile_check.prepare_host import sync_directory


def prepare():
    host = ROOT / "build/installed-project"
    fixture = Path(__file__).parent / "fixtures/installed_project"
    host.mkdir(parents=True, exist_ok=True)
    sync_directory(fixture / "Source", host / "Source", host)
    shutil.copy2(fixture / "NexusCppProbe.uproject", host / "NexusCppProbe.uproject")
    assert not (host / "Plugins").exists(), "this probe must use the engine installation"
    return host


if __name__ == "__main__":
    print(prepare())
