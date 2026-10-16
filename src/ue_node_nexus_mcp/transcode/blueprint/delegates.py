"""Native multicast dispatcher references, signatures and binding checks."""

from .classes import node_name
from .members import dispatchers
from .signature import parse_signature
from .types import type_text
from ..errors import DiagnosticSink, SyncError


DELEGATE_NODES = frozenset(("CallDelegate", "AddDelegate", "RemoveDelegate", "ClearDelegate"))


def reference(schema, owner, name):
    full = owner + "." + name
    if schema is None or not owner.startswith("/Script/"):
        return full
    short_owner = owner.rsplit(".", 1)[-1]
    try:
        resolved = schema.delegate(short_owner, name)
    except SyncError as error:
        if error.code != "schema_ambiguous":
            raise
        return full
    return short_owner + "." + name if resolved and resolved["path"] == full else full


def local_records(document):
    rows = dispatchers(document.section("dispatchers"), DiagnosticSink()) if document else dict()
    return dict((name, dict(params=[dict(item.to_raw(), dir="in") for item in signature.inputs],
                            callable=True, assignable=True))
                for name, (signature, _) in rows.items())


def target(decl):
    positional = decl.positional()
    if len(positional) != 1:
        return None
    owner, separator, name = positional[0].rpartition(".")
    return (owner, name) if separator and owner and name else None


def record(decl, document, schema):
    selected = target(decl)
    if selected is None:
        return None
    owner, name = selected
    if owner == "self":
        local = local_records(document)
        if name in local:
            return local[name]
        asset = document.section("asset") if document else None
        parent = asset.prop_map().get("ParentClass") if asset else None
        owner = parent.value if parent else ""
    return schema.delegate(owner, name) if schema and owner else None


def lint(decl, document, schema, sink):
    selected = target(decl)
    if selected is None:
        sink.error("invalid_delegate_reference", "multicast nodes need Owner.Dispatcher or self.Dispatcher",
                   line=decl.line)
        return None
    try:
        resolved = record(decl, document, schema)
    except SyncError as error:
        if error.code != "schema_ambiguous":
            raise
        sink.error("ambiguous_delegate", str(error), line=decl.line)
        return None
    if resolved is None:
        owner, name = selected
        covered = schema and schema.info().get("coverage", dict()).get("delegates") == "registered_multicast_delegate_index"
        asset = document.section("asset") if document else None
        parent = asset.prop_map().get("ParentClass") if asset else None
        pending = owner.startswith("/Game/") or (owner == "self" and parent and parent.value.startswith("/Game/"))
        if covered and not pending:
            sink.error("unknown_delegate", "multicast dispatcher not found: " + owner + "." + name, line=decl.line)
        else:
            sink.warning("delegate_context_required", "dispatcher signature requires current owner reflection: " +
                         owner + "." + name, line=decl.line)
        return None
    permission = "callable" if node_name(decl.type_name) == "CallDelegate" else "assignable"
    if selected[0] != "self" and not resolved.get(permission, False):
        sink.error("delegate_not_exposed", "dispatcher is not exposed for " + node_name(decl.type_name), line=decl.line)
    return resolved


def binding_links(section, document, schema, sink):
    declarations = section.decl_map()
    for link in section.links():
        source, destination = declarations.get(link.src), declarations.get(link.dst)
        if not source or not destination or node_name(destination.type_name) not in ("AddDelegate", "RemoveDelegate"):
            continue
        if link.dst_pin != "Delegate" or node_name(source.type_name) != "CustomEvent":
            continue
        try:
            resolved = record(destination, document, schema)
        except SyncError:
            continue
        if resolved is None:
            continue
        args = source.positional()
        try:
            signature = parse_signature(args[0] + "(" + ",".join(args[1:]) + ")")
        except (IndexError, ValueError):
            continue
        expected = [type_text(item["type"]) for item in resolved.get("params", [])]
        actual = [type_text(item.to_raw()["type"]) for item in signature.inputs]
        if expected != actual:
            sink.error("delegate_signature_mismatch", "event and dispatcher parameter types must match in order",
                       line=link.line)
