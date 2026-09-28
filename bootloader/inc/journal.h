// Christian Vanegas
// Date: 2026-09-28
// Description: Declares the metadata journal record and page interfaces.

#ifndef JOURNAL_H
#define JOURNAL_H

#include <stdbool.h>
#include <stddef.h>
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

#define JOURNAL_PAGE_GEN_ERASED 0xFFFFFFFFu
#define JOURNAL_PAGE_0_BASE 0x08000u
#define JOURNAL_PAGE_1_BASE 0x08400u
#define JOURNAL_PAGE_SIZE   1024u

#define JOURNAL_ERR_NO_ROOM 0x10000000u
#define JOURNAL_ERR_NO_PAGE 0x08000000u
#define JOURNAL_ERR_CLEANUP (1u << 26)
#define JOURNAL_ERR_NO_RECORD 0x02000000u

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

typedef enum {
    JOURNAL_PAGE_SELECT_NONE,
    JOURNAL_PAGE_SELECT_0,
    JOURNAL_PAGE_SELECT_1,
    JOURNAL_PAGE_SELECT_AMBIGUOUS
} journal_page_select_t;

uint32_t journal_record_crc(const journal_record_t *record);
bool journal_record_is_valid(const journal_record_t *record);

uint32_t journal_page_gen(const void *page);
journal_page_select_t journal_select_current_page(uint32_t gen_0, uint32_t gen_1);

void journal_scan_page(const void *page, size_t page_size,
                       const journal_record_t **active_slot,
                       const journal_record_t **status_a,
                       const journal_record_t **status_b,
                       const void **next_free);

journal_page_select_t journal_read(const journal_record_t **active_slot,
                                   const journal_record_t **status_a,
                                   const journal_record_t **status_b,
                                   const void **next_free);

uint32_t journal_write_record(uint32_t address, const journal_record_t *record);
uint32_t journal_append(uint32_t type, uint32_t value, uint32_t image_crc);

uint32_t journal_compact(void);
uint32_t journal_ensure_room(void);

uint32_t journal_attempts_used(const journal_record_t *record);
uint32_t journal_consume_attempt(uint32_t type);

#endif // JOURNAL_H
