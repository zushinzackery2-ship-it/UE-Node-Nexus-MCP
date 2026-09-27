"""Connection identity and cardinality use pin contracts, not text spelling."""

from ...blueprint.pins import canonical_pin, node_pins, pin
from ...material.pins import canonical_pin as material_pin, output_property
from ...sync_project import SyncError


def is_exec_pin(bindings: dict, node: str, name, direction: str) -> bool:
    for item in bindings.get(node, dict()).get("meta", dict()).get("pins", []):
        if item.get("dir") != direction or (name is not None and item.get("name") != name):
            continue
        pin_type = item.get("type") or dict()
        if isinstance(pin_type, dict) and pin_type.get("category") == "exec":
            return True
        if name is not None:
            return False
    return False


def with_contracts(section, aliases, bindings, document, schema) -> dict:
    result = dict(bindings)
    if document is None:
        return result
    for decl in section.decls():
        identifier = aliases[decl.id]
        metadata = bindings.get(identifier, dict()).get("meta", dict())
        pins = node_pins(decl, document, section, schema, metadata)
        result[identifier] = dict(meta=dict(pins=pins))
    if section.name == "function":
        result["@entry"] = dict(meta=dict(pins=[pin("then", "out")]))
        result["@result"] = dict(meta=dict(pins=[pin("execute", "in")]))
    return result


def encode_links(section, aliases, bindings, document=None, schema=None, kind="blueprint") -> dict:
    """Exec outputs and data inputs each own one connection slot.

    Reflected pins, authored function signatures and native node contracts also
    describe newly created nodes. Multiple executions may meet at one input.
    """
    known = with_contracts(section, aliases, bindings, document if kind == "blueprint" else None, schema)
    decls = section.decl_map()
    result = dict()
    for link in section.links():
        src, dst = aliases.get(link.src, "@" + link.src), aliases.get(link.dst, "@" + link.dst)
        src_pins = known.get(src, dict()).get("meta", dict()).get("pins", [])
        dst_pins = known.get(dst, dict()).get("meta", dict()).get("pins", [])
        if kind in ("material", "material_function"):
            source = material_pin(decls.get(link.src), link.src_pin, "out", known.get(src, dict()).get("meta", dict()), schema)
            target = material_pin(decls.get(link.dst), link.dst_pin, "in", known.get(dst, dict()).get("meta", dict()), schema)
            if kind == "material" and dst == "@out":
                target = output_property(link.dst_pin)
        else:
            source = canonical_pin(link.src_pin, "out", src_pins)
            target = canonical_pin(link.dst_pin, "in", dst_pins)
        record = dict(src=src, dst=dst, src_pin=source, dst_pin=target)
        execution = is_exec_pin(known, src, source, "out") or is_exec_pin(known, dst, target, "in")
        slot = f"out:{src}:{source or ''}" if execution else f"in:{dst}:{target or ''}"
        if slot in result and result[slot] != record:
            endpoint = (link.src, source) if execution else (link.dst, target)
            raise SyncError("pin_cardinality", f"multiple connections occupy {endpoint[0]}.{endpoint[1] or ''}")
        result[slot] = record
    return result
