"""Validate complete candidates, including constraints spanning both edits."""

from __future__ import annotations

from ...lint.service import lint_document
from ...scene.model import from_document as scene_model
from ...errors import SyncError
from .decode import physical_ids, to_document


def problem(path: list, code: str, message: str, details: dict | None = None) -> dict:
    result = dict(path=path, conflict_type=code, reason=message)
    if details:
        result["details"] = details
    return result


def validate(snapshot: dict, schema=None) -> list[dict]:
    state = snapshot["semantic"]
    sections = state["sections"]
    all_entities = dict((key, entity) for section in sections.values() for key, entity in section["entities"].items())
    findings = []
    for scope, section in sections.items():
        findings.extend(section_problems(scope, section, all_entities))
    for identifier in cyclic(parent_links(sections)):
        findings.append(problem(entity_path(sections, identifier), "parent-cycle", "parent relationships form a cycle"))
    if findings:
        return findings
    try:
        document = to_document(snapshot)
        if state["kind"] == "scene":
            base = dict(snapshot.get("raw", dict()), aliases=physical_ids(snapshot))
            scene_model(document, base if base.get("map_path") else None)
        else:
            sink = lint_document(document, state["kind"], schema, current_key=snapshot.get("schema_key"))
            findings.extend(dict(problem([], item.code, item.message), file=item.file,
                                 line=item.line, col=item.col) for item in sink.errors())
    except SyncError as exc:
        findings.append(problem([], exc.code, str(exc), exc.details))
    return findings


def section_problems(scope: str, section: dict, all_entities: dict) -> list[dict]:
    """Name collisions and dangling references of one section, in declaration order."""
    findings = []
    aliases = dict()
    for identifier, entity in section["entities"].items():
        path = ["sections", scope, "entities", identifier]
        alias = entity["alias"]
        if alias in aliases:
            findings.append(problem(path, "name-collision", f"name {alias!r} is occupied by {aliases[alias]}"))
        aliases[alias] = identifier
        findings.extend(reference_problems(path, entity, all_entities))
    for slot, link in section["links"].items():
        for role in ("src", "dst"):
            endpoint = link[role]
            if endpoint not in section["entities"] and endpoint not in ("@out", "@entry", "@result"):
                findings.append(problem(["sections", scope, "links", slot], "dependency-conflict", f"connection {role} {endpoint} does not exist in its graph"))
    owner = section.get("owner")
    if owner and owner not in all_entities:
        findings.append(problem(["sections", scope], "dependency-conflict", "scene owner was removed while its children were edited"))
    return findings


def reference_problems(path: list, entity: dict, all_entities: dict) -> list[dict]:
    findings = []
    for namespace in ("args", "props"):
        for key, field in entity[namespace].items():
            reference = field.get("ref")
            if reference and reference not in all_entities:
                findings.append(problem(path + [namespace, key], "dependency-conflict", f"referenced entity {reference} was deleted"))
    for index, field in enumerate(entity["positional"]):
        if field.get("ref") and field["ref"] not in all_entities:
            findings.append(problem(path + ["positional", index], "dependency-conflict", "declaration used by this node was deleted"))
    return findings


def parent_links(sections: dict) -> dict[str, str]:
    """Entity id -> the entity its parent field references; a later parent field wins."""
    return dict((identifier, field["ref"])
                for section in sections.values()
                for identifier, entity in section["entities"].items()
                for namespace in ("args", "props")
                for key, field in entity[namespace].items()
                if field.get("ref") and key.lower() == "parent")


def cyclic(parents: dict[str, str]) -> set[str]:
    complete, result = set(), set()
    for node in parents:
        if node in complete:
            continue
        chain, positions = [], dict()
        current = node
        while current in parents and current not in complete:
            if current in positions:
                result.update(chain[positions[current]:])
                break
            positions[current] = len(chain)
            chain.append(current)
            current = parents[current]
        complete.update(chain)
    return result


def entity_path(sections: dict, identifier: str) -> list:
    for scope, section in sections.items():
        if identifier in section["entities"]:
            return ["sections", scope, "entities", identifier]
    return []
