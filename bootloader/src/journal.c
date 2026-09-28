// Christian Vanegas
// Date: 2026-09-28
// Description: Checks and scans metadata journal records.

#include "journal.h"
#include "crc32.h"

uint32_t journal_record_crc(const journal_record_t *record) {
    return crc32_compute(record, 16);
}

bool journal_record_is_valid(const journal_record_t *record) {
    return record->record_crc == journal_record_crc(record);
}

uint32_t journal_page_gen(const void *page) {
    return *(const uint32_t *)page;
}

journal_page_select_t journal_select_current_page(uint32_t gen_0, uint32_t gen_1) {
    bool valid_0 = gen_0 != JOURNAL_PAGE_GEN_ERASED;
    bool valid_1 = gen_1 != JOURNAL_PAGE_GEN_ERASED;

    if (!valid_0 && !valid_1) {
        return JOURNAL_PAGE_SELECT_NONE;
    }
    if (!valid_0) {
        return JOURNAL_PAGE_SELECT_1;
    }
    if (!valid_1) {
        return JOURNAL_PAGE_SELECT_0;
    }
    if (gen_0 == gen_1) {
        return JOURNAL_PAGE_SELECT_AMBIGUOUS;
    }
    return gen_0 > gen_1 ? JOURNAL_PAGE_SELECT_0 : JOURNAL_PAGE_SELECT_1;
}

void journal_scan_page(const void *page, size_t page_size,
                       const journal_record_t **active_slot,
                       const journal_record_t **status_a,
                       const journal_record_t **status_b) {
    *active_slot = NULL;
    *status_a = NULL;
    *status_b = NULL;

    if (page_size < sizeof(uint32_t)) {
        return;
    }

    const uint8_t *records = (const uint8_t *)page + sizeof(uint32_t);
    size_t count = (page_size - sizeof(uint32_t)) / sizeof(journal_record_t);

    for (size_t i = 0; i < count; i++) {
        const journal_record_t *record =
            (const journal_record_t *)(records + i * sizeof(journal_record_t));

        if (!journal_record_is_valid(record) ||
            (record->type != JOURNAL_TYPE_ACTIVE_SLOT &&
             record->type != JOURNAL_TYPE_STATUS_A &&
             record->type != JOURNAL_TYPE_STATUS_B)) {
            break;
        }

        const journal_record_t **latest = active_slot;
        switch (record->type) {
        case JOURNAL_TYPE_ACTIVE_SLOT:
            break;
        case JOURNAL_TYPE_STATUS_A:
            latest = status_a;
            break;
        case JOURNAL_TYPE_STATUS_B:
            latest = status_b;
            break;
        }

        if (*latest == NULL || record->seq > (*latest)->seq) {
            *latest = record;
        }
    }
}

journal_page_select_t journal_read(const journal_record_t **active_slot,
                                   const journal_record_t **status_a,
                                   const journal_record_t **status_b) {
    *active_slot = NULL;
    *status_a = NULL;
    *status_b = NULL;

    const void *page_0 = (const void *)(uintptr_t)JOURNAL_PAGE_0_BASE;
    const void *page_1 = (const void *)(uintptr_t)JOURNAL_PAGE_1_BASE;

    uint32_t gen_0 = journal_page_gen(page_0);
    uint32_t gen_1 = journal_page_gen(page_1);
    journal_page_select_t selected =
        journal_select_current_page(gen_0, gen_1);

    switch (selected) {
    case JOURNAL_PAGE_SELECT_0:
        journal_scan_page(page_0, JOURNAL_PAGE_SIZE,
                          active_slot, status_a, status_b);
        break;
    case JOURNAL_PAGE_SELECT_1:
        journal_scan_page(page_1, JOURNAL_PAGE_SIZE,
                          active_slot, status_a, status_b);
        break;
    case JOURNAL_PAGE_SELECT_NONE:
    case JOURNAL_PAGE_SELECT_AMBIGUOUS:
        break;
    }

    return selected;
}
