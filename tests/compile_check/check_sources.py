"""Static source checks only: syntax, imports, budgets and operation contracts."""

from __future__ import annotations

import ast
import json
from pathlib import Path
import re
import tomllib

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "src/ue_node_nexus_mcp"
CORE = ROOT / "Plugins/UeNodeNexusBridge/Source/UeNodeNexusBridge"
VFX = ROOT / "Plugins/UeNodeNexusVfxBridge/Source/UeNodeNexusVfxBridge"
EXCLUDED = set(("__pycache__", "Intermediate", "Binaries"))


def sources() -> list[Path]:
    result = []
    for name in ("src", "Plugins", "tests", "release"):
        result.extend(path for path in (ROOT / name).rglob("*") if path.is_file()
                      and path.suffix in (".py", ".cpp", ".h", ".cs", ".json", ".uplugin")
                      and not set(path.parts) & EXCLUDED)
    return sorted(result)


def check_imports(file: Path, tree: ast.AST, errors: list[str]) -> None:
    if not file.is_relative_to(PACKAGE):
        return
    for node in ast.walk(tree):
        if not isinstance(node, ast.ImportFrom) or not node.module:
            continue
        if node.level:
            base = file.parent
            for _ in range(node.level - 1):
                base = base.parent
            target = base.joinpath(*node.module.split("."))
        elif node.module.startswith("ue_node_nexus_mcp."):
            target = ROOT.joinpath("src", *node.module.split("."))
        else:
            continue
        if not target.with_suffix(".py").is_file() and not (target / "__init__.py").is_file():
            errors.append(f"unresolved local module: {file.relative_to(ROOT)}:{node.lineno}: {node.module}")


def operation_records() -> list[dict]:
    manifest = json.loads((PACKAGE / "operations.json").read_text(encoding="utf-8"))
    assert manifest["version"] == 2
    result = []
    for relative in manifest["files"]:
        file = PACKAGE / relative
        assert file.resolve().is_relative_to((PACKAGE / "operations").resolve())
        result.extend(json.loads(file.read_text(encoding="utf-8"))["operations"])
    return result


def check_operations(records: list[dict], errors: list[str]) -> None:
    names = [row["name"] for row in records]
    if len(names) != len(set(names)):
        errors.append("duplicate operation name")
    for row in records:
        if row["kind"] not in ("read", "write") or row["risk"] not in ("low", "medium", "high"):
            errors.append(f"invalid operation metadata: {row['name']}")
    pattern = r'TEXT\("([a-z][a-z0-9_]*)"\)'
    core_names = set(re.findall(pattern, (CORE / "Private/Core/UeNodeNexusBridgeOperationNames.cpp").read_text(encoding="utf-8")))
    handlers = set(re.findall(r'RegisterCoreOp\(TEXT\("([a-z][a-z0-9_]*)"\)',
                             (CORE / "Private/Core/UeNodeNexusBridgeCoreOperationsRegistry.cpp").read_text(encoding="utf-8")))
    auto_index = set(name for name in re.findall(pattern, (CORE / "Private/Core/UeNodeNexusBridgeAutoIndexDispatch.cpp").read_text(encoding="utf-8"))
                     if name.startswith("auto_index_"))
    vfx = set(re.findall(r'\{\s*TEXT\("([a-z][a-z0-9_]*)"\)', (VFX / "Private/Module/UeNodeNexusVfxBridgeModule.cpp").read_text(encoding="utf-8")))
    expected = set(row["name"] for row in records if not row.get("local"))
    if core_names != handlers:
        errors.append("core handlers differ from declared names: " + str(sorted(core_names ^ handlers)))
    if expected != core_names | auto_index | vfx:
        errors.append("Python/C++ operation contract differs: " + str(sorted(expected ^ (core_names | auto_index | vfx))))


def main() -> None:
    files = sources()
    errors = []
    facade_names = []
    python_count = 0
    for file in files:
        source = file.read_text(encoding="utf-8-sig")
        if len(source.splitlines()) > 300:
            errors.append(f"source budget: {file.relative_to(ROOT)}: {len(source.splitlines())} lines")
        if file.suffix == ".py":
            try:
                tree = ast.parse(source, filename=str(file))
            except SyntaxError as exc:
                errors.append(str(exc))
                continue
            python_count += 1
            check_imports(file, tree, errors)
            for node in ast.walk(tree):
                if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
                    if any(isinstance(item, ast.Call) and isinstance(item.func, ast.Name) and item.func.id == "thin_tool" for item in node.decorator_list):
                        facade_names.append(node.name)
        elif file.suffix in (".json", ".uplugin"):
            json.loads(source)
    expected_facades = set(("ue_context_get", "ue_capability_get", "ue_execute", "ue_read", "ue_diff_get", "ue_plan_validate", "ue_sync"))
    if set(facade_names) != expected_facades or len(facade_names) != 7:
        errors.append(f"public facade contract differs: {facade_names}")
    records = operation_records()
    check_operations(records, errors)
    version = tomllib.loads((ROOT / "pyproject.toml").read_text(encoding="utf-8"))["project"]["version"]
    for name in ("UeNodeNexusBridge", "UeNodeNexusVfxBridge"):
        descriptor = json.loads((ROOT / "Plugins" / name / f"{name}.uplugin").read_text(encoding="utf-8"))
        if descriptor["VersionName"] != version:
            errors.append(f"version mismatch: {name}")
    report = dict(static_only=True, runtime_tests_executed=False, files=len(files), python_files=python_count,
                  operations=len(records), hidden_operations=sum(bool(row.get("hidden")) for row in records),
                  facades=sorted(facade_names), version=version, errors=errors)
    output = ROOT / "build/validation/Logs/SourceCheck.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    if errors:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
