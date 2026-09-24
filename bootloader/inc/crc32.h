// Christian Vanegas
// Date: 2026-09-21
// Description: Declares the bootloader's CRC-32 computation interface.

#ifndef CRC32_H
#define CRC32_H

#include <stdint.h>
#include <stddef.h>

uint32_t crc32_compute(const void *data, size_t length);

#endif // CRC32_H
