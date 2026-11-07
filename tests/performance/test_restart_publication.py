"""Restarting an editor must not re-export a clean workspace to publish one edit."""

from .fixtures import FANOUT, Project
from .measurement import locks

import json
import statistics
import time
import re
from pathlib import Path


def test_restart_keeps_historical_descriptions_but_measures_write_dependencies(tmp_path):
    project = Project(100, tmp_path)
    project.edit(project.materials[0])
    project.run("commit", all=True, message="one edit")
    project.ue.epoch = "restarted"
    exports, applies = project.counted()
    result = project.run("push")
    assert result["status"] == "published", result
    assert project.counted() == (exports + 1 + FANOUT, applies + 1)


def test_restart_performance_matched_workloads(tmp_path, monkeypatch, record_testsuite_property):
    from ue_node_nexus_mcp.transcode.collaboration.apply import observe

    project = Project(1000, tmp_path)
    implementation = observe.remembered
    measured = {}
    old, new = "BLEND_Translucent", "BLEND_Opaque"
    for mode in ("before", "after"):
        samples = []
        exports = []
        held = []

        def remembered(store, context, epoch):
            record = store.record("memory", "observed")
            if mode == "before" and record and record["editor_epoch"] != epoch:
                return {}
            return implementation(store, context, epoch)

        monkeypatch.setattr(observe, "remembered", remembered)
        for iteration in range(4):
            path = Path(project.workspace["file_paths"][project.materials[0]])
            text = re.sub(r"(?m)^BlendMode\s*=.*\n", "", path.read_text(encoding="utf-8"))
            path.write_text(text.replace("[asset]\n", f"[asset]\nBlendMode = {new}\n"), encoding="utf-8")
            old, new = new, old
            project.run("commit", all=True, message="matched restart workload")
            project.ue.epoch = f"{mode}-{iteration}"
            before = project.counted()[0]
            start = time.perf_counter()
            with locks() as records:
                result = project.run("push")
            assert result["status"] == "published", result
            count = project.counted()[0] - before
            assert count == (1000 if mode == "before" else 1 + FANOUT)
            if iteration:
                samples.append(time.perf_counter() - start)
                exports.append(count)
                held.extend(record[2] for record in records if record[0] == "publication.lock")
        measured[mode] = dict(samples=samples, mean=statistics.mean(samples), stdev=statistics.stdev(samples),
                              minimum=min(samples), maximum=max(samples), exports=exports, lock_seconds=held)
    result = dict(workload=dict(assets=1000, edited=1, dependencies=FANOUT, warmup=1, repetitions=3), measured=measured)
    record_testsuite_property("restart_publication", json.dumps(result))
    print(json.dumps(result))
