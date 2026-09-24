// Christian Vanegas
// Date: 2026-09-21
// Description: Host tests for the bootloader CRC-32 implementation.

#include "crc32.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static uint32_t crc32_reference(const void *data, size_t length) {
    const uint8_t *bytes = data;
    uint32_t crc = 0xFFFFFFFF;

    for (size_t i = 0; i < length; i++) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
    }

    return ~crc;
}

int main(void) {
    static const char check[] = "123456789";
    uint8_t buffer[255];
    uint32_t random_state = 0x12345678;

    if (crc32_compute(check, sizeof check - 1) != 0xCBF43926) {
        fputs("CRC-32 check value failed\n", stderr);
        return 1;
    }

    if (crc32_compute(NULL, 0) != 0x00000000) {
        fputs("Empty-input CRC-32 failed\n", stderr);
        return 1;
    }

    for (size_t length = 1; length <= sizeof buffer; length++) {
        for (size_t i = 0; i < length; i++) {
            random_state ^= random_state << 13;
            random_state ^= random_state >> 17;
            random_state ^= random_state << 5;
            buffer[i] = (uint8_t)random_state;
        }

        uint32_t actual = crc32_compute(buffer, length);
        uint32_t expected = crc32_reference(buffer, length);
        if (actual != expected) {
            fprintf(stderr, "Length %lu: got %08" PRIX32
                    ", expected %08" PRIX32 "\n",
                    (unsigned long)length, actual, expected);
            return 1;
        }
    }

    puts("CRC-32 tests passed");
    return 0;
}
