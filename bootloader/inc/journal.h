// Christian Vanegas
// Date: 2026-09-28
// Description: Declares the metadata journal record format and CRC checks.

#ifndef JOURNAL_H
#define JOURNAL_H

#include <stdbool.h>
#include <stdint.h>

#define JOURNAL_TYPE_ACTIVE_SLOT 0
#define JOURNAL_TYPE_STATUS_A    1
#define JOURNAL_TYPE_STATUS_B    2

#define JOURNAL_STATUS_UNWRITTEN 0
#define JOURNAL_STATUS_PENDING   1
#define JOURNAL_STATUS_GOOD      2
#define JOURNAL_STATUS_BAD       3

#define JOURNAL_SLOT_A 0
#define JOURNAL_SLOT_B 1

typedef struct {
    uint32_t seq;
    uint32_t type;
    uint32_t value;
    uint32_t image_crc;
    uint32_t record_crc;
    uint32_t boot_counter;
} journal_record_t;

_Static_assert(sizeof(journal_record_t) == 24,
               "journal record must be 24 bytes");

uint32_t journal_record_crc(const journal_record_t *record);
bool journal_record_is_valid(const journal_record_t *record);

#endif // JOURNAL_H
