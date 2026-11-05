"""Only measured project content mounts participate in publication dependencies."""

from ue_node_nexus_mcp.transcode.schema.lock import SchemaLock
from ue_node_nexus_mcp.transcode.sync.dependencies import document_dependencies
from ue_node_nexus_mcp.transcode.sync.project import resolve_context, write_project_info
from ue_node_nexus_mcp.transcode.text.parser import parse


def test_niagara_native_modules_keep_project_plugin_and_material_dependencies(tmp_path):
    schema = SchemaLock(tmp_path, "key")
    schema.project_content_roots = frozenset(("/Game", "/WeatherPlugin", "/Tests"))
    document, sink = parse(
        "nexus: 1\nasset: /Game/Snow\nclass: NiagaraSystem\nschema: key\n\n"
        "[stack Snow/ParticleUpdate]\n"
        "sample : /Niagara/VectorFields/SampleVectorField.SampleVectorField\n"
        "collision : /Niagara/Modules/Collision/Collision.Collision\n"
        "project_force : /WeatherPlugin/Forces/M_Flow.M_Flow\n\n"
        "[renderers Snow]\nflake : Sprite { Material=/Game/M_Snow.M_Snow }\n"
    )
    assert not sink.has_errors, sink.items
    assert document_dependencies(document, "niagara_system", schema) == {
        "/WeatherPlugin/Forces/M_Flow.M_Flow", "/Game/M_Snow.M_Snow"}


def test_project_root_named_niagara_is_kept_when_native_registration_owns_it(tmp_path):
    schema = SchemaLock(tmp_path, "key")
    schema.project_content_roots = frozenset(("/Game", "/Niagara"))
    document, sink = parse(
        "nexus: 1\nasset: /Game/Snow\nclass: NiagaraSystem\nschema: key\n\n"
        "[stack Snow/ParticleUpdate]\nforce : /Niagara/ProjectForce.ProjectForce\n"
    )
    assert not sink.has_errors, sink.items
    assert document_dependencies(document, "niagara_system", schema) == {"/Niagara/ProjectForce.ProjectForce"}


def test_native_mount_binding_survives_offline_schema_resolution(tmp_path):
    roots = [{"root": "/Game", "directory": str(tmp_path / "Content")},
             {"root": "/WeatherPlugin", "directory": str(tmp_path / "Plugins/WeatherPlugin/Content")}]

    def bridge(operation, payload):
        if operation == "project_context_get":
            return dict(ok=True, data=dict(project_name="Weather", project_file_path=str(tmp_path / "Weather.uproject"),
                                           content_mounts=roots))
        assert operation == "bridge_capabilities_get"
        return dict(ok=True, data=dict(schema_key="key", engine_version="5.5.4"))

    context = resolve_context(bridge, cwd=tmp_path)
    assert context.schema.project_content_roots == frozenset(("/Game", "/WeatherPlugin"))
    write_project_info(context)
    offline = resolve_context(bridge, cwd=tmp_path, project_hint="Weather", offline=True)
    assert offline.schema.project_content_roots == context.schema.project_content_roots
