"""Human-readable, stable node ids.

Ids never appear as GUIDs in text. The base raw file keeps ``{guid: id}``; a
re-pull reuses known ids and only mints new ones for new GUIDs, so hand-written
ids survive round trips.
"""

from __future__ import annotations

import re

_NON_IDENT = re.compile(r"[^A-Za-z0-9]+")
_CAMEL_1 = re.compile(r"(.)([A-Z][a-z]+)")
_CAMEL_2 = re.compile(r"([a-z0-9])([A-Z])")
MAX_ID_LENGTH = 32
_STRIP_PREFIXES = ("MaterialExpression", "K2Node_", "Niagara", "EdGraphNode_", "AnimGraphNode_")
_STRIP_SUFFIXES = ("RendererProperties",)


def snake_case(text: str) -> str:
    text = _CAMEL_1.sub(r"\1_\2", text)
    text = _CAMEL_2.sub(r"\1_\2", text)
    text = _NON_IDENT.sub("_", text).strip("_").lower()
    text = re.sub(r"_+", "_", text)
    return text


def short_class_name(class_path: str) -> str:
    name = class_path.rsplit(".", 1)[-1].rsplit("/", 1)[-1]
    for prefix in _STRIP_PREFIXES:
        if name.startswith(prefix) and len(name) > len(prefix):
            name = name[len(prefix):]
            break
    for suffix in _STRIP_SUFFIXES:
        if name.endswith(suffix) and len(name) > len(suffix):
            name = name[: -len(suffix)]
    return name


_UNICODE_WORD = re.compile(r"[^\w]+", re.UNICODE)


def sanitize_id(candidate: str, fallback: str = "node") -> str:
    """ASCII names become snake_case; names with non-ASCII letters keep their
    words so Chinese parameter names become readable quoted ids (``"玻璃缩放"``)."""
    if any(char.isalpha() and not char.isascii() for char in candidate):
        unicode_text = _UNICODE_WORD.sub("_", candidate).strip("_")
        if unicode_text:
            return unicode_text[:MAX_ID_LENGTH]
    text = snake_case(candidate)
    if not text:
        text = snake_case(fallback) or "node"
    if text[0].isdigit():
        text = "n_" + text
    return text[:MAX_ID_LENGTH].rstrip("_") or "node"


class IdAllocator:
    """Allocates unique ids inside one namespace (a graph, a stack, ...)."""

    def __init__(self, reserved: dict[str, str] | None = None) -> None:
        # guid -> id for already known nodes
        self.by_guid: dict[str, str] = dict(reserved or {})
        self.used: set[str] = set(self.by_guid.values())

    def reserve(self, identifier: str) -> None:
        self.used.add(identifier)

    def allocate(self, guid: str | None, candidate: str, fallback: str = "node") -> str:
        if guid and guid in self.by_guid:
            self.used.add(self.by_guid[guid])
            return self.by_guid[guid]
        base = sanitize_id(candidate, fallback)
        identifier = base
        counter = 2
        while identifier in self.used:
            identifier = f"{base}_{counter}"
            counter += 1
        self.used.add(identifier)
        if guid:
            self.by_guid[guid] = identifier
        return identifier


def material_node_candidate(class_short: str, props: dict[str, str]) -> str:
    desc = props.get("Desc", "").strip()
    if class_short == "FunctionInput":
        return "in_" + props.get("InputName", "")
    if class_short == "FunctionOutput":
        output = props.get("OutputName", "")
        return "out" if output in ("", "Result", "None") else "out_" + output
    if class_short == "MaterialFunctionCall":
        return "fn_" + _asset_short_name(props.get("MaterialFunction", "")).removeprefix("MF_")
    if class_short == "Comment":
        return "comment"
    parameter = props.get("ParameterName", "").strip()
    if parameter and parameter != "None":
        return "p_" + parameter if parameter.isascii() else parameter
    if desc:
        return desc
    if class_short in ("TextureSample", "TextureObject", "TextureSampleParameter2D"):
        return "tex_" + _asset_short_name(props.get("Texture", "")).removeprefix("T_")
    if class_short == "NamedRerouteUsage":
        return "use_" + props.get("DeclarationName", "")
    if class_short == "NamedRerouteDeclaration":
        return "decl_" + props.get("Name", "")
    return class_short


def blueprint_node_candidate(class_short: str, config: dict[str, str], title: str) -> str:
    if class_short == "Event":
        return "ev_" + config.get("function_name", title).removeprefix("Receive")
    if class_short == "CustomEvent":
        return "ev_" + config.get("event_name", title)
    if class_short in ("CallFunction", "CallParentFunction", "Message", "PromotableOperator", "CommutativeAssociativeBinaryOperator"):
        return config.get("function_name", title)
    if class_short == "VariableGet":
        return "get_" + config.get("variable_name", title)
    if class_short == "VariableSet":
        return "set_" + config.get("variable_name", title)
    if class_short == "MacroInstance":
        return config.get("macro_name", title)
    if class_short == "DynamicCast":
        return "cast_" + _asset_short_name(config.get("target_type", ""))
    if class_short == "SpawnActorFromClass":
        return "spawn"
    if class_short in ("IfThenElse",):
        return "branch"
    if class_short == "ExecutionSequence":
        return "seq"
    if class_short == "Knot":
        return "knot"
    if class_short == "Comment":
        return "comment"
    return class_short


def niagara_module_candidate(script_path: str) -> str:
    return _asset_short_name(script_path)


def _asset_short_name(path: str) -> str:
    if not path:
        return ""
    text = path.rsplit("/", 1)[-1]
    if "." in text:
        text = text.rsplit(".", 1)[-1]
    return text.strip("'")
