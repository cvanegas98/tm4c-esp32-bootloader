#!/usr/bin/env python3
"""Build and check slot images for the TM4C123 A/B bootloader.

Implements the D7 image header (docs/design-decisions.md) with the D5 CRC-32.

A packed file is laid out exactly as the slot sits in flash:

    offset 0x000  32-byte header
    offset 0x020  0xFF up to the image base
    offset 0x400  application image, padded with 0xFF to a multiple of 4

so it can be programmed directly at the slot base, e.g. for slot A:

    LMFlash.exe -q ek-tm4c123gxl -e 0xA000-0x12FFF -o 0xA000 -v slot_a.bin

Usage:
    python tools/pack_image.py pack Objects/app_a.bin --slot A --version 1.0.0 -o Objects/slot_a.bin
    python tools/pack_image.py check Objects/slot_a.bin --slot A
"""

import argparse
import struct
import sys
import zlib

MAGIC = 0xB007C0DE
HEADER_VERSION = 1
HEADER_SIZE = 32
HEADER_FORMAT = "<8I"  # all fields are little-endian 32-bit words (D7)
HEADER_SIZE_MAX = 1024  # one header page; later versions may only append (D7)

HEADER_PAGE_BYTES = 0x400
IMAGE_SIZE_MAX = 0x8C00  # 35 KB per slot (memory-map.md)
PAD_BYTE = 0xFF

SLOT_BASE = {"A": 0x0A000, "B": 0x13000}

SRAM_BASE = 0x20000000
SRAM_END = 0x20008000

CRC32_CHECK_INPUT = b"123456789"
CRC32_CHECK_VALUE = 0xCBF43926  # CRC-32/ISO-HDLC check value (D5)


class ImageError(Exception):
    pass


def crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def image_base(slot):
    return SLOT_BASE[slot] + HEADER_PAGE_BYTES


def parse_version(text):
    parts = text.split(".")
    if len(parts) != 3 or not all(p.isdigit() for p in parts):
        raise ImageError(f"version must be MAJOR.MINOR.PATCH, got {text!r}")
    major, minor, patch = (int(p) for p in parts)
    if major > 0xFFFF or minor > 0xFF or patch > 0xFF:
        raise ImageError("version limits: major 0-65535, minor 0-255, patch 0-255")
    return (major << 16) | (minor << 8) | patch


def format_version(value):
    return f"{value >> 16}.{(value >> 8) & 0xFF}.{value & 0xFF}"


def check_vector_table(image, load_address):
    """The same sanity checks the bootloader applies before jumping (D7)."""
    if len(image) < 8:
        raise ImageError("image too small to hold a vector table")
    sp, reset = struct.unpack_from("<2I", image, 0)
    if not (SRAM_BASE < sp <= SRAM_END) or sp % 8 != 0:
        raise ImageError(f"initial SP 0x{sp:08X} is not an 8-byte-aligned SRAM address")
    if reset & 1 == 0:
        raise ImageError(f"reset handler 0x{reset:08X} does not have the Thumb bit set")
    target = reset & ~1
    if not (load_address <= target < load_address + len(image)):
        raise ImageError(
            f"reset handler 0x{target:08X} is outside the image "
            f"[0x{load_address:05X}, 0x{load_address + len(image):05X}). "
            "Was the image linked for this slot? (D1: one build per slot)"
        )
    return sp, reset


def pack(raw, slot, fw_version):
    if not raw:
        raise ImageError("input image is empty")

    image = raw + bytes([PAD_BYTE]) * (-len(raw) % 4)
    if len(image) > IMAGE_SIZE_MAX:
        raise ImageError(f"image is {len(image)} bytes; a slot holds at most {IMAGE_SIZE_MAX}")

    load_address = image_base(slot)
    check_vector_table(image, load_address)

    # header_crc covers bytes 8 .. header_size - 1, i.e. everything after itself (D5, D7).
    body = struct.pack(
        "<6I",
        HEADER_VERSION,
        HEADER_SIZE,
        load_address,
        len(image),
        crc32(image),
        fw_version,
    )
    header = struct.pack("<2I", MAGIC, crc32(body)) + body

    gap = bytes([PAD_BYTE]) * (HEADER_PAGE_BYTES - len(header))
    return header + gap + image


def check(packed, slot):
    """Validate a packed file in the bootloader's order (D7). Returns the header fields."""
    if len(packed) < HEADER_PAGE_BYTES + 8:
        raise ImageError("file is too small to hold a header page and an image")

    fields = struct.unpack_from(HEADER_FORMAT, packed, 0)
    magic, header_crc, header_version, header_size, load_address, image_size, image_crc, fw_version = fields

    if magic != MAGIC:
        raise ImageError(f"magic 0x{magic:08X} != 0x{MAGIC:08X}")
    if not (HEADER_SIZE <= header_size <= HEADER_SIZE_MAX) or header_size % 4 != 0:
        raise ImageError(f"header_size {header_size} out of range")
    if crc32(packed[8:header_size]) != header_crc:
        raise ImageError("header_crc mismatch")
    if header_version < 1:
        raise ImageError(f"header_version {header_version} not accepted")
    if load_address != image_base(slot):
        raise ImageError(
            f"load_address 0x{load_address:05X} does not match slot {slot} "
            f"(0x{image_base(slot):05X})"
        )
    if image_size == 0 or image_size % 4 != 0 or image_size > IMAGE_SIZE_MAX:
        raise ImageError(f"image_size {image_size} out of range")

    image = packed[HEADER_PAGE_BYTES:HEADER_PAGE_BYTES + image_size]
    if len(image) != image_size:
        raise ImageError(f"file holds {len(image)} image bytes; header says {image_size}")
    if crc32(image) != image_crc:
        raise ImageError("image_crc mismatch")

    check_vector_table(image, load_address)

    return {
        "header_version": header_version,
        "header_size": header_size,
        "load_address": load_address,
        "image_size": image_size,
        "image_crc": image_crc,
        "header_crc": header_crc,
        "fw_version": fw_version,
    }


def print_summary(fields, slot):
    print(f"slot {slot}: load 0x{fields['load_address']:05X}, "
          f"{fields['image_size']} bytes, version {format_version(fields['fw_version'])}")
    print(f"  image_crc  0x{fields['image_crc']:08X}")
    print(f"  header_crc 0x{fields['header_crc']:08X}")


def main():
    if crc32(CRC32_CHECK_INPUT) != CRC32_CHECK_VALUE:
        sys.exit("zlib.crc32 does not produce the CRC-32/ISO-HDLC check value")

    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)

    p_pack = sub.add_parser("pack", help="build a slot image from a fromelf .bin")
    p_pack.add_argument("input", help="application .bin linked for the chosen slot")
    p_pack.add_argument("--slot", required=True, choices=sorted(SLOT_BASE))
    p_pack.add_argument("--version", required=True, help="MAJOR.MINOR.PATCH")
    p_pack.add_argument("-o", "--output", required=True)

    p_check = sub.add_parser("check", help="validate a packed slot image")
    p_check.add_argument("input")
    p_check.add_argument("--slot", required=True, choices=sorted(SLOT_BASE))

    args = parser.parse_args()

    try:
        if args.command == "pack":
            with open(args.input, "rb") as f:
                raw = f.read()
            packed = pack(raw, args.slot, parse_version(args.version))
            fields = check(packed, args.slot)  # re-validate what was just built
            with open(args.output, "wb") as f:
                f.write(packed)
            print_summary(fields, args.slot)
            print(f"  wrote {args.output} ({len(packed)} bytes; program at 0x{SLOT_BASE[args.slot]:05X})")
        else:
            with open(args.input, "rb") as f:
                packed = f.read()
            print_summary(check(packed, args.slot), args.slot)
            print("  OK")
    except (ImageError, OSError) as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
