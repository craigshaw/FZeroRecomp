"""Synthetic save tests. Optional FZERO_RECORDS_LIBRARY checks the C codec."""
import ctypes
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import merge_saves as merge


def legacy_save(marker=0):
    data = bytearray(2048)
    data[:5] = data[507:512] = b"FZERO"
    data[506] = marker
    for track in range(15):
        base = merge.legacy_offset(track)
        data[base:base + 33] = merge.EMPTY * 11
    merge.legacy_checksums(data)
    return data


def extended_save(records, marker=0):
    data = legacy_save(marker)
    data[512:] = merge.encode_extension(records, data[:512])
    return bytes(data)


def put_legacy(data, track, row, time, car=0, flag=0):
    offset = merge.legacy_offset(track) + row * 3
    data[offset:offset + 3] = merge.time_bytes(time, car, flag)
    merge.legacy_checksums(data)


def fix_extension_crc(data):
    data[524:528] = bytes(4)
    struct.pack_into("<I", data, 524, merge.checksum(data[512:]))


class MergeTests(unittest.TestCase):
    def test_known_runtime_codec_vector(self):
        records = merge.empty_records()
        for t in range(15):
            for c in range(4):
                records[t][c][0] = [10000 + t * 100 + c * 10 + j for j in range(10)]
                records[t][c][1] = [1000 + t * 10 + c + j for j in range(5)]
        data = extended_save(records)
        # Same full-save vector checked by tests/test_records.c.
        self.assertEqual(merge.checksum(data), 0x3602D52F)
        self.assertEqual(merge.decode_extension(data), records)

    def test_distinct_union_capacity_and_primary_data(self):
        a, b = merge.empty_records(), merge.empty_records()
        a[0][0] = [[100, 100, 300], [10, 10, 30]]
        b[0][0] = [[100, 200, 300], [10, 20]]
        b[0][1] = [[100], [10]]
        a[14][3] = [list(range(100, 110)), list(range(20, 25))]
        b[14][3] = [list(range(105, 115)), list(range(22, 27))]
        primary, secondary = extended_save(a, 0xA5), extended_save(b, 0x5A)
        result = merge.merge_saves(primary, secondary)
        records = merge.decode_extension(result)
        self.assertEqual(records[0][0], [[100, 200, 300], [10, 20, 30]])
        self.assertEqual(records[0][1], [[100], [10]])
        self.assertEqual(records[14][3], [list(range(100, 110)), list(range(20, 25))])
        self.assertEqual(result[506], 0xA5)
        self.assertEqual(result[:5], primary[:5])
        self.assertEqual(result[507:512], primary[507:512])
        self.assertEqual(len(result), 2048)
        # Original table also contains the merged records with car ownership.
        self.assertEqual(result[5:8], merge.time_bytes(100, 0, 0))
        self.assertEqual(result[8:11], merge.time_bytes(100, 1, 0))
        self.assertEqual(result[35:38], merge.time_bytes(10, 0, 0))
        merge.parse_save(result)  # Includes original and extended checksums.
        self.assertEqual(struct.unpack_from('<I', result, 520)[0], merge.checksum(result[:512]))
        self.assertEqual(merge.merge_saves(result, secondary), result)
        self.assertEqual(merge.merge_saves(result, primary), result)
        self.assertEqual(merge.merge_saves(result, result), result)

    def test_legacy_import_placeholders_and_mode_flags(self):
        a, b = legacy_save(17), legacy_save(42)
        put_legacy(a, 0, 0, 9000, 2, 0x40)
        put_legacy(a, 0, 1, 9000, 2)
        put_legacy(b, 0, 0, 9000, 2)
        put_legacy(b, 0, 1, 8000, 1, 0x40)
        put_legacy(b, 0, 10, 2000, 3)
        b[512:] = b'\xff' * 1536
        result = merge.merge_saves(bytes(a), bytes(b))
        records = merge.decode_extension(result)
        self.assertEqual(records[0][2][0], [9000])
        self.assertEqual(records[0][1][0], [8000])
        self.assertEqual(records[0][3][1], [2000])
        self.assertEqual(records[1], [[[], []] for _ in range(4)])
        self.assertEqual(result[5:8], merge.time_bytes(8000, 1, 0x40))
        self.assertEqual(result[8:11], merge.time_bytes(9000, 2, 0x40))
        self.assertEqual(merge.merge_saves(result, bytes(b)), result)

    def test_externally_updated_original_records_are_included(self):
        a = merge.empty_records()
        a[4][2][0] = [8000]
        data = bytearray(extended_save(a))
        put_legacy(data, 4, 0, 7000, 2)
        result = merge.merge_saves(bytes(data), bytes(legacy_save()))
        self.assertEqual(merge.decode_extension(result)[4][2][0], [7000, 8000])

    def test_long_laps_and_unrepresentable_union(self):
        a, b = merge.empty_records(), merge.empty_records()
        a[2][0][1] = [59999]
        b[2][1][1] = [15000]
        self.assertEqual(merge.decode_extension(extended_save(a)), a)
        with self.assertRaisesRegex(merge.SaveError, 'Track 3.*long-lap slot'):
            merge.merge_saves(extended_save(a), extended_save(b))
        b[2][0][1] = [100, 200, 300, 400, 500]
        result = merge.merge_saves(extended_save(a), extended_save(b))
        self.assertEqual(merge.decode_extension(result)[2][0][1], b[2][0][1])
        self.assertEqual(merge.decode_extension(result)[2][1][1], [15000])

    def test_reject_invalid_inputs(self):
        valid = extended_save(merge.empty_records())
        invalid = [valid[:-1], valid + b'\0', bytes(2048)]
        for offset in (0, 170, 512, 530, 2047):
            damaged = bytearray(valid)
            damaged[offset] ^= 1
            invalid.append(bytes(damaged))
        for offset, value in ((516, 2), (517, 1), (518, 0), (2046, 0xC0), (2047, 1)):
            damaged = bytearray(valid)
            damaged[offset] = value
            fix_extension_crc(damaged)
            invalid.append(bytes(damaged))
        damaged = bytearray(valid)
        damaged[528:546] = b'\xff' * 18  # Out-of-range combinatorial rank.
        fix_extension_crc(damaged)
        invalid.append(bytes(damaged))
        damaged = legacy_save()
        damaged[5:8] = bytes((0x80, 0x60, 0))
        merge.legacy_checksums(damaged)
        invalid.append(bytes(damaged))
        for data in invalid:
            with self.subTest(data=data[:8]):
                with self.assertRaises(merge.SaveError):
                    merge.merge_saves(data, valid)
                with self.assertRaises(merge.SaveError):
                    merge.merge_saves(valid, data)

    def test_randomised_union_and_byte_idempotence(self):
        rng = random.Random(1840)
        for trial in range(30):
            a, b = merge.empty_records(), merge.empty_records()
            expected = merge.empty_records()
            for t in range(15):
                for c in range(4):
                    for k, limit in enumerate((10, 5)):
                        for records in (a, b):
                            records[t][c][k] = sorted(rng.randrange(20) * 10 for _ in range(limit))
                        expected[t][c][k] = sorted(set(a[t][c][k] + b[t][c][k]))[:limit]
            primary, secondary = extended_save(a, 7), extended_save(b, 9)
            result = merge.merge_saves(primary, secondary)
            self.assertEqual(merge.decode_extension(result), expected)
            self.assertEqual(merge.decode_extension(merge.merge_saves(secondary, primary)), expected)
            self.assertEqual(merge.merge_saves(result, secondary), result)

    def test_cli_preserves_inputs_and_refuses_existing_output(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            a, b, out = root / 'primary.srm', root / 'secondary.sav', root / 'merged.srm'
            a.write_bytes(legacy_save(11))
            b.write_bytes(legacy_save(22))
            original = (a.read_bytes(), b.read_bytes())
            command = [sys.executable, str(ROOT / 'tools/merge_saves.py'), str(a), str(b)]
            run = subprocess.run(command + [str(out)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            saved = out.read_bytes()
            for destination in (out, a, b):
                run = subprocess.run(command + [str(destination)], capture_output=True, text=True)
                self.assertNotEqual(run.returncode, 0)
            self.assertEqual(out.read_bytes(), saved)
            self.assertEqual((a.read_bytes(), b.read_bytes()), original)
            b.write_bytes(b'invalid')
            missing = root / 'must-not-exist.srm'
            run = subprocess.run(command + [str(missing)], capture_output=True, text=True)
            self.assertNotEqual(run.returncode, 0)
            self.assertFalse(missing.exists())

    @unittest.skipUnless(os.environ.get('FZERO_RECORDS_LIBRARY'), 'optional native C codec check')
    def test_native_codec_reads_and_reencodes_output(self):
        class Car(ctypes.Structure):
            _fields_ = [('races', ctypes.c_uint16 * 10), ('laps', ctypes.c_uint16 * 5)]

        class Records(ctypes.Structure):
            _fields_ = [('track', (Car * 4) * 15)]

        library = ctypes.CDLL(os.environ['FZERO_RECORDS_LIBRARY'])
        buffer_type = ctypes.c_uint8 * 2048
        library.FZeroRecordsDecode.argtypes = [ctypes.POINTER(Records), ctypes.POINTER(ctypes.c_uint8), ctypes.POINTER(ctypes.c_bool)]
        library.FZeroRecordsDecode.restype = ctypes.c_int
        library.FZeroRecordsEncode.argtypes = [ctypes.POINTER(Records), ctypes.POINTER(ctypes.c_uint8)]
        library.FZeroRecordsEncode.restype = ctypes.c_bool
        library.FZeroRecordsLegacyValid.argtypes = [ctypes.POINTER(ctypes.c_uint8)]
        library.FZeroRecordsLegacyValid.restype = ctypes.c_bool
        records = merge.empty_records()
        for t in range(15):
            for c in range(4):
                records[t][c] = [[0, 1000 + t + c, 59999], [0, 100 + t + c, 12000]]
            records[t][t % 4][1].append(59999)
        output = merge.merge_saves(extended_save(records, 91), bytes(legacy_save()))
        data, native, changed = buffer_type.from_buffer_copy(output), Records(), ctypes.c_bool(True)
        self.assertTrue(library.FZeroRecordsLegacyValid(data))
        self.assertEqual(library.FZeroRecordsDecode(ctypes.byref(native), data, ctypes.byref(changed)), 1)
        self.assertFalse(changed.value)
        for t in range(15):
            for c in range(4):
                self.assertEqual([v for v in native.track[t][c].races if v != 65535], records[t][c][0])
                self.assertEqual([v for v in native.track[t][c].laps if v != 65535], records[t][c][1])
        self.assertTrue(library.FZeroRecordsEncode(ctypes.byref(native), data))
        self.assertEqual(bytes(data), output)


if __name__ == '__main__':
    unittest.main()
