"""Run bounded real-UE campaigns; --replay uses the recorded materialized actions."""

import argparse
import json
import os
from pathlib import Path
import shutil
import time

import ue_node_nexus_mcp
from tests.live.editor.prepare import stage_plugins
from tests.live.editor.session import EditorSession
from .native import run as run_native
from .sequences import generate
from .workflow import Campaign

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seeds", default="2711,2712,2713")
    parser.add_argument("--steps", type=int, default=16)
    parser.add_argument("--installed", action="store_true")
    parser.add_argument("--replay", type=Path)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--native-only", action="store_true")
    mode.add_argument("--skip-native", action="store_true")
    parser.add_argument("--rhi", choices=("d3d12", "d3d11"), default="d3d12")
    args = parser.parse_args()
    if not 1 <= args.steps <= 1000:
        parser.error("steps must be 1..1000")
    package = Path(ue_node_nexus_mcp.__file__).resolve()
    source = package.is_relative_to(ROOT / "src")
    if source == args.installed:
        parser.error("installed runs require installed Python imports; source runs require PYTHONPATH=src")
    engine = Path(os.environ.get("UE_NEXUS_ENGINE_DIR", "D:/Program Files/Epic Games/UE_5.5"))
    directory = ROOT / "build/stress" / (time.strftime("%Y%m%d-%H%M%S") + ("-installed" if args.installed else "-source"))
    project_root = directory / "Project"
    project_root.mkdir(parents=True)
    build = ROOT / "build/validation"
    if not args.installed:
        stage_plugins(build, project_root)
    project = project_root / "NexusValidation.uproject"
    shutil.copy2(build / project.name, project)
    native_project = None
    if not args.skip_native:
        # Recovery Automation intentionally binds private fixture repositories.
        # Keep that suite's project separate from the persistent authoring trace.
        native_root = directory / "NativeProject"
        native_root.mkdir()
        if not args.installed:
            stage_plugins(build, native_root)
        native_project = native_root / project.name
        shutil.copy2(build / project.name, native_project)
    traces = [json.loads(args.replay.read_text(encoding="utf-8"))] if args.replay else [
        dict(seed=int(seed), actions=generate(int(seed), args.steps)) for seed in args.seeds.split(",")]
    results = []
    report = dict(ok=False, python_package=str(package), installed=args.installed, rhi=args.rhi, results=results)
    plugin_root = engine / "Engine/Plugins/Marketplace" if args.installed else project_root / "Plugins"
    try:
        for index, trace in enumerate(traces):
            seed = trace["seed"]
            target = directory / str(seed)
            target.mkdir()
            (target / "trace.json").write_text(json.dumps(trace, indent=2), encoding="utf-8")
            row = dict(seed=seed)
            results.append(row)
            if args.native_only:
                with EditorSession(project, engine, f"Stress-{seed}-Identity", args.rhi, allow_low_memory=True) as session:
                    row["builds"] = session.verify_builds(plugin_root)
            if not args.skip_native:
                row["native"] = run_native(native_project, engine, target / "Native", seed, len(trace["actions"]), index == 0, args.rhi)
            if args.native_only:
                continue
            with EditorSession(project, engine, f"Stress-{seed}", args.rhi, allow_low_memory=True) as session:
                row["builds"] = session.verify_builds(plugin_root)
                campaign = Campaign(session, target, trace["actions"], seed)
                state = campaign.execute()
            with EditorSession(project, engine, f"Stress-{seed}-Cold", args.rhi, allow_low_memory=True) as session:
                session.verify_builds(plugin_root)
                row["reopened"] = Campaign(session, target / "Cold", [], seed, state).reopened()
            oracle = target / "oracle.json"
            oracle.write_text(json.dumps(dict(root=state["root"], action=trace["actions"][-1])), encoding="utf-8")
            row["cold_oracle"] = run_native(project, engine, target / "ColdOracle", seed, len(trace["actions"]),
                                             rhi=args.rhi, oracle=oracle)
            row["ok"] = True
        report["ok"] = True
    except BaseException as error:
        report["error"] = dict(type=type(error).__name__, message=str(error),
                               code=getattr(error, "code", None), details=getattr(error, "details", None))
        from ue_node_nexus_mcp.safety.isolation.process import stop_owned

        for file in sorted((project_root / "Logs").glob("Stress-*-exit.json")):
            exit_record = json.loads(file.read_text(encoding="utf-8"))
            if not exit_record.get("normal") and exit_record.get("instance", {}).get("pid"):
                report.setdefault("failed_editor_cleanup", []).append(stop_owned(exit_record["instance"], project))
        raise
    finally:
        (directory / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
        print(json.dumps(dict(ok=report["ok"], report=str(directory / "report.json"))), flush=True)


if __name__ == "__main__":
    main()
