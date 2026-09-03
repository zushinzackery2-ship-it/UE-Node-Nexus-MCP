"""Offline lint: structure for every kind, schema-driven checks per kind."""

from __future__ import annotations

from .errors import DiagnosticSink
from .lint_blueprint import lint_blueprint
from .lint_material import lint_material_graph
from .lint_niagara import lint_niagara
from .model import Bare, Decl, Document, Link, Prop, Section
from .raw_simple import INSTANCE_SECTIONS
from .schema_lock import SchemaLock

KIND_CLASS = {
    "material": ("Material",),
    "material_function": ("MaterialFunction",),
    "material_instance": ("MaterialInstanceConstant",),
    "blueprint": ("Blueprint",),
    "niagara_system": ("NiagaraSystem",),
    "niagara_emitter": ("NiagaraEmitter",),
}
ALLOWED_SECTIONS = {
    "material": {"asset", "graph"},
    "material_function": {"asset", "graph"},
    "material_instance": {"asset", *INSTANCE_SECTIONS},
    "asset": {"asset"},
    "stub": {"tags"},
    "blueprint": {"asset", "variables", "components", "defaults", "dispatchers", "interfaces", "graph", "function", "macro"},
    "niagara_system": {"asset", "user", "emitter", "stack", "renderers"},
    "niagara_emitter": {"asset", "user", "stack", "renderers"},
}
GRAPH_SECTIONS = {"graph", "function", "macro"}
BARE_SECTIONS = {"dispatchers", "interfaces"}
MATERIAL_OUTPUT_PROPERTIES = (
    "MaterialAttributes", "BaseColor", "Metallic", "Specular", "Roughness", "Anisotropy", "EmissiveColor", "Opacity",
    "OpacityMask", "Normal", "Tangent", "SubsurfaceColor", "AmbientOcclusion", "Refraction", "CustomizedUVs_0",
    "CustomizedUVs_1", "CustomizedUVs_2", "CustomizedUVs_3", "CustomizedUVs_4", "CustomizedUVs_5", "CustomizedUVs_6",
    "CustomizedUVs_7", "PixelDepthOffset", "ShadingModel", "Displacement",
    "WorldPositionOffset", "ClearCoat", "ClearCoatRoughness", "SurfaceThickness", "FrontMaterial",
)


def lint_document(document: Document, kind: str, schema: SchemaLock | None, file: str | None = None, current_key: str | None = None) -> DiagnosticSink:
    sink = DiagnosticSink(file=file)
    _lint_header(document, kind, sink, current_key)
    _lint_structure(document, kind, sink)
    if sink.has_errors:
        return sink
    if schema is None or not schema.available:
        sink.warning("schema_missing", "schema lock not available; class/property checks skipped")
        schema = None
    if kind in ("material", "material_function"):
        lint_material_graph(document, kind, schema, sink)
    elif kind == "blueprint":
        lint_blueprint(document, schema, sink)
    elif kind in ("niagara_system", "niagara_emitter"):
        lint_niagara(document, kind, schema, sink)
    elif kind == "asset" and schema is not None:
        _lint_asset_props(document, schema, sink)
    return sink


def _lint_header(document: Document, kind: str, sink: DiagnosticSink, current_key: str | None) -> None:
    header = document.header
    if header.nexus != 1:
        sink.error("unsupported_format_version", f"nexus format version {header.nexus} is not supported", line=1)
    if not header.asset.startswith("/Game/"):
        sink.error("invalid_asset", f"asset must be a /Game package path, got {header.asset!r}", line=2)
    if not header.cls:
        sink.error("missing_class", "class header is required", line=3)
    expected = KIND_CLASS.get(kind)
    if expected and header.cls not in expected:
        sink.error("class_kind_mismatch", f"class {header.cls!r} does not match file kind {kind!r} (expected {', '.join(expected)})", line=3)
    if current_key and header.schema and header.schema != current_key:
        sink.warning("schema_stale", f"file was written against schema {header.schema}, current is {current_key}", line=4)


def _lint_structure(document: Document, kind: str, sink: DiagnosticSink) -> None:
    allowed = ALLOWED_SECTIONS.get(kind, set())
    seen: set[tuple[str, str]] = set()
    for section in document.sections:
        if section.name not in allowed:
            sink.error("unknown_section", f"section [{section.name}] is not valid for {kind}", line=section.line)
            continue
        identity = section.identity()
        if identity in seen:
            sink.error("duplicate_section", f"duplicate section {section.header()}", line=section.line)
        seen.add(identity)
        _lint_section_entries(section, kind, sink)


def _lint_section_entries(section: Section, kind: str, sink: DiagnosticSink) -> None:
    prop_keys: set[str] = set()
    decl_ids: set[str] = set()
    implicit = set(section.meta.get("implicit_nodes", {})) if section.meta else set()
    if section.name == "function":
        implicit |= {"entry", "result"}
    if section.name == "graph" and kind == "material":
        implicit.add("out")
    for entry in section.entries:
        if isinstance(entry, Prop):
            if entry.key in prop_keys:
                sink.error("duplicate_property", f"duplicate property {entry.key!r}", line=entry.line)
            prop_keys.add(entry.key)
        elif isinstance(entry, Decl):
            if entry.id in decl_ids or entry.id in implicit:
                sink.error("duplicate_id", f"duplicate id {entry.id!r}", line=entry.line)
            decl_ids.add(entry.id)
            if entry.opaque and not entry.marker_arg():
                sink.error("opaque_without_class", "@opaque needs its class path: @opaque(/Script/...)", line=entry.line)
        elif isinstance(entry, Bare) and section.name not in BARE_SECTIONS:
            sink.error("syntax", f"unrecognized line: {entry.text}", line=entry.line)
    for entry in section.entries:
        if isinstance(entry, Link):
            if section.name not in GRAPH_SECTIONS:
                sink.error("link_outside_graph", "links are only valid inside graph sections", line=entry.line)
                continue
            for endpoint in (entry.src, entry.dst):
                if endpoint not in decl_ids and endpoint not in implicit:
                    sink.error("unknown_link_endpoint", f"link references unknown node {endpoint!r}", line=entry.line)


def _lint_asset_props(document: Document, schema: SchemaLock, sink: DiagnosticSink) -> None:
    info = schema.resolve_class("asset", document.header.cls)
    if info is None:
        sink.warning("class_not_in_schema", f"class {document.header.cls} is not in the schema lock; property names not checked")
        return
    section = document.section("asset")
    for prop in section.props() if section else []:
        if prop.key not in info.props:
            sink.error("unknown_property", f"{document.header.cls} has no editable property {prop.key!r}", line=prop.line)
