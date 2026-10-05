"""Replay the exact retained PIE diagnostics from the 303-error incident."""

from copy import deepcopy
import json
from pathlib import Path

FIXTURE = Path(__file__).parents[1] / "fixtures" / "runtime-303.json"


def response():
    return json.loads(FIXTURE.read_text(encoding="utf-8"))


def notice():
    return deepcopy(response()["runtime_diagnostics"])


def read_response():
    return dict(ok=True, data=dict(asset_path="/Game/Test.Camera", value="read"),
                diagnostics=[], warnings=[], runtime_diagnostics=notice())
