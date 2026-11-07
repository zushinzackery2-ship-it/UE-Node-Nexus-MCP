"""Offline lint names destructive intent before the editor export phase."""

from tests.collaboration.support.publication import project as project, call, ASSET


def test_lint_reports_required_delete_option_without_contacting_editor(project):
    ue, env, workspace, _ = project
    from pathlib import Path

    path = Path(workspace["file_paths"][ASSET])
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines()
    node = next(line.split(":", 1)[0].strip() for line in lines if ": Constant(" in line)
    path.write_text("\n".join(line for line in lines if not line.lstrip().startswith(node + " ")
                              and not line.lstrip().startswith(node + " ->")) + "\n", encoding="utf-8")
    before = len(ue.calls)
    result = call(project, "lint", workspace)
    assert result["required_options"] == dict(allow_delete=True), result
    assert len(ue.calls) == before
