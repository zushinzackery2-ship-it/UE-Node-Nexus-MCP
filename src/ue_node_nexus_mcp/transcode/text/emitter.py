""":class:`Document` -> canonical ``.nexus`` text.

Canonical rules: fixed header order, sections in document order, entries in
document order, ids padded to the section's widest id (capped), numbers
normalized, quoting decided by content only (see ``values.format_value``).
"""

from __future__ import annotations

from .lexer import is_identifier
from .model import Bare, Decl, Document, Link, Prop, Section
from .values import format_value, quote

MAX_ID_PAD = 20


def emit(document: Document) -> str:
    lines: list[str] = []
    header = document.header
    lines.append(f"nexus: {header.nexus}")
    lines.append(f"asset: {header.asset}")
    lines.append(f"class: {header.cls}")
    lines.append(f"schema: {header.schema}")
    for key, value in header.extra.items():
        lines.append(f"{key}: {value}")
    for section in document.sections:
        lines.append("")
        lines.append(section.header())
        lines.extend(emit_section_entries(section))
    return "\n".join(lines) + "\n"


def emit_section_entries(section: Section) -> list[str]:
    decls = [decl for decl in section.decls() if not decl.modifier]
    pad = min(MAX_ID_PAD, max((len(_ident(decl.id)) for decl in decls), default=0))
    lines: list[str] = []
    seen_link = False
    for entry in section.entries:
        if isinstance(entry, Link) and not seen_link and lines:
            lines.append("")
        if isinstance(entry, Link):
            seen_link = True
        lines.append(emit_entry(entry, pad))
    return lines


def emit_entry(entry: Prop | Decl | Link | Bare, pad: int = 0) -> str:
    if isinstance(entry, Prop):
        return f"{_prop_key(entry.key)} = {format_value(entry.value)}"
    if isinstance(entry, Decl):
        return emit_decl(entry, pad)
    if isinstance(entry, Link):
        return f"{_endpoint(entry.src, entry.src_pin)} -> {_endpoint(entry.dst, entry.dst_pin)}"
    return entry.text


def emit_decl(decl: Decl, pad: int = 0) -> str:
    parts: list[str] = []
    if decl.modifier:
        parts.append(decl.modifier)
        pad = 0
    parts.append(f"{_ident(decl.id).ljust(pad)} : {decl.type_name}{_args(decl.args)}")
    if decl.default is not None:
        parts.append(f"= {format_value(decl.default)}")
    if decl.props:
        parts.append("{ " + ", ".join(_prop_item(key, value) for key, value in decl.props) + " }")
    if decl.pos is not None:
        parts.append(f"@ {decl.pos[0]},{decl.pos[1]}")
    for flag in decl.flags:
        parts.append(f"!{flag}")
    for name, value in decl.annotations.items():
        parts.append(f"@{name}({format_value(value)})" if value else f"@{name}")
    return " ".join(parts)


def _args(args: list[tuple[str | None, str]]) -> str:
    if not args:
        return ""
    rendered = [format_value(value) if key is None else f"{_prop_key(key)}={format_value(value)}" for key, value in args]
    return "(" + ", ".join(rendered) + ")"


def _prop_item(key: str, value: str | None) -> str:
    if value is None:
        return _prop_key(key)
    return f"{_prop_key(key)}={format_value(value)}"


def _prop_key(key: str) -> str:
    if not key or key[0] in "#[\"" or "=" in key or key != key.strip() or "," in key:
        return quote(key)
    return key


def _ident(identifier: str) -> str:
    return identifier if is_identifier(identifier) else quote(identifier)


def _endpoint(node_id: str, pin: str | None) -> str:
    text = _ident(node_id)
    if pin is None:
        return text
    return f"{text}.{format_value(pin)}"
