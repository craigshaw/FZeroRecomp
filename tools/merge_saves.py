"""Merge distinct F-Zero records into a new save, retaining Primary metadata.

Python 3.11+, standard library only. No ROM or game build is required.
See docs/SAVE_MERGE.md for the format rules and limits.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
from math import comb
import os
from pathlib import Path
import struct
import sys
import zlib

SIZE = 2048
TRACKS = 15
CARS = 4
LIMITS = (10, 5)
EMPTY = bytes((9, 0x59, 0x99))


class SaveError(ValueError):
    """An input or result cannot be represented safely."""


def empty_records() -> list:
    # records[track][car][0=races, 1=laps]; empty positions are omitted.
    return [[[[], []] for _ in range(CARS)] for _ in range(TRACKS)]


def checksum(data: bytes | bytearray) -> int:
    return zlib.crc32(data)


def decode_list(rank: int, count: int, empty: int) -> list[int]:
    if rank >= comb(empty + count, count):
        raise SaveError("Invalid extended record rank")
    values = [empty] * count
    high = empty + count - 1
    for i in range(count, 0, -1):
        low = i - 1
        while low < high:
            mid = (low + high + 1) // 2
            if comb(mid, i) <= rank:
                low = mid
            else:
                high = mid - 1
        values[i - 1] = low - i + 1
        rank -= comb(low, i)
        high = max(0, low - 1)
    return [value for value in values if value != empty]


def encode_list(values: list[int], count: int, empty: int) -> int:
    if len(values) > count or values != sorted(values) or any(
        not 0 <= value < empty for value in values
    ):
        raise SaveError("Invalid output record list")
    padded = values + [empty] * (count - len(values))
    return sum(comb(value + i, i + 1) for i, value in enumerate(padded))


def decode_extension(data: bytes) -> list:
    ext = bytearray(data[512:])
    records = empty_records()
    if ext[:4] != b"FZRX":
        if ext not in (bytearray(1536), bytearray(b"\xff" * 1536)):
            raise SaveError("Unrecognised data in the records extension")
        return records
    if ext[4] != 1 or ext[5] != 0:
        raise SaveError("Unsupported records extension version or flags")
    expected = struct.unpack_from("<I", ext, 12)[0]
    ext[12:16] = bytes(4)
    if (ext[6:8] != b"\xcd\x05" or checksum(ext) != expected
            or ext[1535] or ext[1534] & 0xC0):
        raise SaveError("Damaged records extension")
    bits = int.from_bytes(ext[16:1501], "little")
    for track in records:
        for car in track:
            for kind, (width, empty) in enumerate(((137, 60000), (61, 12001))):
                car[kind] = decode_list(bits & ((1 << width) - 1), LIMITS[kind], empty)
                bits >>= width
    inherited = int.from_bytes(ext[1501:1535], "little")
    for track in records:
        entry = inherited & 0x3FFFF
        inherited >>= 18
        if entry == 0xFFFF:
            continue
        time, car = entry & 0xFFFF, entry >> 16
        if not 12000 < time <= 59999 or len(track[car][1]) == 5:
            raise SaveError("Invalid inherited long-lap entry")
        track[car][1].append(time)
    return records


def encode_extension(records: list, legacy: bytes | bytearray) -> bytes:
    ext = bytearray(1536)
    bits = offset = inherited = 0
    for t, track in enumerate(records):
        long_laps = []
        for c, car in enumerate(track):
            laps = car[1]
            if len(laps) > 5 or laps != sorted(laps):
                raise SaveError("Invalid output lap list")
            long_laps.extend((c, value) for value in laps if value > 12000)
            normal_laps = [value for value in laps if value <= 12000]
            for values, count, empty, width in (
                (car[0], 10, 60000, 137), (normal_laps, 5, 12001, 61)
            ):
                bits |= encode_list(values, count, empty) << offset
                offset += width
        if len(long_laps) > 1:
            raise SaveError(f"Track {t + 1}: the merged laps need more than one "
                            "long-lap slot; the 2 KB format cannot store them")
        entry = 0xFFFF
        if long_laps:
            car, time = long_laps[0]
            if time > 59999:
                raise SaveError("Invalid output long lap")
            entry = car << 16 | time
        inherited |= entry << (18 * t)
    ext[:8] = b"FZRX\x01\x00\xcd\x05"
    ext[16:1501] = bits.to_bytes(1485, "little")
    ext[1501:1535] = inherited.to_bytes(34, "little")
    struct.pack_into("<I", ext, 8, checksum(legacy))
    struct.pack_into("<I", ext, 12, checksum(ext))
    return bytes(ext)


def legacy_offset(track: int) -> int:
    return 5 + (track // 5) * 167 + (track % 5) * 33


def legacy_checksums(data: bytearray) -> None:
    for group in range(3):
        base = 5 + group * 167
        struct.pack_into("<H", data, base + 165, sum(data[base:base + 165]))


@dataclass
class Save:
    data: bytes
    records: list
    # Preserve the original mode flag where the source records supply it.
    flags: dict[tuple[int, int, int, int], int]


def parse_save(data: bytes) -> Save:
    if len(data) != SIZE:
        raise SaveError(f"Expected a 2048-byte save, got {len(data)} bytes")
    if data[:5] != b"FZERO" or data[507:512] != b"FZERO":
        raise SaveError("Invalid original save signatures")
    for group in range(3):
        base = 5 + group * 167
        if sum(data[base:base + 165]) != struct.unpack_from("<H", data, base + 165)[0]:
            raise SaveError(f"Invalid original save checksum in group {group + 1}")
    records = decode_extension(data)
    flags = {}
    for t in range(TRACKS):
        base = legacy_offset(t)
        for row in range(11):
            first, sec, cent = data[base + row * 3:base + row * 3 + 3]
            if not first & 0x80:
                continue  # Unset rows are placeholders, not earned records.
            minute = first & 15
            if (minute > 9 or sec >> 4 > 5 or sec & 15 > 9
                    or cent >> 4 > 9 or cent & 15 > 9):
                raise SaveError(f"Track {t + 1}: invalid original record time")
            time = minute * 6000 + ((sec >> 4) * 10 + (sec & 15)) * 100
            time += (cent >> 4) * 10 + (cent & 15)
            car, kind = (first >> 4) & 3, int(row == 10)
            records[t][car][kind].append(time)
            flags.setdefault((t, car, kind, time), first & 0x40)
    return Save(data, records, flags)


def time_bytes(time: int, car: int, flag: int) -> bytes:
    minute, remainder = divmod(time, 6000)
    sec, cent = divmod(remainder, 100)
    return bytes((0x80 | flag | car << 4 | minute,
                  (sec // 10) << 4 | sec % 10, (cent // 10) << 4 | cent % 10))


def merge_saves(primary: bytes, secondary: bytes) -> bytes:
    inputs = []
    for label, data in (("Primary", primary), ("Secondary", secondary)):
        try:
            inputs.append(parse_save(data))
        except SaveError as exc:
            raise SaveError(f"{label}: {exc}") from exc
    a, b = inputs
    records = empty_records()
    for t in range(TRACKS):
        for c in range(CARS):
            for kind, limit in enumerate(LIMITS):
                records[t][c][kind] = sorted(set(a.records[t][c][kind]) |
                                             set(b.records[t][c][kind]))[:limit]
    result = bytearray(primary[:512])
    # Rebuild the original mixed top ten races and single best lap. Equal
    # times from different cars remain separate; car order breaks ties.
    flags = b.flags | a.flags
    for t, track in enumerate(records):
        base = legacy_offset(t)
        for kind, limit in enumerate((10, 1)):
            rows = sorted((time, c) for c, car in enumerate(track) for time in car[kind])[:limit]
            start = base + (30 if kind else 0)
            for row in range(limit):
                entry = EMPTY
                if row < len(rows):
                    time, car = rows[row]
                    entry = time_bytes(time, car, flags.get((t, car, kind, time), 0))
                result[start + row * 3:start + row * 3 + 3] = entry
    legacy_checksums(result)
    result.extend(encode_extension(records, result))
    return bytes(result)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("primary", type=Path, help="save supplying non-record data")
    parser.add_argument("secondary", type=Path, help="save supplying additional records")
    parser.add_argument("output", type=Path, help="new output file; must not already exist")
    args = parser.parse_args(argv)
    try:
        # Bound reads so accidental selection of another file cannot consume
        # arbitrary memory. Validate both inputs before creating any output.
        sources = []
        for path in (args.primary, args.secondary):
            with path.open("rb") as source:
                sources.append(source.read(SIZE + 1))
        result = merge_saves(*sources)
        # Exclusive creation also rejects existing inputs, hard links and
        # symlinks. No overwrite switch is offered by this test utility.
        with args.output.open("xb") as target:
            try:
                if target.write(result) != SIZE:
                    raise OSError("Incomplete output write")
                target.flush()
                os.fsync(target.fileno())
            except OSError:
                target.close()
                args.output.unlink()
                raise
    except (OSError, SaveError) as exc:
        print(f"Merge failed: {exc}", file=sys.stderr)
        return 1
    print(f"Merged save written to {args.output} (2048 bytes). Inputs unchanged.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
