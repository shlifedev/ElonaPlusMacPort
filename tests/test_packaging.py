"""The packaged game data pins must agree with the launchers that use them."""
import json
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import play_game  # noqa: E402

MANIFEST = json.loads((ROOT / "src/launcher/packaging/game-data.json").read_text(encoding="utf-8"))


class PackagingPins(unittest.TestCase):
    def test_korean_ax_matches_dev_launcher(self):
        pins = {f["name"]: f for f in MANIFEST["files"]}
        self.assertEqual(pins["start-ko.ax"]["sha256"], play_game.MANAGED_SHA256)
        self.assertEqual(pins["start-ko.ax"]["source"], play_game.MANAGED_AX.as_posix())

    def test_tauri_launcher_checks_the_same_original_exe(self):
        source = (ROOT / "src/launcher/src/main.rs").read_text(encoding="utf-8")
        self.assertEqual(re.search(r'EXE_SHA256: &str = "([0-9a-f]{64})"', source).group(1),
                         MANIFEST["original_exe_sha256"])

    def test_launcher_reads_every_bundled_file(self):
        source = (ROOT / "src/launcher/src/main.rs").read_text(encoding="utf-8")
        for name in ("start.ax", "start-ko.ax"):
            self.assertIn(f'"{name}"', source)
        self.assertEqual(sorted(f["name"] for f in MANIFEST["files"]),
                         ["scene1.hsp", "scene2.hsp", "start-ko.ax", "start.ax"])


if __name__ == "__main__":
    unittest.main()
