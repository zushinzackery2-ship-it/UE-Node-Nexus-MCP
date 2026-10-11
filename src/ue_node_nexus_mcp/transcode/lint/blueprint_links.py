"""Check numeric links using native K2 real-precision compatibility."""

from ..blueprint.pins import canonical_pin, node_pins

NUMERIC = frozenset(("byte", "int", "int64", "real", "float", "double"))


def lint_links(section, document, schema, sink):
    declarations = section.decl_map()
    pins = dict((decl.id, node_pins(decl, document, section, schema))
                for decl in section.decls() if not decl.opaque and decl.modifier != "local")
    for link in section.links():
        if link.src not in declarations or link.dst not in declarations:
            continue
        source = find_pin(pins.get(link.src, []), link.src_pin, "out")
        target = find_pin(pins.get(link.dst, []), link.dst_pin, "in")
        if not source or not target:
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


def real_precision(pin_type):
    category = pin_type.get("category")
    return category in ("float", "double") or (
        category == "real" and pin_type.get("subcategory") in ("float", "double"))


def find_pin(pins, name, direction):
    name = canonical_pin(name, direction, pins)
    return next((item for item in pins if item.get("dir") == direction and item.get("name") == name), None)
