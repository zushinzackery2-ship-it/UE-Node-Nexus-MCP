"""Custom-input layout; expression targets are carried by graph connections."""

from ..text.lexer import split_top_level
from ..errors import SyncError


def fields(text):
    source = str(text).strip()
    if not source.startswith("(") or not source.endswith(")"):
        raise SyncError("invalid_custom_inputs", "Custom.Inputs requires an ExportText array/structure")
    return split_top_level(source[1:-1], ",") if source[1:-1].strip() else []


def layout(text):
    if text is None:
        return None
    items = []
    for item in fields(text):
        members = []
        for field in fields(item):
            name, separator, content = field.partition("=")
            if not separator:
                raise SyncError("invalid_custom_inputs", "Custom.Inputs member needs a name and value")
            if name.strip() == "Input":
                inner = [entry for entry in fields(content) if entry.partition("=")[0].strip() != "Expression"]
                content = "(" + ",".join(inner) + ")"
            members.append(name.strip() + "=" + content.strip())
        items.append("(" + ",".join(members) + ")")
    return "(" + ",".join(items) + ")"


def is_custom(decl):
    return decl.type_name.rsplit(".", 1)[-1].removeprefix("MaterialExpression") == "Custom"


def normalize_args(decl, args):
    result = dict(args)
    if is_custom(decl) and "Inputs" in result:
        result["Inputs"] = layout(result["Inputs"])
    return result


def rewire_inputs(graph, plan):
    declarations = graph.decl_map()
    edited = {verb.args.get("id") for verb in plan.verbs if verb.op in ("create_node", "set_node_param")
              and verb.args.get("id") in declarations and is_custom(declarations[verb.args["id"]])
              and (verb.op == "create_node" or verb.args.get("name") == "Inputs")}
    for verb in plan.verbs:
        if verb.args.get("id") not in edited:
            continue
        if verb.op == "create_node":
            verb.args["params"] = normalize_args(declarations[verb.args["id"]], verb.args["params"])
        elif verb.op == "set_node_param" and verb.args.get("name") == "Inputs":
            verb.args["value"] = layout(verb.args["value"])
    existing = {(verb.args.get("to"), verb.args.get("to_pin") or "0") for verb in plan.verbs if verb.op == "connect_pins"}
    for link in graph.links():
        if link.dst in edited and (link.dst, link.dst_pin or "0") not in existing:
            plan.add("connect_pins", line=link.line, from_pin=link.src_pin, to_pin=link.dst_pin,
                     **{"from": link.src, "to": link.dst})
            existing.add((link.dst, link.dst_pin or "0"))
