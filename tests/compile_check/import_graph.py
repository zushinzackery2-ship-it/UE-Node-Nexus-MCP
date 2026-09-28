"""Resolve package imports and report cycles without importing runtime services."""

from __future__ import annotations

import ast
from graphlib import CycleError, TopologicalSorter
from pathlib import Path


def module_name(root: Path, file: Path) -> str:
    parts = list(file.relative_to(root).with_suffix("").parts)
    if parts[-1] == "__init__":
        parts.pop()
    return ".".join(parts)


def dependencies(root: Path, *, deferred: bool = False) -> dict[str, set[str]]:
    files = dict((module_name(root, file), file) for file in root.rglob("*.py"))
    result = dict((name, set()) for name in files)
    for name, file in files.items():
        tree = ast.parse(file.read_text(encoding="utf-8-sig"))
        parent = name.split(".") if file.name == "__init__.py" else name.split(".")[:-1]
        nodes = ast.walk(tree) if deferred else tree.body
        for node in nodes:
            targets = import_targets(node, parent)
            for target in targets:
                if target in files and target != name:
                    result[name].add(target)
    return result


def import_targets(node: ast.AST, parent: list[str]) -> list[str]:
    if isinstance(node, ast.Import):
        return [item.name for item in node.names]
    if not isinstance(node, ast.ImportFrom):
        return []
    prefix = parent[:len(parent) - node.level + 1] if node.level else []
    target = ".".join(prefix + (node.module.split(".") if node.module else []))
    children = [target + "." + item.name for item in node.names]
    return [target, *children] if node.module else children


def cycles(graph: dict[str, set[str]]) -> list[list[str]]:
    """Report a representative cycle per removed edge until the graph is acyclic."""
    remaining = dict((name, set(deps)) for name, deps in graph.items())
    result = []
    while True:
        try:
            tuple(TopologicalSorter(remaining).static_order())
            return result
        except CycleError as error:
            cycle = error.args[1]
            result.append(cycle)
            remaining[cycle[1]].discard(cycle[0])
