"""Interface and dispatcher declarations, independent of graph layout."""

from ..bp_signature import parse_signature
from ..lexer import LexError


def dispatchers(section, sink) -> dict:
    result = dict()
    for entry in section.bares() if section else []:
        try:
            signature = parse_signature(entry.text)
            if signature.outputs or signature.flags:
                raise LexError("dispatchers accept input parameters and a category only", 1)
            if signature.name in result:
                raise LexError("duplicate dispatcher " + signature.name, 1)
            result[signature.name] = (signature, entry.line)
        except LexError as exc:
            sink.error("invalid_dispatcher", str(exc), line=entry.line)
    return result


def interfaces(section, sink) -> dict:
    result = dict()
    for entry in section.bares() if section else []:
        path = entry.text.strip()
        if not path.startswith("/") or "." not in path:
            sink.error("invalid_interface", "interface needs a full native or generated class path", line=entry.line)
        elif path in result:
            sink.error("duplicate_interface", path, line=entry.line)
        else:
            result[path] = entry.line
    return result


def lint_members(document, sink) -> None:
    interfaces(document.section("interfaces"), sink)
    dispatchers(document.section("dispatchers"), sink)


def diff_members(local, base, plan) -> None:
    wanted = interfaces(local.section("interfaces"), plan)
    before = interfaces(base.section("interfaces") if base else None, plan)
    for path in before.keys() - wanted.keys():
        plan.add("bp_interface_remove", interface=path)
    for path, line in wanted.items():
        if path not in before:
            plan.add("bp_interface_add", line=line, interface=path)
    wanted = dispatchers(local.section("dispatchers"), plan)
    before = dispatchers(base.section("dispatchers") if base else None, plan)
    for name in before.keys() - wanted.keys():
        plan.add("bp_dispatcher_remove", name=name)
    for name, (signature, line) in wanted.items():
        if name not in before:
            plan.add("bp_dispatcher_add", line=line, name=name, signature=signature.to_raw())
        elif signature.text() != before[name][0].text():
            plan.add("bp_dispatcher_signature_set", line=line, name=name, signature=signature.to_raw())


def declarations_first(plan) -> None:
    """Stable partition keeps member dependencies ahead of every graph edit."""
    declarations = ("set_asset_prop", "bp_variable_", "bp_component_", "bp_default_", "bp_function_",
                    "bp_local_variable_", "bp_interface_", "bp_dispatcher_", "bp_graph_")
    plan.verbs.sort(key=lambda verb: not verb.op.startswith(declarations))


def interface_changed(plan) -> bool:
    structural = (
        "bp_variable_add", "bp_variable_remove", "bp_variable_rename",
        "bp_component_add", "bp_component_remove", "bp_component_rename",
        "bp_function_add", "bp_function_remove", "bp_function_rename", "bp_function_signature_set",
        "bp_graph_add")
    return any(verb.op in structural or verb.op.startswith(("bp_interface_", "bp_dispatcher_"))
               or (verb.op == "bp_variable_set" and "type" in verb.args) for verb in plan.verbs)
