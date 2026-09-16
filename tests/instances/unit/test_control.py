import os
import uuid

import pytest

from ue_node_nexus_mcp.coordination.pipe_server import PipeServer
from ue_node_nexus_mcp.transport import named_pipe_transport


@pytest.mark.skipif(os.name != "nt", reason="Windows named pipes")
def test_control_framing_peer_identity_and_clean_shutdown():
    address = "\\\\.\\pipe\\NexusTest." + uuid.uuid4().hex
    server = PipeServer(address, lambda request, peer: dict(ok=True, request=request, peer=peer)).start()
    try:
        for number in range(20):
            result = named_pipe_transport.send(address, dict(number=number), 2)
            assert result == dict(ok=True, request=dict(number=number), peer=os.getpid())
    finally:
        server.close()
    assert not any(thread.is_alive() for thread in server.threads)
