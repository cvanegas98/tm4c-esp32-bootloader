// Christian Vanegas
// Date: 2026-09-28
// Description: Checks and scans metadata journal records.

#include "journal.h"
#include "crc32.h"
#include "flash.h"

static bool page_is_erased(uint32_t base) {
    const volatile uint32_t *words = (const volatile uint32_t *)(uintptr_t)base;

    for (size_t i = 0; i < JOURNAL_PAGE_SIZE / sizeof(uint32_t); i++) {
        if (words[i] != UINT32_MAX) {
            return false;
        }
    }
    return true;
}

static bool page_has_room(uint32_t base, const void *next_free) {
    if (next_free == NULL) {
        return false;
    }

    uintptr_t offset = (uintptr_t)next_free - (uintptr_t)base;
    if (offset < sizeof(uint32_t) ||
        offset > JOURNAL_PAGE_SIZE ||
        JOURNAL_PAGE_SIZE - offset < sizeof(journal_record_t)) {
        return false;
    }

    // A scan also stops at a torn record. Its slot must be blank before reuse.
    const volatile uint32_t *words = (const volatile uint32_t *)next_free;
    for (size_t i = 0; i < sizeof(journal_record_t) / sizeof(uint32_t); i++) {
        if (words[i] != UINT32_MAX) {
            return false;
        }
    }
    return true;
}

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
                       const journal_record_t **status_b,
                       const void **next_free) {
    *active_slot = NULL;
    *status_a = NULL;
    *status_b = NULL;
    *next_free = NULL;

    if (page_size < sizeof(uint32_t)) {
        return;
    }

    const uint8_t *records = (const uint8_t *)page + sizeof(uint32_t);
    size_t count = (page_size - sizeof(uint32_t)) / sizeof(journal_record_t);
    size_t i = 0;

    for (; i < count; i++) {
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

    // On a stopped scan this points at the invalid record. On a full
    // scan it points just after the last complete record slot.
    *next_free = records + i * sizeof(journal_record_t);
}

journal_page_select_t journal_read(const journal_record_t **active_slot,
                                   const journal_record_t **status_a,
                                   const journal_record_t **status_b,
                                   const void **next_free) {
    *active_slot = NULL;
    *status_a = NULL;
    *status_b = NULL;
    *next_free = NULL;

    const void *page_0 = (const void *)(uintptr_t)JOURNAL_PAGE_0_BASE;
    const void *page_1 = (const void *)(uintptr_t)JOURNAL_PAGE_1_BASE;

    uint32_t gen_0 = journal_page_gen(page_0);
    uint32_t gen_1 = journal_page_gen(page_1);
    journal_page_select_t selected =
        journal_select_current_page(gen_0, gen_1);

    switch (selected) {
    case JOURNAL_PAGE_SELECT_0:
        journal_scan_page(page_0, JOURNAL_PAGE_SIZE, active_slot, status_a, status_b, next_free);
        break;
    case JOURNAL_PAGE_SELECT_1:
        journal_scan_page(page_1, JOURNAL_PAGE_SIZE, active_slot, status_a, status_b, next_free);
        break;
    case JOURNAL_PAGE_SELECT_NONE:
    case JOURNAL_PAGE_SELECT_AMBIGUOUS:
        break;
    }

    return selected;
}

uint32_t journal_write_record(uint32_t address, const journal_record_t *record) {
    uint32_t result;

    result = flash_write_word(address + offsetof(journal_record_t, boot_counter), record->boot_counter);
    if (result != 0u) {
        return result;
    }

    result = flash_write_word(address + offsetof(journal_record_t, seq), record->seq);
    if (result != 0u) {
        return result;
    }

    result = flash_write_word(address + offsetof(journal_record_t, type), record->type);
    if (result != 0u) {
        return result;
    }

    result = flash_write_word(address + offsetof(journal_record_t, value), record->value);
    if (result != 0u) {
        return result;
    }

    result = flash_write_word(address + offsetof(journal_record_t, image_crc), record->image_crc);
    if (result != 0u) {
        return result;
    }

    // This is the final write that commits the record.
    return flash_write_word(address + offsetof(journal_record_t, record_crc), journal_record_crc(record));
}

uint32_t journal_append(uint32_t type, uint32_t value, uint32_t image_crc) {
    const journal_record_t *active;
    const journal_record_t *status_a;
    const journal_record_t *status_b;
    const void *next_free;

    journal_page_select_t selected = journal_read(&active, &status_a, &status_b, &next_free);

    if (selected == JOURNAL_PAGE_SELECT_NONE || selected == JOURNAL_PAGE_SELECT_AMBIGUOUS) {
        return JOURNAL_ERR_NO_PAGE;
    }

    uint32_t page_base = selected == JOURNAL_PAGE_SELECT_0 ? JOURNAL_PAGE_0_BASE : JOURNAL_PAGE_1_BASE;

    if (!page_has_room(page_base, next_free)) {
        return JOURNAL_ERR_NO_ROOM;
    }

    uint32_t seq = 0u;
    if (active != NULL && active->seq > seq) {
        seq = active->seq;
    }
    if (status_a != NULL && status_a->seq > seq) {
        seq = status_a->seq;
    }
    if (status_b != NULL && status_b->seq > seq) {
        seq = status_b->seq;
    }

    journal_record_t record = {
        .seq = seq + 1u,
        .type = type,
        .value = value,
        .image_crc = image_crc,
        .boot_counter = 0xFFFFFFFFu
    };

    return journal_write_record((uint32_t)(uintptr_t)next_free, &record);
}

uint32_t journal_compact(void) {
    const journal_record_t *active;
    const journal_record_t *status_a;
    const journal_record_t *status_b;
    const void *next_free;

    journal_page_select_t selected = journal_read(&active, &status_a, &status_b, &next_free);

    if (selected == JOURNAL_PAGE_SELECT_NONE || selected == JOURNAL_PAGE_SELECT_AMBIGUOUS) {
        return JOURNAL_ERR_NO_PAGE;
    }

    uint32_t current = selected == JOURNAL_PAGE_SELECT_0 ? JOURNAL_PAGE_0_BASE : JOURNAL_PAGE_1_BASE;
    uint32_t target = selected == JOURNAL_PAGE_SELECT_0 ? JOURNAL_PAGE_1_BASE : JOURNAL_PAGE_0_BASE;
    uint32_t current_gen = journal_page_gen((const void *)(uintptr_t)current);

    // Incrementing this value would produce the erased-page sentinel.
    if (current_gen == JOURNAL_PAGE_GEN_ERASED - 1u) {
        return JOURNAL_ERR_NO_PAGE;
    }

    if (!page_is_erased(target)) {
        uint32_t result = flash_erase_page(target);
        if (result != 0u) {
            return result;
        }
    }

    // TODO (Phase 2): Feed the watchdog here. D6 requires a feed between
    // multi-page erases; the old page is erased after the copy and handoff.

    const journal_record_t *winners[3] = {
        active, status_a, status_b
    };
    uint32_t copied = 0u;

    for (size_t i = 0; i < 3; i++) {
        if (winners[i] == NULL) {
            continue;
        }

        uint32_t address = target + sizeof(uint32_t) + copied * sizeof(journal_record_t);
        uint32_t result = journal_write_record(address, winners[i]);
        if (result != 0u) {
            return result;
        }
        copied++;
    }

    // Last write that makes the target page authoritative.
    uint32_t result = flash_write_word(target, current_gen + 1u);
    if (result != 0u) {
        return result;
    }

    // Winner pointers referred to the old page; all reads are finished.
    result = flash_erase_page(current);
    if (result != 0u) {
        return JOURNAL_ERR_CLEANUP | result;
    }
    return 0u;
}

uint32_t journal_ensure_room(void) {
    const journal_record_t *active;
    const journal_record_t *status_a;
    const journal_record_t *status_b;
    const void *next_free;

    journal_page_select_t selected = journal_read(&active, &status_a, &status_b, &next_free);

    if (selected == JOURNAL_PAGE_SELECT_NONE || selected == JOURNAL_PAGE_SELECT_AMBIGUOUS) {
        return JOURNAL_ERR_NO_PAGE;
    }

    uint32_t current = selected == JOURNAL_PAGE_SELECT_0 ? JOURNAL_PAGE_0_BASE : JOURNAL_PAGE_1_BASE;

    if (page_has_room(current, next_free)) {
        return 0u;
    }

    return journal_compact();
}
