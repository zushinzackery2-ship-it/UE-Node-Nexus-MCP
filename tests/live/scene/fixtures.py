"""Author a mixed Blueprint/Actor/ISM/HISM scene through public mirror syntax."""

from __future__ import annotations

from ue_node_nexus_mcp.transcode.model import Decl, Document, Header, Prop, Section


def transform(x: float = 0, y: float = 0, z: float = 0) -> str:
    return f"(Rotation=(X=0,Y=0,Z=0,W=1),Translation=(X={x},Y={y},Z={z}),Scale3D=(X=1,Y=1,Z=1))"


def author(workspace) -> None:
    blueprint = f"/Game/NexusAcceptance/{workspace.name}/BP_Actor.BP_Actor"
    file = workspace.project / "NexusAcceptance" / workspace.name / "BP_Actor.bp.nexus"
    file.parent.mkdir(parents=True, exist_ok=True)
    file.write_text(
        f"nexus: 1\nasset: {blueprint}\nclass: Blueprint\nschema: {workspace.schema}\n\n"
        "[asset]\nParentClass = /Script/Engine.Actor\n\n"
        "[variables]\nValue : float = 1 { InstanceEditable }\n\n"
        "[components]\nMesh : StaticMeshComponent { StaticMesh=/Engine/BasicShapes/Cube.Cube }\n",
        encoding="utf-8")
    document = Document(Header(asset=workspace.map_path, cls="World", schema=workspace.schema))
    document.sections.append(Section("scene", entries=[Prop("Name", workspace.name)]))
    actors = document.ensure_section("actors")
    for name in ("root", "ism", "hism", "blueprint"):
        props = [("Label", name), ("Folder", "NexusAcceptance/" + workspace.name), ("Transform", transform(z=200))]
        if name == "ism":
            props.append(("Parent", "root"))
        if name == "blueprint":
            props.append(("Value", "2"))
        actors.entries.append(Decl(name, blueprint if name == "blueprint" else "/Script/Engine.Actor", props=props))
    document.ensure_section("components", "root").entries.append(Decl("root", "/Script/Engine.SceneComponent"))
    for name, cls in (("ism", "InstancedStaticMeshComponent"), ("hism", "HierarchicalInstancedStaticMeshComponent")):
        document.ensure_section("components", name).entries.append(Decl("mesh", "/Script/Engine." + cls,
            props=[("StaticMesh", "/Engine/BasicShapes/Cube.Cube"), ("CustomDataCount", "1")]))
        section = document.ensure_section("instances", name + ".mesh")
        for index in range(4):
            section.entries.append(Decl(f"tile_{index}", "Instance", props=[("Transform", transform(x=index * 120)), ("CustomData", f"({index / 4})")]))
    workspace.write(document)
