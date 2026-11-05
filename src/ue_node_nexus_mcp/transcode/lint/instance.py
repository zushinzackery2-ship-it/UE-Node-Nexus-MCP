"""Validate material parameter value grammar before reaching a native transaction."""

import math
import struct

from ..raw.simple import INSTANCE_SECTIONS
from ..text.lexer import LexError, parse_kv_list
from ..text.values import is_numeric


def finite_float(text):
    if not is_numeric(text):
        return False
    try:
        return math.isfinite(struct.unpack("f", struct.pack("f", float(text)))[0])
    except (ValueError, OverflowError):
        return False


def valid_channels(text, boolean):
    if not text.startswith("(") or not text.endswith(")"):
        return False
    try:
        channels = parse_kv_list(text[1:-1])
    except LexError:
        return False
    if len(channels) != 4 or {str(key).upper() for key, _ in channels} != set("RGBA"):
        return False
    return all(value.strip().lower() in ("true", "false", "1", "0") if boolean else finite_float(value.strip())
               for _, value in channels)


def lint_instance(document, sink):
    for section in document.sections:
        if section.name not in INSTANCE_SECTIONS:
            continue
        for prop in section.props():
            text = prop.value.strip()
            valid = True
            if section.name == "scalar":
                valid = finite_float(text)
            elif section.name == "switch":
                valid = text.lower() in ("true", "false", "1", "0")
            elif section.name in ("vector", "component_mask"):
                valid = valid_channels(text, section.name == "component_mask")
            if not valid:
                sink.error("invalid_parameter_value", f"{section.name} parameter {prop.key!r} has invalid value {text!r}", line=prop.line)
