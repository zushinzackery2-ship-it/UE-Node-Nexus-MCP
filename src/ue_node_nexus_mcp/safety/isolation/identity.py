"""Compare source and worker identities before paying for a validation copy."""

from pathlib import Path

from ...instances.errors import require
from ...instances.lifecycle.engine import resolve_engine

FIELDS = ("source_fingerprint", "source_commit", "version", "contract_version", "source_dirty")


def verify(builds, expected, *, phase):
    for name, recorded in expected.items():
        actual = builds.get(name, {})
        for field in FIELDS:
            require(actual.get(field) == recorded.get(field), "isolation_identity_mismatch",
                    "loaded plugin differs from its recorded build input", phase=phase, plugin=name,
                    field=field, expected=recorded.get(field), actual=actual.get(field))


def source(project, capabilities):
    from .session import expected_builds

    engine = resolve_engine(project, None)
    verify(capabilities.get("build", {}), expected_builds(Path(project), engine), phase="source_preflight")
