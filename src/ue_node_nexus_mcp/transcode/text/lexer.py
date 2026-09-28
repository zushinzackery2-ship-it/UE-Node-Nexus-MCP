"""Line-level scanning helpers that respect quotes and bracket nesting."""

from __future__ import annotations

_OPEN = {"(": ")", "{": "}", "[": "]"}
_CLOSE = {")", "}", "]"}
_IDENT_START = set("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_")
_IDENT_BODY = _IDENT_START | set("0123456789")
_TYPE_BODY = _IDENT_BODY | set("@./:-")


class LexError(ValueError):
    def __init__(self, message: str, col: int) -> None:
        super().__init__(message)
        self.col = col


def skip_ws(text: str, pos: int) -> int:
    while pos < len(text) and text[pos] in " \t":
        pos += 1
    return pos


def read_quoted(text: str, pos: int) -> int:
    """Return the index just past the quoted string starting at ``pos``."""
    index = pos + 1
    while index < len(text):
        if text[index] == "\\":
            index += 2
            continue
        if text[index] == '"':
            return index + 1
        index += 1
    raise LexError("unterminated string", pos + 1)


def read_group(text: str, pos: int) -> int:
    """Return the index just past the bracket group opening at ``pos``."""
    stack = [_OPEN[text[pos]]]
    index = pos + 1
    while index < len(text):
        char = text[index]
        if char == '"':
            index = read_quoted(text, index)
            continue
        if char in _OPEN:
            stack.append(_OPEN[char])
        elif char in _CLOSE:
            if char != stack[-1]:
                raise LexError(f"mismatched '{char}'", index + 1)
            stack.pop()
            if not stack:
                return index + 1
        index += 1
    raise LexError(f"unterminated '{text[pos]}'", pos + 1)


def find_top_level(text: str, needle: str, start: int = 0) -> int:
    """Index of ``needle`` at nesting depth zero and outside quotes, or -1."""
    index = start
    while index < len(text):
        char = text[index]
        if char == '"':
            index = read_quoted(text, index)
            continue
        if char in "({[":
            index = read_group(text, index)
            continue
        if text.startswith(needle, index):
            return index
        index += 1
    return -1


def split_top_level(text: str, sep: str) -> list[str]:
    parts: list[str] = []
    start = 0
    while True:
        index = find_top_level(text, sep, start)
        if index < 0:
            parts.append(text[start:])
            return parts
        parts.append(text[start:index])
        start = index + len(sep)


def read_value_token(text: str, pos: int) -> int:
    """Return the end of one value token: quoted, bracket group or bare run."""
    if pos >= len(text):
        return pos
    char = text[pos]
    if char == '"':
        return read_quoted(text, pos)
    if char in "({[":
        return read_group(text, pos)
    index = pos
    while index < len(text) and text[index] not in " \t{}":
        if text[index] in "\"([":
            index = read_value_token(text, index)
            continue
        index += 1
    return index


def read_identifier(text: str, pos: int) -> int:
    if pos >= len(text):
        return pos
    if text[pos] == '"':
        return read_quoted(text, pos)
    if text[pos] not in _IDENT_START:
        return pos
    index = pos + 1
    while index < len(text) and text[index] in _IDENT_BODY:
        index += 1
    return index


def read_type_name(text: str, pos: int) -> int:
    """Type expression: identifier/path characters plus balanced ``<...>``.

    Inside angle brackets any characters (including ``(...)`` payloads such as
    ``Array<Object(/Script/Engine.Actor)>``) belong to the type.
    """
    index = pos
    depth = 0
    while index < len(text):
        char = text[index]
        if char == "<":
            depth += 1
        elif char == ">":
            if depth == 0:
                break
            depth -= 1
        elif depth == 0 and char not in _TYPE_BODY:
            break
        elif depth > 0 and char == '"':
            index = read_quoted(text, index)
            continue
        index += 1
    if depth != 0:
        raise LexError("unterminated '<'", pos + 1)
    return index


def parse_kv_list(body: str) -> list[tuple[str | None, str]]:
    """Split ``k=v, k2=v2, positional`` into (key|None, value) pairs."""
    items: list[tuple[str | None, str]] = []
    if not body.strip():
        return items
    for part in split_top_level(body, ","):
        part = part.strip()
        if not part:
            continue
        eq = find_top_level(part, "=")
        if eq < 0:
            items.append((None, part))
            continue
        key = part[:eq].strip()
        value = part[eq + 1:].strip()
        if key.startswith('"'):
            from .values import unquote

            key = unquote(key)
        items.append((key, value))
    return items


def is_identifier(text: str) -> bool:
    return bool(text) and text[0] in _IDENT_START and all(char in _IDENT_BODY for char in text)
