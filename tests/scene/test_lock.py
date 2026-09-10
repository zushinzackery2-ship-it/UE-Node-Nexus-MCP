import multiprocessing

import pytest

from ue_node_nexus_mcp.transcode.sync_project import SyncError
from ue_node_nexus_mcp.transcode.transaction.lock import MirrorLock


def hold_lock(root, ready, release):
    with MirrorLock(root):
        ready.set()
        release.wait(10)


def test_second_transaction_is_rejected_and_lock_is_released(tmp_path):
    with MirrorLock(tmp_path):
        with pytest.raises(SyncError) as error:
            with MirrorLock(tmp_path):
                pass
        assert error.value.code == "sync_busy"
    with MirrorLock(tmp_path):
        assert (tmp_path / ".nexus/sync.lock").is_file()


def test_separate_server_processes_share_the_lock(tmp_path):
    context = multiprocessing.get_context("spawn")
    ready, release = context.Event(), context.Event()
    process = context.Process(target=hold_lock, args=(tmp_path, ready, release))
    process.start()
    try:
        assert ready.wait(5)
        with pytest.raises(SyncError) as error:
            with MirrorLock(tmp_path):
                pass
        assert error.value.code == "sync_busy"
    finally:
        release.set()
        process.join(10)
        if process.is_alive():
            process.terminate()
            process.join()
    assert process.exitcode == 0
