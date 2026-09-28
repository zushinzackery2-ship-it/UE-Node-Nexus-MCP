"""Issue 4 #2 and #3 in the real editor: a sparse push and one function interface batch.

#2: a worktree that projects one material publishes it without paths, while an
unrelated Blueprint it inherited calls ``CallParentFunction(Actor.UserConstructionScript)``.
#3: a MaterialFunction gains a required input; the caller in the batch wires it in
the same publication, the caller outside the batch fails on its own and says which
function it follows, and wiring that caller afterwards publishes it.
"""

from __future__ import annotations

from pathlib import Path
import re

from ue_node_nexus_mcp.transcode.storage.paths import text_path
from tests.live.collaboration.workflow import Workflow

FOLDER = "/Game/Issues4/"
LAMP = FOLDER + "BP_Lamp.BP_Lamp"
GLASS = FOLDER + "M_Glass.M_Glass"
BLEND = FOLDER + "MF_Blend.MF_Blend"
CALLER = FOLDER + "M_Caller.M_Caller"
OTHER = FOLDER + "M_Other.M_Other"
CALL = ("[asset]\nShadingModel = MSM_Unlit\n\n[graph]\nvalue : Constant(R=0.5)\n"
        f"call : MaterialFunctionCall(MaterialFunction={BLEND})\nvalue -> call.A\ncall.Result -> out.EmissiveColor\n")
SOURCES = {
    GLASS: ("material", "Material", "[asset]\nShadingModel = MSM_Unlit\n\n[graph]\nvalue : Constant(R=0.25)\nvalue -> out.EmissiveColor\n"),
    LAMP: ("blueprint", "Blueprint", "[asset]\nParentClass = /Script/Engine.Actor\n\n[function UserConstructionScript()]\n"
                                     "parent : CallParentFunction(Actor.UserConstructionScript)\nentry.then -> parent.execute\n"),
    BLEND: ("material_function", "MaterialFunction", "[graph]\nin_a : FunctionInput(InputName=A, InputType=FunctionInput_Scalar, SortPriority=0)\n"
                                                     "result : FunctionOutput(OutputName=Result)\nin_a -> result\n"),
    CALLER: ("material", "Material", CALL),
    OTHER: ("material", "Material", CALL),
}
# ``B`` is required: bUsePreviewValueAsDefault stays false, so a call must connect it.
# An export omits the pin of a single-input node, so the links are matched as lines.
REQUIRED_INPUT = (r"in_a -> result(\.\w+)?", "in_b : FunctionInput(InputName=B, InputType=FunctionInput_Scalar, SortPriority=1)\n"
                                             "sum : Add\nin_a -> sum.A\nin_b -> sum.B\nsum -> result")
WIRE_B = (r"value -> call(\.A)?", "value -> call.A\nsecond : Constant(R=0.25)\nsecond -> call.B")


def edit(workspace, asset, pattern, replacement) -> None:
    """Replace the one line matching ``pattern`` in ``asset``'s file."""
    path = Path(workspace["file_paths"][asset])
    lines = path.read_text(encoding="utf-8").splitlines()
    matches = [index for index, line in enumerate(lines) if re.fullmatch(pattern, line.strip())]
    assert len(matches) == 1, (asset, pattern, lines)
    lines[matches[0]:matches[0] + 1] = replacement.splitlines()
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def substitute(workspace, asset, old, new) -> None:
    path = Path(workspace["file_paths"][asset])
    text = path.read_text(encoding="utf-8")
    assert text.count(old) == 1, (asset, old, text)
    path.write_text(text.replace(old, new), encoding="utf-8", newline="\n")


def rows(result) -> dict:
    return dict((row["asset"], row) for row in result["rows"])


class Issues4:
    def __init__(self, session) -> None:
        self.flow = Workflow(session)

    def create(self) -> dict:
        """Author the fixtures through a full worktree, the way an agent would."""
        self.flow.prepare()
        author = self.flow.call("checkout", agent_id="author")
        key = author["schema_key"]
        for asset, (kind, cls, body) in SOURCES.items():
            path = text_path(Path(author["files_root"]), asset, kind)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(f"nexus: 1\nasset: {asset.split('.', 1)[0]}\nclass: {cls}\nschema: {key}\n\n{body}", encoding="utf-8", newline="\n")
        lint = self.flow.call("lint", author)
        assert lint["error_count"] == 0, lint
        self.flow.commit(author, "Issues4 fixtures")
        created = self.flow.call("push", author)
        assert created["status"] == "published" and created["error_count"] == 0, created
        self.author = self.flow.call("checkout", agent_id="author-2")
        return dict(created=sorted(row["asset"] for row in created["rows"] if row["action"] == "pushed"))

    def sparse_push(self) -> dict:
        """#2: the inherited Blueprint is neither observed nor validated by this push."""
        full_lint = self.flow.call("lint", self.author)
        assert full_lint["error_count"] == 0, full_lint
        sparse = self.flow.call("checkout", paths=[GLASS], agent_id="sparse")
        assert list(sparse["file_paths"]) == [GLASS], sparse["file_paths"]
        substitute(sparse, GLASS, "R=0.25", "R=0.3")
        self.flow.commit(sparse, "one material")
        result = self.flow.call("push", sparse)
        assert result["status"] == "published" and result["errors"] == dict(), result
        assert list(rows(result)) == [GLASS], result["rows"]
        return dict(workspace=sparse["id"], rows=result["rows"], lint_errors=full_lint["error_count"])

    def interface_batch(self) -> dict:
        """#3: the batch's own caller compiles once with its wiring; the other caller owns its failure."""
        author = self.author
        self.flow.call("pull", author)
        edit(author, BLEND, *REQUIRED_INPUT)
        edit(author, CALLER, *WIRE_B)
        lint = self.flow.call("lint", author)
        assert lint["error_count"] == 0, lint
        self.flow.commit(author, "required input and its wiring")
        result = self.flow.call("push", author)
        found = rows(result)
        assert found[BLEND]["action"] == "pushed", result
        assert found[CALLER]["action"] == "pushed" and found[CALLER]["refreshed"] == [BLEND], found[CALLER]
        failure = result["errors"].get(OTHER)
        assert failure and failure["function"] == BLEND and failure["asset"] == OTHER, result["errors"]
        assert set(result["errors"]) == set((OTHER,)), result["errors"]
        # The failure is the caller's to fix, and fixing its wiring is all it takes.
        self.flow.call("pull", author)
        edit(author, OTHER, *WIRE_B)
        self.flow.commit(author, "wire the other caller")
        repaired = self.flow.call("push", author, paths=[OTHER])
        assert repaired["status"] == "published" and rows(repaired)[OTHER]["action"] == "pushed", repaired
        return dict(batch=result["rows"], failure=failure, repaired=repaired["rows"])
