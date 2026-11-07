"""A protocol simulator cannot silently certify an unimplemented native command."""

import pytest

from tests.transcode.fake_ue import FakeUe
from tests.transcode.fixtures import material_raw


@pytest.mark.parametrize("operation", ["transcode_apply", "vfx_transcode_apply"])
def test_unknown_plan_verb_cannot_return_a_successful_compile(operation):
    raw = material_raw()
    bridge = FakeUe({"material": raw})
    with pytest.raises(AssertionError, match="does not model"):
        bridge.call(operation, dict(asset_path=raw["asset_path"], plan=[dict(op="unimplemented_verb")]))
