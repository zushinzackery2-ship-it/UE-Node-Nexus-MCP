"""Value text helpers.

Values are stored exactly as UE ``ImportText`` (PPF_None) expects them; the
text format only decides whether to show them quoted. We never invent a value
grammar of our own: structs stay ``(R=1,G=0,B=0,A=1)``, objects stay paths,
enums stay short names. Numbers are normalized so ``1.000000`` and ``1``
compare equal.
"""

from __future__ import annotations

import re

_NUMBER_RE = re.compile(r"(?<![\w.])([-+]?\d+)\.(\d+)(?![\w.])")
_SIMPLE_TOKEN_RE = re.compile(r"^[A-Za-z0-9_/][A-Za-z0-9_:./\-+]*$")
_NUMERIC_RE = re.compile(r"^[-+]?(\d+(\.\d*)?|\.\d+)([eE][-+]?\d+)?$")
_ESCAPES = {"\\": "\\\\", '"': '\\"', "\n": "\\n", "\r": "\\r", "\t": "\\t"}
_UNESCAPES = {"\\": "\\", '"': '"', "n": "\n", "r": "\r", "t": "\t"}

STRING_TYPES = {"FString", "FName", "FText"}


def _strip_number(match: re.Match[str]) -> str:
    integer, fraction = match.group(1), match.group(2).rstrip("0")
    return integer if not fraction else f"{integer}.{fraction}"


def normalize_value(text: str) -> str:
    """Comparison form: numbers normalized, booleans lower-cased, trimmed."""
    result = normalize_display(text).strip()
    lowered = result.lower()
    if lowered in ("true", "false"):
        return lowered
    return result


def _find_quote_end(text: str, start: int) -> int:
    index = start + 1
    while index < len(text):
        if text[index] == "\\":
            index += 2
            continue
        if text[index] == '"':
            return index + 1
        index += 1
    return len(text)


def values_equal(left: str | None, right: str | None) -> bool:
    if left is None or right is None:
        return left is right
    return normalize_value(left) == normalize_value(right)


def quote(raw: str) -> str:
    """Escape a value for one text line.

    Control characters are escaped rather than written verbatim: the reader splits
    text into lines with ``str.splitlines`` and trims values, so a raw ``\\x0b`` (or a
    binary stub tag payload) would silently move to the next line or lose bytes.
    """
    out: list[str] = ['"']
    for char in raw:
        mapped = _ESCAPES.get(char)
        if mapped is not None:
            out.append(mapped)
        elif ord(char) < 0x20 or ord(char) == 0x7F:
            out.append(f"\\x{ord(char):02x}")
        else:
            out.append(char)
    out.append('"')
    return "".join(out)


def _unescape(char: str, body: str, index: int) -> tuple[str, int]:
    """Value and next index for the escape sequence starting at ``index``."""
    if char == "x" and index + 2 < len(body):
        digits = body[index + 1:index + 3]
        try:
            return chr(int(digits, 16)), index + 3
        except ValueError:
            return char, index + 1
    return _UNESCAPES.get(char, char), index + 1


def unquote(token: str) -> str:
    if len(token) < 2 or token[0] != '"' or token[-1] != '"':
        return token
    body = token[1:-1]
    out: list[str] = []
    index = 0
    while index < len(body):
        char = body[index]
        if char == "\\" and index + 1 < len(body):
            text, index = _unescape(body[index + 1], body, index + 1)
            out.append(text)
            continue
        out.append(char)
        index += 1
    return "".join(out)


def is_balanced_group(raw: str) -> bool:
    if len(raw) < 2 or raw[0] != "(" or raw[-1] != ")":
        return False
    depth = 0
    index = 0
    while index < len(raw):
        char = raw[index]
        if char == '"':
            index = _find_quote_end(raw, index)
            continue
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0 and index != len(raw) - 1:
                return False
            if depth < 0:
                return False
        index += 1
    return depth == 0


_MARKER_RE = re.compile(r"^@[A-Za-z_]+(\(.*\))?$")


def is_marker(raw: str) -> bool:
    """``@link(User.X)`` / ``@dynamic`` style values stay verbatim."""
    match = _MARKER_RE.match(raw)
    if match is None:
        return False
    group = match.group(1)
    return group is None or is_balanced_group(group)


def needs_quotes(raw: str) -> bool:
    if raw == "" or raw != raw.strip():
        return True
    if is_marker(raw) or is_numeric(raw):
        return False
    if raw[0] in "@!#[{\"'":
        return True
    if is_balanced_group(raw):
        return _has_unterminated_quote(raw)
    return not _SIMPLE_TOKEN_RE.match(raw)


def _has_unterminated_quote(raw: str) -> bool:
    index = 0
    while index < len(raw):
        if raw[index] == '"':
            end = _find_quote_end(raw, index)
            if end > len(raw) or raw[end - 1] != '"' or end == index + 1:
                return True
            index = end
            continue
        index += 1
    return False


def format_value(raw: str) -> str:
    """Render a raw import-text value for the text format.

    Quoting is decided purely by content so that ``emit(parse(text))`` is
    stable without type information: simple tokens, paths, enum names and
    balanced ``(...)`` structs stay verbatim, everything else is quoted.
    Numeric tokens are normalized (``1.000000`` -> ``1``).
    """
    if needs_quotes(raw):
        return quote(raw)
    return normalize_display(raw)


def normalize_display(raw: str) -> str:
    """Number-only normalization used for display; booleans are left alone."""
    out: list[str] = []
    index = 0
    while index < len(raw):
        if raw[index] == '"':
            end = _find_quote_end(raw, index)
            out.append(raw[index:end])
            index = end
            continue
        start = index
        while index < len(raw) and raw[index] != '"':
            index += 1
        out.append(_NUMBER_RE.sub(_strip_number, raw[start:index]))
    return "".join(out)


def parse_value(token: str) -> str:
    """Inverse of :func:`format_value`: quoted text is unescaped, else verbatim."""
    token = token.strip()
    if token.startswith('"'):
        return unquote(token)
    return token


def is_numeric(raw: str) -> bool:
    return bool(_NUMERIC_RE.match(raw.strip()))


def is_boolean(raw: str) -> bool:
    return raw.strip().lower() in ("true", "false")
