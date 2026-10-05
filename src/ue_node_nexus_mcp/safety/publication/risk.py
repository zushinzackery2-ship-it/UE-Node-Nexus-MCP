"""Explain risk from the actual delta rather than generic operation names."""

from __future__ import annotations


MATERIAL_TOKENS = ("Custom", "VertexInterpolator", "WorldPositionOffset", "PixelDepthOffset",
                   "ShadingModel", "Tessellation", "VirtualTexture", "Nanite")


def node_classes(*documents) -> dict:
    classes = dict()
    for document in documents:
        if document is None:
            continue
        for section in document.sections:
            if section.name == "graph" or section.name.startswith("graph "):
                for declaration in section.decls():
                    classes.setdefault(declaration.id, []).append(declaration.type_name)
    return classes


def rendering_change(operation: dict, classes: dict) -> bool:
    if operation.get("op") == "set_node_position":
        return False
    values = list(operation.values())
    for field in ("id", "from", "to"):
        values.extend(classes.get(operation.get(field), []))
    return any(any(token.casefold() in str(value).casefold() for token in MATERIAL_TOKENS) for value in values)


def reasons(item: dict) -> list[str]:
    if item.get("empty"):
        return []
    kind = item["kind"]
    payload = item.get("payload", dict())
    plan = payload.get("plan", [])
    if kind == "niagara_system" and not payload.get("delete_asset"):
        return ["niagara_store_and_compiled_graph"]
    if kind not in ("material", "material_function") or payload.get("delete_asset"):
        return []
    result = []
    if item.get("interface_changed"):
        result.append("material_function_interface")
    if any(rendering_change(operation, item.get("node_classes", dict())) for operation in plan):
        result.append("material_rendering_contract")
    return result
