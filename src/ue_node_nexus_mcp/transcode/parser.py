"""``.nexus`` text -> :class:`Document`.

Grammar (one fact per line, ``#`` comments):

    key: value                      header, before the first section
    [name args...]                  section header
    Key = Value                     property line
    [local ]id : Type[(args)] [= default] [{k=v,...}] [@ x,y] [!flag] [@annot(arg)]
    src[.pin] -> dst[.pin]          link line
    anything else                   bare line (interfaces, dispatcher signatures)
"""

from __future__ import annotations

import re

from .errors import DiagnosticSink
from .lexer import (
    LexError,
    find_top_level,
    is_identifier,
    parse_kv_list,
    read_group,
    read_identifier,
    read_quoted,
    read_type_name,
    read_value_token,
    skip_ws,
)
from .model import Bare, Decl, Document, Header, Link, Prop, Section
from .values import parse_value, unquote

_MODIFIERS = ("local",)
_HEADER_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*:\s*(.*)$")
_POSITION_RE = re.compile(r"^([-+]?\d+)\s*,\s*([-+]?\d+)$")


def strip_comment(line: str) -> str:
    index = 0
    while index < len(line):
        char = line[index]
        if char == '"':
            try:
                index = read_quoted(line, index)
            except LexError:
                return line
            continue
        if char == "#":
            return line[:index]
        index += 1
    return line


def parse(text: str, file: str | None = None) -> tuple[Document, DiagnosticSink]:
    sink = DiagnosticSink(file=file)
    document = Document()
    current: Section | None = None
    for line_no, raw_line in enumerate(text.splitlines(), start=1):
        line = strip_comment(raw_line).strip()
        if not line:
            continue
        if line.startswith("["):
            current = _parse_section_header(line, line_no, sink)
            if current is not None:
                document.sections.append(current)
            continue
        if current is None:
            _parse_header_line(document.header, line, line_no, sink)
            continue
        try:
            entry = _parse_entry(line, line_no)
        except LexError as exc:
            sink.error("syntax", str(exc), line=line_no, col=exc.col)
            continue
        if entry is not None:
            current.entries.append(entry)
    return document, sink


def _parse_section_header(line: str, line_no: int, sink: DiagnosticSink) -> Section | None:
    if not line.endswith("]"):
        sink.error("syntax", "section header must end with ']'", line=line_no)
        return None
    inner = line[1:-1].strip()
    if not inner:
        sink.error("syntax", "empty section header", line=line_no)
        return None
    parts = inner.split(None, 1)
    name = parts[0]
    args = parts[1].strip() if len(parts) > 1 else ""
    if not is_identifier(name):
        sink.error("syntax", f"invalid section name: {name}", line=line_no)
        return None
    return Section(name=name, args=args, line=line_no)


def _parse_header_line(header: Header, line: str, line_no: int, sink: DiagnosticSink) -> None:
    match = _HEADER_RE.match(line)
    if match is None:
        sink.error("syntax", "expected 'key: value' header line before the first section", line=line_no)
        return
    key, value = match.group(1), match.group(2).strip()
    if key == "nexus":
        if not value.isdigit():
            sink.error("syntax", "nexus header must be an integer", line=line_no)
            return
        header.nexus = int(value)
    elif key == "asset":
        header.asset = value
    elif key == "class":
        header.cls = value
    elif key == "schema":
        header.schema = value
    else:
        header.extra[key] = value


def _parse_entry(line: str, line_no: int) -> Prop | Decl | Link | Bare | None:
    if find_top_level(line, "->") >= 0:
        return _parse_link(line, line_no)
    decl = _try_parse_decl(line, line_no)
    if decl is not None:
        return decl
    eq = find_top_level(line, "=")
    if eq > 0:
        key = line[:eq].strip()
        if key.startswith('"'):
            key = unquote(key)
        return Prop(key=key, value=parse_value(line[eq + 1:]), line=line_no)
    return Bare(text=line, line=line_no)


def _try_parse_decl(line: str, line_no: int) -> Decl | None:
    pos = 0
    modifier: str | None = None
    for candidate in _MODIFIERS:
        if line.startswith(candidate + " "):
            modifier = candidate
            pos = skip_ws(line, len(candidate))
            break
    id_end = read_identifier(line, pos)
    if id_end == pos:
        return None
    colon = skip_ws(line, id_end)
    if colon >= len(line) or line[colon] != ":":
        return None
    decl_id = unquote(line[pos:id_end]) if line[pos] == '"' else line[pos:id_end]
    pos = skip_ws(line, colon + 1)
    type_end = read_type_name(line, pos)
    if type_end == pos:
        raise LexError("expected a type or class name after ':'", pos + 1)
    decl = Decl(id=decl_id, type_name=line[pos:type_end], modifier=modifier, line=line_no)
    pos = skip_ws(line, type_end)
    if pos < len(line) and line[pos] == "(":
        end = read_group(line, pos)
        decl.args = [(key, parse_value(value)) for key, value in parse_kv_list(line[pos + 1:end - 1])]
        pos = skip_ws(line, end)
    _parse_decl_tail(line, pos, decl)
    return decl


def _parse_decl_tail(line: str, pos: int, decl: Decl) -> None:
    while pos < len(line):
        char = line[pos]
        if char == "=":
            pos = skip_ws(line, pos + 1)
            end = read_value_token(line, pos)
            if end == pos:
                raise LexError("expected a value after '='", pos + 1)
            decl.default = parse_value(line[pos:end])
            pos = skip_ws(line, end)
        elif char == "{":
            end = read_group(line, pos)
            decl.props = [
                (key, parse_value(value)) if key is not None else (parse_value(value), None)
                for key, value in parse_kv_list(line[pos + 1:end - 1])
            ]
            pos = skip_ws(line, end)
        elif char == "@":
            pos = _parse_at(line, pos, decl)
        elif char == "!":
            end = read_identifier(line, pos + 1)
            if end == pos + 1:
                raise LexError("expected a flag name after '!'", pos + 2)
            decl.flags.append(line[pos + 1:end])
            pos = skip_ws(line, end)
        else:
            raise LexError(f"unexpected text: {line[pos:]}", pos + 1)


def _parse_at(line: str, pos: int, decl: Decl) -> int:
    after = skip_ws(line, pos + 1)
    if after < len(line) and (line[after].isdigit() or line[after] in "-+"):
        end = after
        while end < len(line) and line[end] not in " \t":
            end += 1
        match = _POSITION_RE.match(line[after:end])
        if match is None:
            raise LexError("position must be '@ x,y'", after + 1)
        decl.pos = (int(match.group(1)), int(match.group(2)))
        return skip_ws(line, end)
    name_end = read_identifier(line, after)
    if name_end == after:
        raise LexError("expected position or annotation after '@'", pos + 1)
    name = line[after:name_end]
    pos = skip_ws(line, name_end)
    if pos < len(line) and line[pos] == "(":
        end = read_group(line, pos)
        decl.annotations[name] = parse_value(line[pos + 1:end - 1])
        return skip_ws(line, end)
    decl.annotations[name] = ""
    return pos


def _parse_endpoint(text: str) -> tuple[str, str | None]:
    text = text.strip()
    if not text:
        raise LexError("empty link endpoint", 1)
    id_end = read_identifier(text, 0)
    if id_end == 0:
        raise LexError(f"invalid link endpoint: {text}", 1)
    node_id = unquote(text[:id_end]) if text[0] == '"' else text[:id_end]
    if id_end == len(text):
        return node_id, None
    if text[id_end] != ".":
        raise LexError(f"invalid link endpoint: {text}", id_end + 1)
    pin_text = text[id_end + 1:]
    if not pin_text:
        raise LexError("missing pin name after '.'", id_end + 2)
    return node_id, parse_value(pin_text)


def _parse_link(line: str, line_no: int) -> Link:
    arrow = find_top_level(line, "->")
    src, src_pin = _parse_endpoint(line[:arrow])
    dst, dst_pin = _parse_endpoint(line[arrow + 2:])
    return Link(src=src, src_pin=src_pin, dst=dst, dst_pin=dst_pin, line=line_no)
