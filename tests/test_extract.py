"""Synthetic fixtures exercise safety without distributing game bytes."""

from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from elona_extract import ExtractError, decrypt_ax, extract, inspect_ax, read_dpm, write_outputs


def fixture_ax() -> bytes:
    parts = [b"\x00\x00\x00\x00", b"test.dll\0_Test@16\0entry\0", struct.pack("<i", 0), b"",
             struct.pack("<4i", 1, 0, 0, 0),
             struct.pack("<hh6i", 0, -5, 0, 1, 9, 4, 2, 0)
             + struct.pack("<hh6i", -1, 1, 1, 0, 18, 0, 0, 0),
             struct.pack("<hhi", 4, -5, 0), b""]
    position = 96
    ranges = []
    for data in parts:
        ranges.extend((position, len(data)))
        position += len(data)
    values = [0x301, 1, position, *ranges, position, 0, 0, 0]
    return struct.pack("<4s23i", b"HSP3", *values) + b"".join(parts)


def encrypt_fixture(data: bytes, xor_first: bool = True) -> bytes:
    previous = 0
    result = bytearray()
    for value in data:
        delta = (value - previous) & 255
        result.append(((delta ^ 211) + 33) & 255 if xor_first else ((delta + 19) & 255) ^ 67)
        previous = value
    return bytes(result)


def fixture_pe(members=None) -> bytes:
    if members is None:
        members = [(b"scene1.hsp", b"scene data", 0), (b"start.ax", encrypt_fixture(fixture_ax()), 1)]
    pe = bytearray(0x300)
    pe[:2] = b"MZ"
    struct.pack_into("<I", pe, 0x3C, 0x80)
    struct.pack_into("<4sHHIIIHH", pe, 0x80, b"PE\0\0", 0x14C, 1, 0, 0, 0, 0xE0, 0)
    struct.pack_into("<H", pe, 0x98, 0x10B)
    struct.pack_into("<II", pe, 0x178 + 16, 0x100, 0x200)
    directory = bytearray(struct.pack("<4sIII", b"DPMX", 16 + 32 * len(members), len(members), 0))
    payload = bytearray()
    for name, data, key in members:
        directory.extend(struct.pack("<16sIIII", name, 0xFFFFFFFF, key, len(payload), len(data)))
        payload.extend(data)
    return bytes(pe + directory + payload)


class ExtractionTests(unittest.TestCase):
    def test_fixture_extraction_and_declarations(self):
        files, manifest = extract(fixture_pe(), require_baseline=False)
        self.assertEqual(files["start.ax"], fixture_ax())
        self.assertEqual(files["scene1.hsp"], b"scene data")
        self.assertEqual(manifest["summary"]["external_function_count"], 1)
        inventory = inspect_ax(files["start.ax"])
        self.assertEqual(inventory["libraries"][0]["name"], "test.dll")
        self.assertEqual(inventory["functions"][0]["name"], "_Test@16")
        self.assertEqual(inventory["functions"][1]["name"], "entry")
        self.assertEqual(inventory["header_bytes"], 96)

    def test_both_obfuscation_orders(self):
        for xor_first in (False, True):
            self.assertEqual(decrypt_ax(encrypt_fixture(fixture_ax(), xor_first)), fixture_ax())

    def test_input_identity_is_required_by_default(self):
        with self.assertRaisesRegex(ExtractError, "pinned"):
            extract(fixture_pe())

    def test_truncated_pe_and_dpm_are_rejected(self):
        original = fixture_pe()
        for size in (0, 2, 0x3F, 0x90, 0x180, 0x307, len(original) - 1):
            with self.subTest(size=size), self.assertRaises(ExtractError):
                read_dpm(original[:size])

    def test_unsafe_names_are_rejected(self):
        for name in (b"../evil", b"..\\evil", b"/tmp/x", b"C:evil", b"CON", b"NUL.txt", b"a.", b".hidden"):
            with self.subTest(name=name), self.assertRaises(ExtractError):
                read_dpm(fixture_pe([(name, b"x", 0)]))

    def test_case_insensitive_duplicate_names_are_rejected(self):
        with self.assertRaisesRegex(ExtractError, "duplicate"):
            read_dpm(fixture_pe([(b"a.hsp", b"a", 0), (b"A.HSP", b"b", 0)]))

    def test_member_overlap_is_rejected(self):
        data = bytearray(fixture_pe())
        struct.pack_into("<I", data, 0x300 + 16 + 32 + 24, 0)
        with self.assertRaisesRegex(ExtractError, "overlap"):
            read_dpm(data)

    def test_member_out_of_bounds_is_rejected(self):
        data = bytearray(fixture_pe())
        struct.pack_into("<I", data, 0x300 + 16 + 24, 0xFFFFFFFF)
        with self.assertRaisesRegex(ExtractError, "range"):
            read_dpm(data)

    def test_pe_section_overlapping_headers_is_rejected(self):
        data = bytearray(fixture_pe())
        struct.pack_into("<I", data, 0x178 + 20, 0x80)
        with self.assertRaisesRegex(ExtractError, "overlaps headers"):
            read_dpm(data)

    def test_unexplained_trailing_bytes_are_rejected(self):
        with self.assertRaisesRegex(ExtractError, "unexplained"):
            read_dpm(fixture_pe() + b"extra")

    def test_directory_layout_mismatch_is_rejected(self):
        data = bytearray(fixture_pe())
        struct.pack_into("<I", data, 0x304, 0)
        with self.assertRaisesRegex(ExtractError, "layout"):
            read_dpm(data)

    def test_header_size_and_segment_overlap_are_rejected(self):
        for offset, value, message in ((12, 1, "size mismatch"), (24, 96, "overlap"), (16, 112, "96-byte")):
            data = bytearray(fixture_ax())
            struct.pack_into("<i", data, offset, value)
            with self.subTest(offset=offset), self.assertRaisesRegex(ExtractError, message):
                inspect_ax(data)

    def test_segment_size_misalignment_is_rejected(self):
        data = bytearray(fixture_ax())
        struct.pack_into("<i", data, 60, 55)
        with self.assertRaisesRegex(ExtractError, "alignment"):
            inspect_ax(data)

    def test_invalid_function_parameter_and_name_references(self):
        original = fixture_ax()
        functions, = struct.unpack_from("<i", original, 56)
        for relative, value, message in ((4, 1000, "parameter"), (12, 999999, "name offset")):
            data = bytearray(original)
            struct.pack_into("<i", data, functions + relative, value)
            with self.subTest(relative=relative), self.assertRaisesRegex(ExtractError, message):
                inspect_ax(data)

    def test_invalid_label_target_is_rejected(self):
        data = bytearray(fixture_ax())
        labels, = struct.unpack_from("<i", data, 32)
        struct.pack_into("<i", data, labels, 1000)
        with self.assertRaisesRegex(ExtractError, "label target"):
            inspect_ax(data)

    def test_output_report_name_collision_is_rejected(self):
        with self.assertRaisesRegex(ExtractError, "conflicts"):
            extract(fixture_pe([(b"manifest.json", b"x", 0)]), require_baseline=False)

    def test_missing_ax_is_rejected(self):
        with self.assertRaisesRegex(ExtractError, "no start.ax"):
            extract(fixture_pe([(b"scene.hsp", b"x", 0)]), require_baseline=False)


class OutputTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.original = self.root / "original"
        self.original.mkdir()
        self.output = self.root / "out"
        self.files = {"start.ax": b"fixture", "manifest.json": b"{}"}

    def test_new_output_and_identical_rerun(self):
        self.assertTrue(write_outputs(self.output, self.files, original_dir=self.original))
        timestamp = (self.output / "start.ax").stat().st_mtime_ns
        self.assertFalse(write_outputs(self.output, self.files, original_dir=self.original))
        self.assertEqual((self.output / "start.ax").stat().st_mtime_ns, timestamp)

    def test_changed_or_foreign_content_is_never_overwritten(self):
        write_outputs(self.output, self.files, original_dir=self.original)
        (self.output / "start.ax").write_bytes(b"user edit")
        with self.assertRaisesRegex(ExtractError, "differs"):
            write_outputs(self.output, self.files, original_dir=self.original)
        self.assertEqual((self.output / "start.ax").read_bytes(), b"user edit")
        (self.output / "extra").write_bytes(b"keep")
        with self.assertRaisesRegex(ExtractError, "unexpected contents"):
            write_outputs(self.output, self.files, original_dir=self.original)

    def test_input_and_output_overlap_rejected(self):
        for output in (self.original, self.original / "nested", self.root):
            with self.subTest(output=output), self.assertRaisesRegex(ExtractError, "separate"):
                write_outputs(output, self.files, original_dir=self.original)

    def test_symlink_destination_and_members_rejected(self):
        self.output.symlink_to(self.original, target_is_directory=True)
        with self.assertRaisesRegex(ExtractError, "symlink"):
            write_outputs(self.output, self.files, original_dir=self.original)
        self.output.unlink()
        write_outputs(self.output, self.files, original_dir=self.original)
        (self.output / "start.ax").unlink()
        fixture = self.root / "outside"
        fixture.write_bytes(b"fixture")
        (self.output / "start.ax").symlink_to(fixture)
        with self.assertRaisesRegex(ExtractError, "differs"):
            write_outputs(self.output, self.files, original_dir=self.original)

    def test_direct_writer_rejects_traversal(self):
        with self.assertRaises(ExtractError):
            write_outputs(self.output, {"../escape": b"x"}, original_dir=self.original)
        self.assertFalse((self.root / "escape").exists())


if __name__ == "__main__":
    unittest.main()
