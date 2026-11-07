"""Reduce a replay while preserving its structured failure codes and baseline."""

import argparse
import json
from pathlib import Path
import subprocess
import sys


def signature(report):
    found = set()

    def visit(value):
        if isinstance(value, dict):
            if value.get("code") or value.get("conflict_type"):
                asset = str(value.get("asset", "")).rsplit("/", 1)[-1]
                found.add(json.dumps(dict(code=value.get("code", value.get("conflict_type")),
                    asset=asset, stage=value.get("stage"), operation=value.get("operation"), field=value.get("field"),
                    test=value.get("test"), messages=value.get("messages")), sort_keys=True))
            for key, item in value.items():
                visit(item)
        elif isinstance(value, list):
            for item in value:
                visit(item)
        elif isinstance(value, str) and value.startswith(("{", "[")):
            try:
                visit(json.loads(value))
            except ValueError:
                pass

    visit(report.get("error", {}))
    return tuple(sorted(found))


def reduce_actions(actions, reproduces, attempts=12):
    current = list(actions)
    chunk = max(1, (len(current) - 1) // 2)
    count = 0
    while chunk and count < attempts:
        reduced = False
        for start in range(1, len(current), chunk):
            candidate = current[:start] + current[start + chunk:]
            if len(candidate) < 2:
                continue
            count += 1
            if reproduces(candidate):
                current = candidate
                reduced = True
                break
            if count >= attempts:
                break
        if not reduced:
            chunk //= 2
    return current, count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("report", type=Path)
    parser.add_argument("--attempts", type=int, default=12)
    parser.add_argument("--installed", action="store_true")
    args = parser.parse_args()
    trace = json.loads(args.trace.read_text(encoding="utf-8"))
    baseline_report = json.loads(args.report.read_text(encoding="utf-8"))
    expected = signature(baseline_report)
    if not expected:
        parser.error("the failure needs structured codes; a generic process error is not a shrink oracle")
    directory = args.trace.parent / "Shrink"
    directory.mkdir(exist_ok=True)
    runs = []

    def reproduces(actions):
        candidate = directory / f"candidate-{len(runs)}.json"
        candidate.write_text(json.dumps(dict(trace, actions=actions), indent=2), encoding="utf-8")
        command = [sys.executable, "-m", "tests.stress", "--replay", str(candidate), "--skip-native",
                   "--rhi", baseline_report["rhi"]]
        if args.installed or baseline_report.get("installed"):
            command.append("--installed")
        completed = subprocess.run(command, capture_output=True, text=True, timeout=1800)
        lines = [line for line in completed.stdout.splitlines() if line.startswith('{"ok":')]
        if not lines:
            raise RuntimeError("replay did not produce its durable report")
        path = Path(json.loads(lines[-1])["report"])
        actual = signature(json.loads(path.read_text(encoding="utf-8")))
        matches = actual == expected
        runs.append(dict(actions=len(actions), report=str(path), signature=actual, matches=matches))
        (directory / "attempts.json").write_text(json.dumps(runs, indent=2), encoding="utf-8")
        return matches

    if not reproduces(trace["actions"]):
        raise RuntimeError("original failure did not reproduce; keep its complete evidence")
    actions, count = reduce_actions(trace["actions"], reproduces, args.attempts)
    target = directory / "minimal-trace.json"
    target.write_text(json.dumps(dict(trace, actions=actions), indent=2), encoding="utf-8")
    print(json.dumps(dict(trace=str(target), attempts=count, original=len(trace["actions"]), reduced=len(actions))))


if __name__ == "__main__":
    main()
