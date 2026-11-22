import importlib.util
from pathlib import Path
import tempfile
import unittest

MODULE = Path(__file__).resolve().parents[1] / "scripts/fix_decompiled_control_flow.py"
spec = importlib.util.spec_from_file_location("flow_transform", MODULE)
flow = importlib.util.module_from_spec(spec)
spec.loader.exec_module(flow)


class PublicationSafety(unittest.TestCase):
    def test_same_two_files_are_verified_without_replacing(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "out"
            files = {"start.hsp": b"end\n", "start.manifest.json": b"{}\n"}
            flow.publish(path, files)
            inode = (path / "start.hsp").stat().st_ino
            flow.publish(path, files)
            self.assertEqual((path / "start.hsp").stat().st_ino, inode)

    def test_manifest_symlink_never_changes_its_target(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            target = root / "original"
            target.write_bytes(b"preserve")
            out = root / "out"
            out.mkdir()
            (out / "start.hsp").write_bytes(b"end\n")
            (out / "start.manifest.json").symlink_to(target)
            with self.assertRaises(ValueError):
                flow.publish(out, {"start.hsp": b"end\n", "start.manifest.json": b"changed"})
            self.assertEqual(target.read_bytes(), b"preserve")

    def test_existing_changed_manifest_is_preserved(self):
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp) / "out"
            flow.publish(out, {"start.hsp": b"end\n", "start.manifest.json": b"original"})
            with self.assertRaises(ValueError):
                flow.publish(out, {"start.hsp": b"end\n", "start.manifest.json": b"changed"})
            self.assertEqual((out / "start.manifest.json").read_bytes(), b"original")

    def test_unpinned_source_is_rejected(self):
        with self.assertRaises(ValueError):
            flow.transform(b"end\n")


if __name__ == "__main__":
    unittest.main()
