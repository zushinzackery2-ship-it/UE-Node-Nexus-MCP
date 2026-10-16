"""Check numeric links using native K2 real-precision compatibility."""

from ..blueprint.pins import canonical_pin, class_pin_aliases, node_pins
from ..blueprint.signature import parse_signature
from ..text.lexer import LexError

NUMERIC = frozenset(("byte", "int", "int64", "real", "float", "double"))
INTERFACE = "interface"
OBJECT_LIKE = frozenset(("object", "class", "softobject", "softclass"))


def lint_links(section, document, schema, sink):
    declarations = section.decl_map()
    pins = dict((decl.id, node_pins(decl, document, section, schema))
                for decl in section.decls() if not decl.opaque and decl.modifier != "local")
    aliases = dict((decl.id, class_pin_aliases(schema, decl.type_name))
                   for decl in section.decls() if not decl.opaque and decl.modifier != "local")
    implicit = implicit_pins(section)
    for link in section.links():
        source = find_pin(pins.get(link.src, implicit.get(link.src, [])), link.src_pin, "out", aliases.get(link.src))
        target = find_pin(pins.get(link.dst, implicit.get(link.dst, [])), link.dst_pin, "in", aliases.get(link.dst))
        if not source or not target:
            continue
        if container_layout_mismatch(source.get("type", dict()), target.get("type", dict())):
            sink.error("array_layout_mismatch",
                       f"{link.src}.{source['name']} -> {link.dst}.{target['name']}: interface and object array elements "
                       "have different runtime layouts; use matching element types",
                       line=link.line)
            continue
        if link.src not in declarations or link.dst not in declarations:
            continue
        left, right = source.get("type", dict()), target.get("type", dict())
        if left.get("category") not in NUMERIC or right.get("category") not in NUMERIC:
            continue
        # K2 treats both real precisions as compatible and emits bytecode conversions.
        # EdGraphSchema_K2.cpp::ArePinTypesCompatible must remain the authority here.
        if real_precision(left) and real_precision(right):
            continue
        if (left.get("category"), left.get("subcategory", "")) != (right.get("category"), right.get("subcategory", "")):
            sink.error("explicit_conversion_required",
                       f"{link.src}.{source['name']} -> {link.dst}.{target['name']} needs an explicit numeric conversion node",
                       line=link.line)


def implicit_pins(section):
    """Entry and result pins a function signature declares; links to them bypass declarations."""
    if section.name != "function":
        return dict()
    try:
        signature = parse_signature(section.args)
    except LexError:
        return dict()
    entry = [dict(name="then", dir="out", type=dict(category="exec"))]
    result = [dict(name="execute", dir="in", type=dict(category="exec"))]
    entry.extend(dict(param.to_raw(), dir="out") for param in signature.inputs)
    result.extend(dict(param.to_raw(), dir="in") for param in signature.outputs)
    return dict(entry=entry, result=result)


def container_layout_mismatch(left, right):
    """Object arrays and interface arrays have different element layouts.

    K2's connection path can still accept such a link when a wildcard pin was
    already bound to an interface; executing the graph then reads the interface
    pointer from the wrong element offset, which crashes the editor. Native
    apply enforces the same rule with the final resolved pin types.
    """
    container = left.get("container", "none") or "none"
    if container == "none" or container != (right.get("container", "none") or "none"):
        return False
    categories = (left.get("category"), right.get("category"))
    if INTERFACE not in categories:
        return False
    return any(category in OBJECT_LIKE for category in categories)


def real_precision(pin_type):
    category = pin_type.get("category")
    return category in ("float", "double") or (
        category == "real" and pin_type.get("subcategory") in ("float", "double"))


def find_pin(pins, name, direction, aliases=None):
    name = canonical_pin(name, direction, pins, aliases)
    return next((item for item in pins if item.get("dir") == direction and item.get("name") == name), None)
