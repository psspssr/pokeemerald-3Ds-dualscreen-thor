import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from verify_gba_save import verify


def image(counter=1, slot=0):
    data = bytearray(b'\xff' * 131072)
    for index in range(14):
        start = (slot * 14 + index) * 4096
        data[start:start + 4096] = bytes(4096)
        # Zero payload's checksum is independently known to be zero.
        struct.pack_into('<HHII', data, start + 0xFF4, index, 0, 0x08012025, counter)
    return data


class GbaSaveTests(unittest.TestCase):
    def test_standard_and_truncated(self):
        self.assertEqual(verify(bytes(image()))['save_counter'], 1)
        self.assertEqual(verify(bytes(image()[:65536]))['selected_slot'], 0)

    def test_bad_checksum_rejected(self):
        data = image()
        data[123] = 1
        with self.assertRaisesRegex(ValueError, 'no complete valid'):
            verify(bytes(data))

    def test_duplicate_section_rejected(self):
        data = image()
        struct.pack_into('<H', data, 4096 + 0xFF4, 0)
        with self.assertRaisesRegex(ValueError, 'no complete valid'):
            verify(bytes(data))

    def test_incomplete_new_slot_falls_back(self):
        data = image()
        second = image(counter=2, slot=1)
        data[14 * 4096:20 * 4096] = second[14 * 4096:20 * 4096]
        self.assertEqual(verify(bytes(data))['save_counter'], 1)

    def test_counter_wrap(self):
        data = image(counter=0xFFFFFFFF)
        data[14 * 4096:28 * 4096] = image(counter=0, slot=1)[14 * 4096:28 * 4096]
        self.assertEqual(verify(bytes(data))['selected_slot'], 1)


if __name__ == '__main__':
    unittest.main()
