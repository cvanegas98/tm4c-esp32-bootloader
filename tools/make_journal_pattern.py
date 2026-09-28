#!/usr/bin/env python3
"""Build a metadata journal page pattern for hardware bench tests (D8).

Produces a 1024-byte flash pattern: a page_gen word, followed by the given
records (each with a correctly computed record_crc, boot_counter left at
0xFFFFFFFF), padded with 0xFF to fill the page -- ready to program directly
at 0x08000 or 0x08400 with LM Flash, e.g.:

    LMFlash.exe -q ek-tm4c123gxl -e 0x8000-0x83FF -o 0x8000 -v page0.bin

Usage:
    python tools/make_journal_pattern.py --gen 5 \
        --record active_slot:b --record status_a:pending -o page0.bin

Each --record is TYPE:VALUE[:IMAGE_CRC[:SEQ]]. TYPE is active_slot,
status_a, or status_b. VALUE is a/b for active_slot, or
unwritten/pending/good/bad for status_*. IMAGE_CRC defaults to 0. SEQ
defaults to the record's position (1-based) among the --record arguments,
in the order given; pass it explicitly to build adversarial orderings
(e.g. a high-seq record placed before a lower-seq one).
"""

import argparse
import struct
import sys
import zlib

PAGE_SIZE = 1024
PAD_BYTE = 0xFF

TYPES = {"active_slot": 0, "status_a": 1, "status_b": 2}
SLOT_VALUES = {"a": 0, "b": 1}
STATUS_VALUES = {"unwritten": 0, "pending": 1, "good": 2, "bad": 3}


class PatternError(Exception):
    pass


def crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def parse_record(text, default_seq):
    parts = text.split(":")
    if len(parts) < 2:
        raise PatternError(f"record {text!r} needs at least TYPE:VALUE")

    type_name, value_name = parts[0], parts[1]
    image_crc = int(parts[2], 0) if len(parts) > 2 else 0
    seq = int(parts[3], 0) if len(parts) > 3 else default_seq

    if type_name not in TYPES:
        raise PatternError(f"unknown record type {type_name!r}")
    type_id = TYPES[type_name]

    if type_name == "active_slot":
        if value_name not in SLOT_VALUES:
            raise PatternError(
                f"active_slot value must be a or b, got {value_name!r}")
        value = SLOT_VALUES[value_name]
    else:
        if value_name not in STATUS_VALUES:
            raise PatternError(
                f"status value must be one of {list(STATUS_VALUES)}, "
                f"got {value_name!r}")
        value = STATUS_VALUES[value_name]

    return seq, type_id, value, image_crc


def pack_record(seq, type_id, value, image_crc, boot_counter=0xFFFFFFFF):
    # record_crc covers offset 0-15 only (seq, type, value, image_crc); the
    # skip is deliberate -- boot_counter is bit-cleared without a re-append
    # and must stay outside the CRC (D8).
    body = struct.pack("<4I", seq, type_id, value, image_crc)
    record_crc = crc32(body)
    return body + struct.pack("<2I", record_crc, boot_counter)


def pack_page(page_gen, records):
    body = struct.pack("<I", page_gen)
    for record in records:
        body += pack_record(*record)
    if len(body) > PAGE_SIZE:
        raise PatternError(
            f"{len(body)} bytes of header/records do not fit in a "
            f"{PAGE_SIZE}-byte page")
    return body + bytes([PAD_BYTE]) * (PAGE_SIZE - len(body))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--gen", required=True, type=lambda s: int(s, 0),
        help="page_gen word (0xFFFFFFFF for an erased/inactive page)")
    parser.add_argument(
        "--record", action="append", default=[],
        metavar="TYPE:VALUE[:IMAGE_CRC[:SEQ]]",
        help="repeatable, appended in page order")
    parser.add_argument("-o", "--output", required=True)
    args = parser.parse_args()

    try:
        records = [
            parse_record(text, default_seq=i)
            for i, text in enumerate(args.record, start=1)
        ]
        pattern = pack_page(args.gen, records)
        with open(args.output, "wb") as f:
            f.write(pattern)
        print(f"wrote {args.output} ({len(pattern)} bytes): "
              f"page_gen=0x{args.gen:08X}, {len(records)} record(s)")
        for seq, type_id, value, image_crc in records:
            print(f"  seq={seq} type={type_id} value={value} "
                  f"image_crc=0x{image_crc:08X}")
    except (PatternError, OSError) as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
