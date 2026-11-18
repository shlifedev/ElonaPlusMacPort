"""Verify that the opt-in display transform cannot change unrelated game bytes."""
import importlib.util
import sys
from contextlib import redirect_stderr
from io import StringIO
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))
spec = importlib.util.spec_from_file_location("prepare_korean_game", ROOT / "tools/prepare_korean_game.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


import play_game


class LauncherTests(unittest.TestCase):
    def test_prepared_and_legacy_workspaces(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            original = root / "work/normalized/2.335/start.ax"
            managed = root / play_game.MANAGED_AX
            with redirect_stderr(StringIO()) as message:
                self.assertEqual(play_game.game_ax(root, "ko"), original)
            self.assertIn("Korean paragraph AX missing", message.getvalue())
            legacy = root / play_game.LEGACY_MANAGED_AX
            legacy.parent.mkdir(parents=True)
            legacy.write_bytes(b"legacy fixture")
            with patch.object(play_game, "LEGACY_MANAGED_SHA256", module.digest(b"legacy fixture")):
                with redirect_stderr(StringIO()):
                    self.assertEqual(play_game.game_ax(root, "ko"), legacy)
            with self.assertRaisesRegex(RuntimeError, "Unexpected Korean"):
                play_game.game_ax(root, "ko")
            managed.parent.mkdir(parents=True)
            managed.write_bytes(b"fixture")
            self.assertEqual(play_game.game_ax(root, "original"), original)
            with self.assertRaisesRegex(RuntimeError, "Unexpected Korean"):
                play_game.game_ax(root, "ko")
            with patch.object(play_game, "MANAGED_SHA256", module.digest(b"fixture")):
                self.assertEqual(play_game.game_ax(root, "ko"), managed)


class TransformTests(unittest.TestCase):
    def fixture(self):
        # 0x815c contains a CP932 trail backslash. A Unicode round trip is not
        # needed; the check concerns preservation of every opaque source byte.
        return (b';fixture\r\nmes "\x81\x5c"\n' + module.ANCHOR + module.LOG_ENTRY
                + b"\tnoteget buff2, p\r\n\t\tcs_list s(3), wx + 96, y - 1, 19\r\n\t\tpos wx + 20, y + 20\r\n\t\tmes buff\r\n"
                + b"\twy = winposy(468)\r\n\tpos wx, wy\r\n\tgcopy 4, 0, 0, 736, 448\r\n"
                + b"\t\tpos x, y\r\n\t\tmes s\r\n\t\tif ( p \\ 20 == 0 ) {\r\n\t\t\tfont lang(cfg_font1, cfg_font2), 12 + sizefix - en * 2, 1\r\n"
                + b'\t\t\tpos x + 90, y + 330\r\n\t\t\tmes "- " + (p / 20 + 1) + " -"\r\n\t\t}\r\n\tloop\r\n*label_5808\r\n'
                + b"\tdisplay_topic itemname(ci), wx + 28, wy + 34\r\n\tmes display_topic_prm0\r\n"
                + b"\t\ttx@m72 = limit(strlen(cs_list_prm0) * 7 + 32 + cs_list_prm4, 10, 480)\r\n"
                + b"\tgcopy 3, 128, 288, 128, 32\r\n" * 3
                + module.DIALOG_DRAW + b"\ttalk_conv buff, 56 - en * 3\r\n"
                + b"\t\tcs_list s, wx + 84, wy + 60 + cnt * 19 - 1, 19, 0, 1, p\r\n"
                + b"\t\tpos wx + 600 - strlen(s(1)) * 7, wy + 60 + cnt * 19 + 2\r\n"
                + b"\t\tcs_list s, wx + 140 - 26, wy + 60 + cnt * 19 - 1, 19, 30, p(1) != (-1), p(1)\r\n"
                + b"".join(line * count for line, count in module.HUD_LINES) + b"\treturn\r\n")

    def transform_fixture(self, data):
        with patch.object(module, "SOURCE_SHA256", module.digest(data)), patch.object(module, "dialogue_calls", return_value=[]), \
             patch.object(module, "creation_inserts", return_value=[]), \
             patch.object(module, "option_inserts", return_value=[]), \
             patch.object(module, "menu_inserts", return_value=[]), \
             patch.object(module, "story_inserts", return_value=[]), \
             patch.object(module, "static_dialogue_inserts", return_value=[]):
            return module.transform(data)

    def test_removing_inserted_spans_recovers_every_original_byte(self):
        source = self.fixture()
        output, changes = self.transform_fixture(source)
        body = bytearray(output)
        for change in reversed(changes):
            at = change["output_byte_offset"]
            del body[at:at + change["inserted_bytes"]]
        self.assertEqual(bytes(body), source)
        self.assertEqual(output.count(b"native_ko_gmes"), 4)
        self.assertIn(b"\t\treturn 0\r\n", output)

    def test_unknown_source_hash_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            module.transform(self.fixture())

    def test_missing_duplicate_or_changed_anchor_is_rejected(self):
        for source in (b"no function", self.fixture() + module.ANCHOR,
                       self.fixture().replace(b'\tm@m111 = ""', b'\tm@m111 = "changed"')):
            with self.subTest(source=source), self.assertRaisesRegex(ValueError, "anchor"):
                self.transform_fixture(source)

    def test_existing_native_declaration_is_rejected(self):
        for declaration in (b"#regcmd 49\r\n", b"native_ko_gmes\r\n"):
            with self.assertRaisesRegex(ValueError, "already declares"):
                self.transform_fixture(declaration + self.fixture())

    def test_output_cannot_escape_work_or_overwrite_existing(self):
        with self.assertRaisesRegex(ValueError, "below work"):
            module.check_output_path(ROOT / "localization/generated-game")
        # A fresh checkout has no work/ yet; make a throwaway directory below it.
        (ROOT / "work").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=ROOT / "work") as existing:
            with self.assertRaisesRegex(ValueError, "already exists"):
                module.check_output_path(Path(existing))

    def test_output_symlink_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            link = Path(tmp) / "link"
            link.symlink_to(ROOT / "work", target_is_directory=True)
            with self.assertRaisesRegex(ValueError, "symlinks"):
                module.check_output_path(link / "new")

    @staticmethod
    def object(code):
        header = bytearray(112)
        struct.pack_into("<4s5I", header, 0, b"HSP3", 0x360, 0, 112 + len(code), 112, len(code))
        return bytes(header) + code

    def test_counts_opcode_tokens_not_byte_patterns_in_values(self):
        code = bytes((48, 0x20, 0, 0, 4, 0x80, 48, 0, 0, 0, 11, 0x20, 0, 0, 2, 0))
        result = module.command_counts(self.object(code))
        self.assertEqual(result["type48_commands"], {"0": 1})
        self.assertEqual(result["total_tokens"], 3)

    def test_truncated_or_bad_token_stream_is_rejected(self):
        for code in (bytes((48, 0x80, 0, 0)), bytes((48, 1, 0, 0)), bytes((11, 0x20, 0, 0))):
            with self.subTest(code=code), self.assertRaises(ValueError):
                module.command_counts(self.object(code))


if __name__ == "__main__":
    unittest.main()
