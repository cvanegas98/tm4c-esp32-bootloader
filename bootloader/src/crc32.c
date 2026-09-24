// Christian Vanegas
// Date: 2026-09-21
// Description: This file contains the implementation of the CRC32 algorithm used for data 
// integrity checks in the bootloader. The CRC32 algorithm is a widely used checksum algorithm 
// that provides a quick way to detect errors in data transmission or storage.

#include "crc32.h"

static const uint32_t crc_table[16] = {
    0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC,
    0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
    0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
    0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C
};

uint32_t crc32_compute(const void *data, size_t length) {
    const uint8_t *current = data;
    uint32_t crc = 0xFFFFFFFF;

    for (size_t i = 0; i < length; i++) {
        crc ^= current[i];
        crc = (crc >> 4) ^ crc_table[crc & 0x0F];
        crc = (crc >> 4) ^ crc_table[crc & 0x0F];
    }

    return ~crc;
}
