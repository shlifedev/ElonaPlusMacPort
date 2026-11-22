import sys
from pathlib import Path
import struct
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from elona_extract import ExtractError
from normalize_legacy_ax import normalize


def fixture():
    data = bytearray(106)
    struct.pack_into('<4s23i', data, 0, b'HSP3', 0x301, 1, len(data),
                     96, 4, 100, 2, 102, 4, 106, 0, 106, 0,
                     106, 0, 106, 0, 106, 0, 106, 0, 0, 0)
    data[96:102] = b'\x11\x20\x00\x00A\0'
    struct.pack_into('<i', data, 102, 0)
    return data


class NormalizeTests(unittest.TestCase):
    def test_payload_and_relative_label_are_unchanged(self):
        source = fixture()
        result, report = normalize(source)
        self.assertEqual(result[112:], source[96:])
        self.assertEqual(result[96:112], bytes(16))
        self.assertEqual(struct.unpack_from('<I', result, 16)[0], 112)
        self.assertEqual(struct.unpack_from('<I', result, 12)[0], len(result))
        self.assertEqual(struct.unpack_from('<I', result, 80)[0], len(result))
        self.assertTrue(report['payload_unchanged'])

    def test_deterministic(self):
        self.assertEqual(normalize(fixture()), normalize(fixture()))

    def test_reject_current_header(self):
        output, _ = normalize(fixture())
        with self.assertRaises(ExtractError):
            normalize(output)

    def test_reject_truncated(self):
        with self.assertRaises(ExtractError):
            normalize(fixture()[:-1])

    def test_reject_runtime_option(self):
        source = fixture()
        struct.pack_into('<I', source, 92, 96)
        with self.assertRaisesRegex(ExtractError, 'runtime-name'):
            normalize(source)

    def test_reject_empty_pointer_inside_header(self):
        source = fixture()
        struct.pack_into('<I', source, 40, 12)
        with self.assertRaisesRegex(ExtractError, 'inside'):
            normalize(source)

    def test_reject_invalid_code_range(self):
        source = fixture()
        struct.pack_into('<I', source, 20, 0x7ffffffe)
        with self.assertRaises(ExtractError):
            normalize(source)

    def test_zero_unused_pointer_is_preserved(self):
        source = fixture()
        struct.pack_into('<I', source, 72, 0)
        output, _ = normalize(source)
        self.assertEqual(struct.unpack_from('<I', output, 72)[0], 0)


if __name__ == '__main__':
    unittest.main()
