from __future__ import annotations

from typing import Any, Literal

from .contracts import require_list, require_non_empty_string
from .runtime import default_tool
from .tools_niagara_common import _call_niagara
from .tools_niagara_properties import niagara_renderer_properties_get, niagara_renderer_properties_set, niagara_system_properties_get, niagara_system_properties_set  # noqa: F401


@default_tool("niagara")
def niagara_system_create(
    asset_path: str,
    source_asset_path: str | None = None,
    create_default_nodes: bool = True,
    dry_run: bool = True,
    save: bool = False,
    format: Literal["summary", "full"] = "summary",
) -> dict[str, Any]:
    """Create or copy a Niagara system asset; summary keeps default responses compact."""
    require_non_empty_string(asset_path, "asset_path")
    return _call_niagara(
        "niagara_system_create",
        {
            "asset_path": asset_path,
            "source_asset_path": source_asset_path,
            "create_default_nodes": create_default_nodes,
            "dry_run": dry_run,
            "save": save,
            "format": format,
        },
    )


@default_tool("niagara")
def niagara_system_duplicate(
    source_asset_path: str,
    destination_asset_path: str,
    dry_run: bool = True,
    save: bool = False,
    format: Literal["summary", "full"] = "summary",
) -> dict[str, Any]:
    """Duplicate a Niagara system asset; summary keeps default responses compact."""
    require_non_empty_string(source_asset_path, "source_asset_path")
    require_non_empty_string(destination_asset_path, "destination_asset_path")
    return _call_niagara(
        "niagara_system_duplicate",
        {
            "source_asset_path": source_asset_path,
            "destination_asset_path": destination_asset_path,
            "dry_run": dry_run,
            "save": save,
            "format": format,
        },
    )


@default_tool("niagara")
def niagara_system_summary_get(asset_path: str, format: Literal["compact", "full", "indexed", "tiny"] = "indexed") -> dict[str, Any]:
    """Return Niagara asset readiness counts; indexed/tiny keep default responses compact."""
    require_non_empty_string(asset_path, "asset_path")
    return _call_niagara("niagara_system_summary_get", {"asset_path": asset_path, "format": format})


@default_tool("niagara")
def niagara_asset_lint(asset_path: str, format: Literal["indexed", "tiny", "full"] = "indexed") -> dict[str, Any]:
    """Report generic Niagara authoring risks; indexed is the compact default."""
    require_non_empty_string(asset_path, "asset_path")
    return _call_niagara("niagara_asset_lint", {"asset_path": asset_path, "format": format})


@default_tool("niagara")
def niagara_emitters_list(asset_path: str, format: Literal["compact", "full", "indexed", "tiny"] = "indexed") -> dict[str, Any]:
    """List existing emitters; indexed/tiny keep default responses compact."""
    require_non_empty_string(asset_path, "asset_path")
    return _call_niagara("niagara_emitters_list", {"asset_path": asset_path, "format": format})


@default_tool("niagara")
def niagara_user_params_get(asset_path: str, format: Literal["compact", "full", "indexed", "tiny"] = "indexed") -> dict[str, Any]:
    """List exposed Niagara User parameters; indexed/tiny omit values to keep default responses compact."""
    require_non_empty_string(asset_path, "asset_path")
    return _call_niagara("niagara_user_params_get", {"asset_path": asset_path, "format": format})


@default_tool("niagara")
def niagara_user_params_set(
    asset_path: str,
    params: list[dict[str, Any]],
    dry_run: bool = True,
    save: bool = False,
) -> dict[str, Any]:
    """Set existing or addable simple Niagara User parameters with typed validation."""
    require_non_empty_string(asset_path, "asset_path")
    require_list(params, "params")
    return _call_niagara("niagara_user_params_set", {"asset_path": asset_path, "params": params, "dry_run": dry_run, "save": save})


@default_tool("niagara")
def niagara_materials_get(asset_path: str, format: Literal["compact", "full", "indexed", "tiny"] = "indexed") -> dict[str, Any]:
    """List materials on existing sprite/ribbon/mesh renderers; this tool cannot create renderers."""
    require_non_empty_string(asset_path, "asset_path")
    return _call_niagara("niagara_materials_get", {"asset_path": asset_path, "format": format})


@default_tool("niagara")
def niagara_materials_set(
    asset_path: str,
    material_path: str,
    emitter_index: int | None = None,
    renderer_index: int | None = None,
    dry_run: bool = True,
    save: bool = False,
) -> dict[str, Any]:
    """Assign a material to existing sprite/ribbon/mesh renderers; no-op when no matching renderer exists."""
    require_non_empty_string(asset_path, "asset_path")
    require_non_empty_string(material_path, "material_path")
    return _call_niagara(
        "niagara_materials_set",
        {
            "asset_path": asset_path,
            "material_path": material_path,
            "emitter_index": emitter_index,
            "renderer_index": renderer_index,
            "dry_run": dry_run,
            "save": save,
        },
    )


@default_tool("niagara")
def niagara_emitter_create(
    asset_path: str,
    mode: Literal["default", "empty", "from_asset"] = "default",
    source_emitter_path: str | None = None,
    name: str | None = None,
    dry_run: bool = True,
    save: bool = False,
) -> dict[str, Any]:
    """Add a generic empty/default emitter or copy an emitter asset into a Niagara system."""
    require_non_empty_string(asset_path, "asset_path")
    return _call_niagara(
        "niagara_emitter_create",
        {
            "asset_path": asset_path,
            "mode": mode,
            "source_emitter_path": source_emitter_path,
            "name": name,
            "dry_run": dry_run,
            "save": save,
        },
    )


@default_tool("niagara")
def niagara_emitter_properties_get(asset_path: str, emitter_index: int) -> dict[str, Any]:
    """Read compact editable handle/emitter settings for one Niagara emitter."""
    require_non_empty_string(asset_path, "asset_path")
    return _call_niagara("niagara_emitter_properties_get", {"asset_path": asset_path, "emitter_index": emitter_index})


@default_tool("niagara")
def niagara_emitter_properties_set(
    asset_path: str,
    emitter_index: int,
    params: list[dict[str, Any]],
    dry_run: bool = True,
    save: bool = False,
) -> dict[str, Any]:
    """Patch compact emitter settings: name, enabled, local_space, determinism, random_seed, sim_target."""
    require_non_empty_string(asset_path, "asset_path")
    require_list(params, "params")
    return _call_niagara(
        "niagara_emitter_properties_set",
        {"asset_path": asset_path, "emitter_index": emitter_index, "params": params, "dry_run": dry_run, "save": save},
    )


@default_tool("niagara")
def niagara_renderers_list(asset_path: str, format: Literal["compact", "full", "indexed", "tiny"] = "indexed") -> dict[str, Any]:
    """List all Niagara renderers with indexes and primary material paths."""
    require_non_empty_string(asset_path, "asset_path")
    return _call_niagara("niagara_renderers_list", {"asset_path": asset_path, "format": format})


@default_tool("niagara")
def niagara_renderer_create(
    asset_path: str,
    emitter_index: int,
    renderer_type: Literal["sprite", "ribbon", "mesh", "light", "component", "decal", "volume"] = "sprite",
    dry_run: bool = True,
    save: bool = False,
) -> dict[str, Any]:
    """Add a generic Niagara renderer to an existing emitter."""
    require_non_empty_string(asset_path, "asset_path")
    return _call_niagara(
        "niagara_renderer_create",
        {
            "asset_path": asset_path,
            "emitter_index": emitter_index,
            "renderer_type": renderer_type,
            "dry_run": dry_run,
            "save": save,
        },
    )


@default_tool("niagara")
def niagara_compile(asset_path: str, wait: bool = True, save: bool = False) -> dict[str, Any]:
    """Compile a Niagara system and report asset readiness warnings."""
    require_non_empty_string(asset_path, "asset_path")
    return _call_niagara("niagara_compile", {"asset_path": asset_path, "wait": wait, "save": save})
