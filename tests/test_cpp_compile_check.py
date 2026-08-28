"""Host-compiler syntax/type check for plugin TUs written without a UE build.

These translation units have never been through a real Unreal build in this
environment. Compiling them with clang against the minimal UE API stubs in
``tests/compile_check/ue_stubs`` catches C++ errors in our own code (types,
overload choices, const-ness, template usage). It does NOT prove the Epic API
signatures themselves; the stubs mirror the documented UE 5.5 surface and a
real UE 5.5 editor build is still required before deployment.
"""

from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
STUBS_DIR = ROOT / "tests" / "compile_check" / "ue_stubs"
MODULE_DIR = ROOT / "Plugins" / "UeNodeNexusBridge" / "Source" / "UeNodeNexusBridge"

# Only TUs whose full include closure is covered by the stubs are listed here;
# the rest of the plugin needs a real UE build to compile.
CHECKED_SOURCES = (
    "Private/Assets/UeNodeNexusBridgeAssetDependencyOps.cpp",
    "Private/Core/UeNodeNexusBridgeCoreOperationsRegistry.cpp",
    "Private/Core/UeNodeNexusBridgeOperationNames.cpp",
    "Private/Core/UeNodeNexusBridgeViewportCaptureOps.cpp",
    "Private/Level/UeNodeNexusBridgeLevelActorWriteOps.cpp",
    "Private/Level/UeNodeNexusBridgeLevelOpenOps.cpp",
)

def _compiler_candidates() -> list[list[str]]:
    candidates: list[list[str]] = []
    clang = shutil.which("clang++")
    if clang:
        candidates.append([clang])
        # Ubuntu clang sometimes picks a GCC toolchain dir that has no matching
        # libstdc++ headers; retry pinned to each toolchain that does have them.
        gcc_root = Path("/usr/lib/gcc/x86_64-linux-gnu")
        for header_dir in sorted(Path("/usr/include/c++").glob("*"), reverse=True):
            gcc_dir = gcc_root / header_dir.name
            if gcc_dir.is_dir():
                candidates.append([clang, f"--gcc-install-dir={gcc_dir}"])
    gxx = shutil.which("g++")
    if gxx:
        candidates.append([gxx])
    return candidates


def _working_compiler() -> list[str] | None:
    probe = "#include <cstdint>\nint main() { return 0; }\n"
    for candidate in _compiler_candidates():
        result = subprocess.run(
            [*candidate, "-std=c++20", "-fsyntax-only", "-x", "c++", "-"],
            input=probe,
            capture_output=True,
            text=True,
            timeout=60,
        )
        if result.returncode == 0:
            return candidate
    return None


COMPILER = _working_compiler()


@pytest.mark.skipif(COMPILER is None, reason="no working C++20 compiler with libstdc++ headers")
@pytest.mark.parametrize("source", CHECKED_SOURCES)
def test_plugin_source_compiles_against_ue_stubs(source: str) -> None:
    source_path = MODULE_DIR / source
    assert source_path.is_file(), f"missing source file: {source}"

    command = [
        *COMPILER,
        "-std=c++20",
        "-fsyntax-only",
        "-x",
        "c++",
        "-Wall",
        "-Wextra",
        "-Wno-unused-parameter",
        "-DUENODENEXUSBRIDGE_API=",
        f"-I{STUBS_DIR}",
        f"-I{MODULE_DIR / 'Public'}",
        f"-I{MODULE_DIR / 'Private' / 'Core'}",
        str(source_path),
    ]
    completed = subprocess.run(command, capture_output=True, text=True, timeout=120)
    assert completed.returncode == 0, f"{source} failed the stub compile check:\n{completed.stderr}"
