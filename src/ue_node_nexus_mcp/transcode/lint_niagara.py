"""Niagara system / emitter lint."""

from __future__ import annotations

from .errors import DiagnosticSink
from .lint_material import check_value
from .model import Decl, Document, Section
from .raw_niagara import STACK_GROUPS
from .schema_lock import SchemaLock
from .sync_project import SyncError
from .values import is_marker


def lint_niagara(document: Document, kind: str, schema: SchemaLock | None, sink: DiagnosticSink) -> None:
    emitters = {section.args.strip() for section in document.find_sections("emitter")}
    for section in document.find_sections("emitter"):
        if kind != "niagara_system":
            sink.error("unknown_section", "[emitter] sections only exist in Niagara systems", line=section.line)
        if not section.args.strip():
            sink.error("missing_emitter_name", "[emitter Name] needs a name", line=section.line)
    for section in document.find_sections("stack"):
        _lint_stack(section, kind, emitters, schema, sink)
    for section in document.find_sections("renderers"):
        _lint_renderers(section, kind, emitters, schema, sink)
    user = document.section("user")
    if user is not None:
        for decl in user.decls():
            if decl.default is None:
                sink.error("missing_value", f"user parameter {decl.id!r} needs '= value'", line=decl.line)


def _split_stack_args(args: str, kind: str, emitters: set[str], line: int, sink: DiagnosticSink) -> tuple[str, str] | None:
    text = args.strip()
    if kind == "niagara_system":
        if "/" not in text:
            sink.error("invalid_stack", "[stack Emitter/Group] needs the emitter prefix in a Niagara system", line=line)
            return None
        emitter, group = text.split("/", 1)
        if emitter not in emitters:
            sink.error("unknown_emitter", f"stack references undeclared emitter {emitter!r}", line=line)
            return None
        return emitter, group
    if "/" in text:
        sink.error("invalid_stack", "[stack Group] takes no emitter prefix in an emitter asset", line=line)
        return None
    return "", text


def _lint_stack(section: Section, kind: str, emitters: set[str], schema: SchemaLock | None, sink: DiagnosticSink) -> None:
    parsed = _split_stack_args(section.args, kind, emitters, section.line, sink)
    if parsed is None:
        return
    _, group = parsed
    opaque_group = group.startswith("Event:") or group.startswith("Stage:")
    if group not in STACK_GROUPS and not opaque_group:
        sink.error("unknown_stack_group", f"unknown stack group {group!r}; valid: {', '.join(STACK_GROUPS)}, Event:<name>, Stage:<name>", line=section.line)
        return
    for decl in section.decls():
        if opaque_group:
            if not decl.opaque:
                sink.error("opaque_stack_edit", f"{group} stacks are read-only in this version", line=decl.line)
            continue
        _lint_module(decl, schema, sink)


def _lint_module(decl: Decl, schema: SchemaLock | None, sink: DiagnosticSink) -> None:
    if decl.opaque:
        return
    if decl.positional():
        sink.error("unexpected_positional", "module lines take only Input=Value arguments", line=decl.line)
    for key, value in decl.args:
        if key is not None and value.startswith("@") and not is_marker(value):
            sink.error("invalid_marker", f"{key}={value!r}: markers are @link(Namespace.Name) or @dynamic", line=decl.line)
    if schema is None or decl.type_name == "SetVariables":
        # Set Variables modules own a per-node generated script; their inputs are the assigned variables
        return
    # A pull records the exact script of every module it exported, so a declaration that
    # only writes the short name still resolves to the script the editor actually holds.
    declared = str(decl.meta.get("script") or "")
    try:
        path, record, ambiguous = schema.niagara_module(declared) if declared else (None, None, [])
        if record is None:
            path, record, ambiguous = schema.niagara_module(decl.type_name)
    except SyncError as exc:
        if exc.code != "schema_ambiguous":
            raise
        path, record = None, None
        ambiguous = [str(item) for item in (exc.details.get("candidates") or [])]
    if record is None:
        if ambiguous:
            sink.error("ambiguous_module", f"module {decl.type_name!r} matches several scripts; use the full path: {', '.join(ambiguous[:5])}", line=decl.line)
        else:
            # the module index only covers scripts that were loaded when the schema was exported
            sink.warning("unknown_module", f"module script {decl.type_name!r} is not in the schema lock; push resolves it live", line=decl.line)
        return
    decl.meta.setdefault("script", path)
    if record.get("inputs") is None:
        return  # indexed from the asset registry only: the script was not loaded when the schema was exported
    inputs = {str(item.get("name", "")) for item in record.get("inputs") or []}
    for key, _ in decl.args:
        if key is not None and key not in inputs:
            sink.error("unknown_input", f"module {decl.type_name} has no input {key!r}; inputs: {', '.join(sorted(inputs))}", line=decl.line)


def _lint_renderers(section: Section, kind: str, emitters: set[str], schema: SchemaLock | None, sink: DiagnosticSink) -> None:
    prefix = section.args.strip()
    if kind == "niagara_system" and prefix not in emitters:
        sink.error("unknown_emitter", f"renderers section references undeclared emitter {prefix!r}", line=section.line)
    if kind == "niagara_emitter" and prefix:
        sink.error("invalid_renderers", "[renderers] takes no emitter name in an emitter asset", line=section.line)
    for decl in section.decls():
        if decl.args:
            sink.error("unexpected_args", "renderer lines take their properties in { ... }", line=decl.line)
        if schema is None:
            continue
        info = schema.resolve_class("niagara_renderer", decl.type_name)
        if info is None:
            sink.error("unknown_class", f"unknown Niagara renderer class {decl.type_name!r}", line=decl.line)
            continue
        for key, value in decl.props:
            if key not in info.props:
                sink.error("unknown_property", f"{decl.type_name} has no editable property {key!r}", line=decl.line)
            elif value is not None:
                check_value(key, value, info.props[key], decl.line, sink)
