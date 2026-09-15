"""Document pair -> :class:`AssetPlan` (kind dispatch + material / MI / generic)."""

from __future__ import annotations

from .diff_blueprint import diff_blueprint
from .diff_common import diff_prop_section
from .diff_graph import diff_graph_section
from .diff_niagara import diff_niagara
from .model import Document, Section
from .paths import object_path
from .plan import AssetPlan
from .raw_simple import INSTANCE_SECTIONS
from .schema_lock import SchemaLock
from .values import values_equal

FUNCTION_INTERFACE_CLASSES = {"FunctionInput", "FunctionOutput"}


def build_plan(local: Document, base: Document | None, kind: str, ids: dict[str, str] | None = None,
               schema: SchemaLock | None = None) -> AssetPlan:
    """Diff ``local`` (text) against ``base`` (last synced raw); ``base`` None means create.

    ``schema`` resolves class spellings, so a class written as its short name in one
    document and as its full path in the other is not read as a class change.
    """
    plan = AssetPlan(asset_path=object_path(local.header.asset), kind=kind, ids=dict(ids or {}), asset_class=local.header.cls)
    plan.creates_asset = base is None
    if kind == "stub":
        if base is None or _text_equal(local, base) is False:
            plan.error("stub_read_only", "stub files mirror AssetRegistry tags and cannot be pushed", line=1)
        return plan
    if kind in ("material", "material_function"):
        diff_prop_section(local.section("asset"), base.section("asset") if base else None, plan, "set_asset_prop")
        local_graph = local.section("graph") or Section(name="graph")
        base_graph = base.section("graph") if base else None
        diff_graph_section(local_graph, base_graph, plan, kind, schema=schema)
        if kind == "material_function":
            plan.interface_changed = _function_interface_changed(local_graph, base_graph)
    elif kind == "material_instance":
        diff_prop_section(local.section("asset"), base.section("asset") if base else None, plan, "set_asset_prop")
        for name in INSTANCE_SECTIONS:
            _diff_instance_params(local.section(name), base.section(name) if base else None, name, plan)
    elif kind == "asset":
        diff_prop_section(local.section("asset"), base.section("asset") if base else None, plan, "set_asset_prop")
    elif kind == "blueprint":
        diff_blueprint(local, base, plan, schema)
    elif kind in ("niagara_system", "niagara_emitter"):
        diff_niagara(local, base, plan, kind, schema)
    else:
        plan.error("unsupported_kind", f"cannot push kind {kind!r}")
    return plan


def _text_equal(local: Document, base: Document) -> bool:
    from .emitter import emit

    return emit(local) == emit(base)


def _diff_instance_params(local: Section | None, base: Section | None, kind_name: str, plan: AssetPlan) -> None:
    local_props = local.prop_map() if local else {}
    base_props = base.prop_map() if base else {}
    for name, prop in local_props.items():
        before = base_props.get(name)
        if before is None or not values_equal(before.value, prop.value):
            plan.add("mi_set_param", line=prop.line, kind=kind_name, name=name, value=prop.value)
    for name in base_props:
        if name not in local_props:
            plan.add("mi_clear_param", kind=kind_name, name=name)


def _function_interface_changed(local: Section, base: Section | None) -> bool:
    """FunctionInput / FunctionOutput set, names, types or order changed."""
    def signature(section: Section | None) -> list[tuple[str, str, str, str]]:
        if section is None:
            return []
        items: list[tuple[str, str, str, str]] = []
        for decl in section.decls():
            if decl.type_name in FUNCTION_INTERFACE_CLASSES:
                keyed = decl.keyed()
                items.append((decl.type_name, keyed.get("InputName", keyed.get("OutputName", "")), keyed.get("InputType", ""), keyed.get("SortPriority", "")))
        return sorted(items)

    return signature(local) != signature(base)
