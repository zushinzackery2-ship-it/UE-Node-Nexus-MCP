"""Local function and custom-event signatures shared by lint and pin contracts."""

from .classes import node_name
from .signature import parse_signature
from ..text.lexer import LexError


def event_signature(decl):
    positional = decl.positional()
    if not positional:
        raise LexError("CustomEvent needs an event name", 1)
    signature = parse_signature(positional[0] + "(" + ",".join(positional[1:]) + ")")
    for item in signature.inputs:
        item.to_raw()
    return signature


def local_records(document):
    records = dict()
    for section in document.find_sections("function"):
        try:
            signature = parse_signature(section.args)
            records[signature.name] = record(signature)
        except LexError:
            continue
    for section, decl in document.iter_decls():
        if section.name not in ("graph", "function") or node_name(decl.type_name) != "CustomEvent":
            continue
        try:
            signature = event_signature(decl)
            records[signature.name] = record(signature)
        except LexError:
            continue
    return records


def record(signature):
    params = [dict(item.to_raw(), dir="in") for item in signature.inputs]
    params.extend(dict(item.to_raw(), dir="out") for item in signature.outputs)
    return dict(params=params, pure="Pure" in signature.flags)
