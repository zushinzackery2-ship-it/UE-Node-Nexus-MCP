"""Private validation must verify the same DLL selected by Unreal."""

import json
from pathlib import Path
import tempfile
import unittest

from ue_node_nexus_mcp.safety.isolation.builds import module_binary
from ue_node_nexus_mcp.instances.errors import InstanceError


class ModuleManifest(unittest.TestCase):
    def test_versioned_binary_is_selected_instead_of_old_mapped_binary(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binaries = root / "Binaries/Win64"
            binaries.mkdir(parents=True)
            old = binaries / "UnrealEditor-UeNodeNexusVfxBridge.dll"
            new = binaries / "UnrealEditor-UeNodeNexusVfxBridge-0944f0cd.dll"
            old.write_bytes(b"old")
            new.write_bytes(b"new")
            data = dict(BuildId="37670630", Modules=dict(UeNodeNexusVfxBridge=new.name))
            (binaries / "UnrealEditor.modules").write_text(json.dumps(data))
            self.assertEqual(module_binary(root, "UeNodeNexusVfxBridge"), new)
            self.assertEqual(old.read_bytes(), b"old")

    def test_manifest_cannot_select_an_external_path(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binaries = root / "Binaries/Win64"
            binaries.mkdir(parents=True)
            data = dict(Modules=dict(UeNodeNexusVfxBridge="../foreign.dll"))
            (binaries / "UnrealEditor.modules").write_text(json.dumps(data))
            with self.assertRaises(InstanceError):
                module_binary(root, "UeNodeNexusVfxBridge")


if __name__ == "__main__":
    unittest.main()
