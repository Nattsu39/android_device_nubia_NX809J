"""Check loader metadata and preserved OEM code/data in the pinned ELF transform."""

from pathlib import Path
import struct
import sys
import unittest

CAMERA = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(CAMERA))
import patch_provider as elf


class ProviderElfTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original = (CAMERA / 'provider/vendor.qti.camera.provider-service_64').read_bytes()
        cls.patched = elf.patch_provider(cls.original)

    def headers(self, data):
        phoff = struct.unpack_from('<Q', data, 32)[0]
        count = struct.unpack_from('<H', data, 56)[0]
        return [elf.PROGRAM_HEADER.unpack_from(data, phoff + index * elf.PROGRAM_HEADER.size)
                for index in range(count)]

    def dependencies(self, data):
        programs = self.headers(data)
        dynamic = next(header for header in programs if header[0] == elf.PT_DYNAMIC)
        entries = [elf.DYNAMIC_ENTRY.unpack_from(data, offset) for offset in range(
            dynamic[2], dynamic[2] + dynamic[5], elf.DYNAMIC_ENTRY.size)]
        address = next(value for tag, value in entries if tag == elf.DT_STRTAB)
        load = next(header for header in programs
                    if header[0] == elf.PT_LOAD and header[3] <= address < header[3] + header[5])
        strings_offset = address - load[3] + load[2]
        return [data[strings_offset + value:].split(b'\0', 1)[0]
                for tag, value in entries if tag == elf.DT_NEEDED]

    def test_original_dependencies_keep_their_order(self):
        self.assertEqual(self.dependencies(self.patched),
                         [elf.LIBRARY.rstrip(b'\0')] + self.dependencies(self.original))

    def test_original_load_contents_and_entry_point_are_preserved(self):
        self.assertEqual(self.original[16:32], self.patched[16:32])
        for header in self.headers(self.original):
            if header[0] == elf.PT_LOAD:
                start, length = header[2], header[5]
                # Program-header metadata lies inside the first load segment.
                start = max(start, 64 + 14 * elf.PROGRAM_HEADER.size)
                self.assertEqual(self.original[start:header[2] + length],
                                 self.patched[start:header[2] + length])

    def test_dynamic_table_stays_writable_and_inside_relro(self):
        programs = self.headers(self.patched)
        dynamic = next(header for header in programs if header[0] == elf.PT_DYNAMIC)
        relro = next(header for header in programs if header[0] == elf.PT_GNU_RELRO)
        self.assertEqual(dynamic[1], 6)
        self.assertGreaterEqual(dynamic[3], relro[3])
        self.assertLessEqual(dynamic[3] + dynamic[6], relro[3] + relro[6])

    def test_changed_firmware_is_rejected(self):
        changed = bytearray(self.original)
        changed[0x8000] ^= 1
        with self.assertRaises(ValueError):
            elf.patch_provider(changed)

    def test_duplicate_transformation_is_rejected(self):
        with self.assertRaises(ValueError):
            elf.patch_provider(self.patched)


if __name__ == '__main__':
    unittest.main()
