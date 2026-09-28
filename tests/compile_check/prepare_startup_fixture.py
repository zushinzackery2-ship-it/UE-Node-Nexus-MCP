"""Stage the independent startup test plugin beside the validated product plugins."""

from pathlib import Path
import json
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tests.compile_check.prepare_host import sync_directory


def prepare() -> None:
    host = ROOT / "build/validation"
    source = ROOT / "tests/live/startup_fixture"
    target = host / "Plugins/UeNexusStartupFixture"
    sync_directory(source, target, host)
    print(f"Staged isolated startup fixture: {target}")


def finalize() -> None:
    host = ROOT / "build/validation/Plugins"
    directory = host / "UeNexusStartupFixture/Binaries/Win64"
    binary = directory / "UnrealEditor-UeNexusStartupFixture.dll"
    assert binary.is_file(), binary
    reference = json.loads((host / "UeNodeNexusBridge/Binaries/Win64/UnrealEditor.modules").read_text(encoding="utf-8"))
    manifest = dict(BuildId=reference["BuildId"], Modules=dict(UeNexusStartupFixture=binary.name))
    (directory / "UnrealEditor.modules").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    finalize() if "--finalize" in sys.argv else prepare()
