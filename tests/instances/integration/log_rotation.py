"""Four installed-package processes rotate independent logs in one shared directory."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import logging
import os
from pathlib import Path
import subprocess
import sys

from ue_node_nexus_mcp.diagnostics.logging import configure_logging
from ue_node_nexus_mcp.diagnostics.sessions import MAX_BYTES, RESERVED_BYTES, TOTAL_BYTES

ROOT = Path(__file__).resolve().parents[3]


def worker(index):
    directory = configure_logging()
    logger = logging.getLogger("ue_node_nexus_mcp.rotation_acceptance")
    for sequence in range(6000):
        logger.info("writer=%s sequence=%s %s", index, sequence, "x" * 1200)
    logger.info("writer=%s completed", index)
    logging.shutdown()
    print(json.dumps(dict(index=index, pid=os.getpid(), directory=str(directory))), flush=True)


def child(host, index):
    command = [sys.executable, "-m", "tests.instances.integration.log_rotation", "--worker", str(index)]
    with (host / (str(index) + ".stderr.log")).open("wb") as errors:
        result = subprocess.run(command, cwd=str(ROOT), env=dict(os.environ, UE_NEXUS_LOG_DIR=str(host / "Logs")),
                                stdout=subprocess.PIPE, stderr=errors, timeout=90, check=True)
    return json.loads(result.stdout)


def run(name):
    host = (ROOT / "build" / name).resolve()
    host.relative_to((ROOT / "build").resolve())
    host.mkdir()
    with ThreadPoolExecutor(max_workers=4) as pool:
        clients = list(pool.map(lambda index: child(host, index), range(4)))
    directory = host / "Logs/Sessions"
    groups = []
    for client in clients:
        logs = list(directory.glob("session-" + str(client["pid"]) + "-*.log"))
        assert len(logs) == 1, (client, list(directory.iterdir()))
        current = logs[0]
        files = sorted(directory.glob(current.name + "*"))
        assert len(files) == 4, files
        assert all(path.stat().st_size <= MAX_BYTES for path in files), files
        assert "writer=" + str(client["index"]) + " completed" in current.read_text(encoding="utf-8")
        total = sum(path.stat().st_size for path in files)
        assert total <= RESERVED_BYTES
        groups.append(dict(pid=client["pid"], files=[str(path) for path in files], bytes=total))
    assert len(set(group["files"][0] for group in groups)) == len(clients)
    total = sum(group["bytes"] for group in groups)
    assert total <= TOTAL_BYTES
    report = dict(ok=True, python=sys.executable, clients=clients, groups=groups, total_log_bytes=total,
                  reserved_per_client=RESERVED_BYTES, global_budget=TOTAL_BYTES)
    (host / "log-rotation-result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(dict(ok=True, processes=len(clients), total_log_bytes=total)), flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name")
    parser.add_argument("--worker", type=int)
    args = parser.parse_args()
    if args.worker is not None:
        worker(args.worker)
    elif args.name:
        run(args.name)
    else:
        parser.error("--name is required")
