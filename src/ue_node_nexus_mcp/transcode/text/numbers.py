"""Decimal spellings that preserve the value represented by UE numeric types."""

from decimal import Decimal, InvalidOperation
from math import isfinite
from struct import pack, unpack


def decimal_text(text: str) -> str:
    number = Decimal(text)
    return "0" if number == 0 else format(number.normalize(), "f")


def normalize_number(text: str, kind: str) -> str:
    source = text.strip().rstrip("fF")
    try:
        if kind in ("float", "floatproperty"):
            bits = pack("!f", float(source))
            number = unpack("!f", bits)[0]
            if not isfinite(number):
                return text
            # The shortest spelling with the same binary32 bits is both readable
            # and lossless, including subnormals and values exported with %.9g.
            for precision in range(1, 10):
                candidate = format(number, f".{precision}g")
                try:
                    candidate_bits = pack("!f", float(candidate))
                except OverflowError:
                    continue
                if candidate_bits == bits:
                    return decimal_text(candidate)
        if kind in ("double", "doubleproperty", "real"):
            number = float(source)
            return decimal_text(repr(number)) if isfinite(number) else text
        return decimal_text(source)
    except (InvalidOperation, OverflowError, ValueError):
        return text


def struct_member_type(kind: str, name: str) -> str:
    if kind == "transform":
        return "quat" if name.lower() == "rotation" else "vector"
    if kind in ("linearcolor", "vector3f", "vector2f", "vector4f", "quat4f"):
        return "float"
    if kind == "staticcomponentmask":
        return "bool"
    return "byte" if kind == "color" else "double"
