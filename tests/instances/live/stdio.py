"""Real MCP sessions in separate working directories for installed-artifact tests."""

from contextlib import asynccontextmanager
import json
import os
import sys

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


class Peer:
    def __init__(self, client, workspace):
        self.client, self.workspace = client, workspace
        self.records = []

    async def call(self, tool, arguments, allow_error=False):
        response = await self.client.call_tool(tool, arguments)
        raw = next(item.text for item in response.content if item.type == "text")
        try:
            result = json.loads(raw)
        except json.JSONDecodeError:
            self.records.append(dict(tool=tool, arguments=arguments, unexpected_response=raw))
            raise AssertionError(dict(tool=tool, unexpected_response=raw))
        self.records.append(dict(tool=tool, arguments=arguments, result=result))
        assert allow_error or result.get("ok"), result
        return result

    async def execute(self, operation, allow_error=False, **payload):
        result = await self.call("ue_execute", dict(operation=operation, payload=payload, response=dict(mode="full")), allow_error)
        return result if allow_error else result["data"]

    async def sync(self, action, workspace=None, paths=None, allow_error=False, **options):
        if action != "init":
            options.setdefault("dry_run", False)
        if workspace:
            options["workspace_id"] = workspace["id"]
        result = await self.call("ue_sync", dict(action=action, paths=paths, options=options), allow_error)
        data = result.get("data", dict())
        if data.get("artifact") and data.get("next_read"):
            request = data["next_read"]
            result = (await self.call(request["tool"], request["args"]))["data"]
        return result.get("data", result)


@asynccontextmanager
async def connect(project, root, workspace, module="ue_node_nexus_mcp.server", extra_env=None):
    workspace.mkdir(parents=True, exist_ok=True)
    (workspace / "Content_Transcoded").mkdir(exist_ok=True)
    parameters = StdioServerParameters(command=sys.executable,
        args=["-m", module, "--project", str(project)], cwd=str(workspace),
        env=dict(dict(os.environ, UE_NEXUS_RUNTIME_DIR=str(root), UE_NEXUS_LOG_DIR=str(root / "ClientLogs")), **(extra_env or dict())))
    peer = None
    try:
        with (workspace / "stderr.log").open("a", encoding="utf-8") as stderr:
            async with stdio_client(parameters, errlog=stderr) as streams:
                async with ClientSession(*streams) as client:
                    await client.initialize()
                    peer = Peer(client, workspace)
                    yield peer
    finally:
        if peer:
            (workspace / "requests.json").write_text(json.dumps(peer.records, ensure_ascii=False, indent=2), encoding="utf-8")
