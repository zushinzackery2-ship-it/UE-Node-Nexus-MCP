"""Bind complete worker execution to the exact submitted engine-facing unit."""

from __future__ import annotations

from ...transcode.errors import SyncError
from ...transcode.storage.io import digest


BUILD_FIELDS = ("source_fingerprint", "source_commit", "source_dirty", "version", "contract_version", "build_id")


def build_identity(builds: dict) -> dict:
    result = dict()
    for name in ("UeNodeNexusBridge", "UeNodeNexusGuard", "UeNodeNexusVfxBridge"):
        row = builds.get(name, dict())
        if not row.get("loaded") or any(row.get(key) is None for key in BUILD_FIELDS):
            raise SyncError("safety_identity_incomplete", "loaded module identity is incomplete", dict(module=name))
        result[name] = dict((key, row[key]) for key in BUILD_FIELDS)
    return result


def binding(item, candidate, observation, builds, rhi, environment):
    if not isinstance(environment, dict) or any(key not in environment for key in
            ("engine_version", "engine_build", "engine_modules", "project_definition", "configuration", "plugins", "content_mounts")):
        raise SyncError("safety_identity_incomplete", "publishing engine and plugin environment is incomplete")
    guarded = sorted(set((item["asset"],)) | set(item["dependencies"]))
    return dict(candidate=candidate, asset=item["asset"], kind=item["kind"], payload_digest=digest(item["payload"]),
                revisions=dict((asset, observation["revisions"].get(asset)) for asset in guarded),
                builds=build_identity(builds), rhi=rhi, environment=environment)


def verify(report: dict, expected: dict, operation: dict) -> dict:
    data = report.get("data", dict())
    cleanup = data.get("cleanup", dict())
    code = cleanup.get("exit_code", cleanup.get("instance", dict()).get("exit_code"))
    if not report.get("ok") or data.get("state") != "validated" or not data.get("source_unchanged"):
        raise SyncError("safety_validation_failed", "candidate failed complete isolated validation", dict(validation=report))
    if not cleanup.get("exit_confirmed") or cleanup.get("forced") or code != 0:
        raise SyncError("safety_worker_exit_failed", "candidate requires normal worker exit", dict(cleanup=cleanup))
    if data.get("operations_digest") != digest([operation]):
        raise SyncError("safety_candidate_changed", "validation names a different operation")
    builds = data.get("worker", dict()).get("build", dict())
    if build_identity(builds) != expected["builds"] or data.get("worker", dict()).get("rhi") != expected["rhi"]:
        raise SyncError("safety_identity_mismatch", "validation worker differs from the publishing editor")
    if data.get("worker", dict()).get("environment") != expected["environment"]:
        raise SyncError("safety_identity_mismatch", "validation engine, enabled plugins or configuration differs")
    results = data.get("results", [])
    if len(results) != 1 or not results[0].get("ok"):
        raise SyncError("safety_validation_incomplete", "validation must return the candidate's successful result")
    publication = results[0].get("data", dict()).get("isolated_publication", dict())
    if not all(publication.get(key) for key in ("compiled", "saved", "readback_verified")):
        raise SyncError("safety_validation_incomplete", "validation omitted compile, save or exported readback")
    return publication
