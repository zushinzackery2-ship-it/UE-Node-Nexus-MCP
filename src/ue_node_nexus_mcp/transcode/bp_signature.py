"""``[function Name(A: float, B: Object(/Script/Engine.Actor) = None) -> (R: bool) {Pure, Category=X}]``."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any

from .bp_types import parse_type_text, type_text
from .lexer import LexError, find_top_level, parse_kv_list, read_group, read_identifier, skip_ws, split_top_level
from .values import format_value, parse_value

KNOWN_FLAGS = ("Pure", "Const", "Public", "Protected", "Private", "CallInEditor", "Static")


@dataclass
class SignatureParam:
    name: str
    type_text: str
    default: str | None = None

    def to_raw(self) -> dict[str, Any]:
        return {"name": self.name, "type": parse_type_text(self.type_text), "default": self.default or ""}


@dataclass
class FunctionSignature:
    name: str
    inputs: list[SignatureParam] = field(default_factory=list)
    outputs: list[SignatureParam] = field(default_factory=list)
    flags: list[str] = field(default_factory=list)
    category: str = ""

    def to_raw(self) -> dict[str, Any]:
        return {
            "name": self.name,
            "inputs": [param.to_raw() for param in self.inputs],
            "outputs": [param.to_raw() for param in self.outputs],
            "flags": list(self.flags),
            "category": self.category,
        }

    def text(self) -> str:
        inputs = ", ".join(_param_text(param) for param in self.inputs)
        outputs = ", ".join(_param_text(param) for param in self.outputs)
        text = f"{self.name}({inputs})"
        if outputs:
            text += f" -> ({outputs})"
        flags = list(self.flags)
        if self.category and self.category != "Default":
            flags.append(f"Category={self.category}")
        if flags:
            text += " {" + ", ".join(flags) + "}"
        return text


def _param_text(param: SignatureParam) -> str:
    text = f"{param.name}: {param.type_text}"
    if param.default not in (None, ""):
        text += f" = {format_value(param.default)}"
    return text


def parse_signature(text: str) -> FunctionSignature:
    text = text.strip()
    name_end = read_identifier(text, 0)
    if name_end == 0:
        raise LexError("function name expected", 1)
    signature = FunctionSignature(name=text[:name_end])
    pos = skip_ws(text, name_end)
    if pos >= len(text) or text[pos] != "(":
        raise LexError("function parameter list '(...)' expected", pos + 1)
    end = read_group(text, pos)
    signature.inputs = _parse_params(text[pos + 1:end - 1])
    pos = skip_ws(text, end)
    if text.startswith("->", pos):
        pos = skip_ws(text, pos + 2)
        if pos >= len(text) or text[pos] != "(":
            raise LexError("return list '(...)' expected after '->'", pos + 1)
        end = read_group(text, pos)
        signature.outputs = _parse_params(text[pos + 1:end - 1])
        pos = skip_ws(text, end)
    if pos < len(text) and text[pos] == "{":
        end = read_group(text, pos)
        for key, value in parse_kv_list(text[pos + 1:end - 1]):
            if key is None:
                signature.flags.append(value)
            elif key == "Category":
                signature.category = parse_value(value)
            else:
                signature.flags.append(f"{key}={parse_value(value)}")
        pos = skip_ws(text, end)
    if pos != len(text):
        raise LexError(f"unexpected text in function signature: {text[pos:]}", pos + 1)
    return signature


def _parse_params(body: str) -> list[SignatureParam]:
    params: list[SignatureParam] = []
    if not body.strip():
        return params
    for part in split_top_level(body, ","):
        part = part.strip()
        if not part:
            continue
        colon = find_top_level(part, ":")
        if colon <= 0:
            raise LexError(f"parameter needs 'name: type': {part}", 1)
        name = part[:colon].strip()
        rest = part[colon + 1:].strip()
        default = None
        eq = find_top_level(rest, "=")
        if eq >= 0:
            default = parse_value(rest[eq + 1:])
            rest = rest[:eq].strip()
        parse_type_text(rest)
        params.append(SignatureParam(name=name, type_text=rest, default=default))
    return params


def signature_from_raw(name: str, raw: dict[str, Any]) -> FunctionSignature:
    signature = FunctionSignature(name=name)
    for param in raw.get("inputs") or []:
        signature.inputs.append(SignatureParam(str(param.get("name", "")), type_text(param.get("type")), str(param.get("default") or "") or None))
    for param in raw.get("outputs") or []:
        signature.outputs.append(SignatureParam(str(param.get("name", "")), type_text(param.get("type")), str(param.get("default") or "") or None))
    signature.flags = [str(flag) for flag in raw.get("flags") or []]
    signature.category = str(raw.get("category", "") or "")
    return signature
