from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from compare_ax_roundtrip import Object


def token(kind, value, flags=0):
    return bytes([kind, flags]) + struct.pack("<i" if flags & 128 else "<H", value)


def object_bytes(code, data=b""):
    end = 96 + len(code) + len(data)
    words = [0] * 23
    words[0:3] = [0x301, 0, end]
    words[3:7] = [96, len(code), 96 + len(code), len(data)]
    for i in range(7, 19, 2):
        words[i] = end
    words[19] = end
    return struct.pack("<4s23I", b"HSP3", *words) + code + data


class RoundtripComparison(unittest.TestCase):
    def normalize(self, raw):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "fixture.ax"
            path.write_bytes(raw)
            return [normalized for normalized, _ in Object(path).normalized()]

    def test_storage_width_does_not_change_value(self):
        self.assertEqual(self.normalize(object_bytes(token(4, 123))),
                         self.normalize(object_bytes(token(4, 123, 128))))

    def test_literal_offsets_resolve_but_changed_bytes_differ(self):
        first = self.normalize(object_bytes(token(2, 0), b"abc\0"))
        self.assertEqual(first, self.normalize(object_bytes(token(2, 4), b"pad\0abc\0")))
        self.assertNotEqual(first, self.normalize(object_bytes(token(2, 0), b"abd\0")))

    def test_actual_branch_target_change_is_detected(self):
        # IF occupies 6 bytes. Displacements 2 and 4 words select token 2 or 3.
        tail = token(4, 1) + token(15, 16, 32) + token(15, 16, 32)
        first = self.normalize(object_bytes(token(11, 0, 32) + struct.pack("<H", 2) + tail))
        second = self.normalize(object_bytes(token(11, 0, 32) + struct.pack("<H", 4) + tail))
        self.assertEqual(first[0][3], 2)
        self.assertEqual(second[0][3], 3)
        self.assertNotEqual(first, second)

    def test_literal_must_terminate_inside_its_segment(self):
        with self.assertRaises(ValueError):
            self.normalize(object_bytes(token(2, 0), b"unterminated"))

    def test_long_token_cannot_read_into_data(self):
        with self.assertRaises(ValueError):
            self.normalize(object_bytes(bytes([4, 128, 0, 0]), b"\0\0"))


if __name__ == "__main__":
    unittest.main()
