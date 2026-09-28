// Christian Vanegas
// Date: 2026-09-28
// Description: Host tests for metadata journal record CRCs.

#include "journal.h"

#include <stdio.h>
#include "flash.h"

uint32_t flash_write_word(uint32_t address, uint32_t value) {
    (void)address;
    (void)value;
    return FLASH_ERR_INVALID_ADDRESS;
}

uint32_t flash_erase_page(uint32_t address) {
    (void)address;
    return FLASH_ERR_INVALID_ADDRESS;
}

int main(void) {
    journal_record_t record = {
        .seq = 0x01020304,
        .type = JOURNAL_TYPE_STATUS_A,
        .value = JOURNAL_STATUS_PENDING,
        .image_crc = 0x11223344,
        .record_crc = 0xBD59CE03,
        .boot_counter = 0xFFFFFFFF
    };

    // CRC of the first four fields in little-endian byte order.
    if (journal_record_crc(&record) != 0xBD59CE03) {
        fputs("Journal record CRC check value failed\n", stderr);
        return 1;
    }

    if (!journal_record_is_valid(&record)) {
        fputs("Valid journal record was rejected\n", stderr);
        return 1;
    }

    record.boot_counter &= ~1u;
    if (!journal_record_is_valid(&record)) {
        fputs("Boot counter change invalidated record\n", stderr);
        return 1;
    }

    record.value = JOURNAL_STATUS_GOOD;
    if (journal_record_is_valid(&record)) {
        fputs("Changed record value was accepted\n", stderr);
        return 1;
    }
    record.value = JOURNAL_STATUS_PENDING; // restore before the remaining checks

    record.seq ^= 1u;
    if (journal_record_is_valid(&record)) {
        fputs("Changed record seq was accepted\n", stderr);
        return 1;
    }
    record.seq ^= 1u;

    record.type ^= 1u;
    if (journal_record_is_valid(&record)) {
        fputs("Changed record type was accepted\n", stderr);
        return 1;
    }
    record.type ^= 1u;

    record.image_crc ^= 1u;
    if (journal_record_is_valid(&record)) {
        fputs("Changed record image_crc was accepted\n", stderr);
        return 1;
    }
    record.image_crc ^= 1u;

    if (!journal_record_is_valid(&record)) {
        fputs("Record was not restored correctly\n", stderr);
        return 1;
    }

    journal_record_t torn = {
        .seq = 0x01020304,
        .type = JOURNAL_TYPE_STATUS_A,
        .value = JOURNAL_STATUS_PENDING,
        .image_crc = 0x11223344,
        .record_crc = 0xFFFFFFFF,
        .boot_counter = 0xFFFFFFFF
    };

    if (journal_record_is_valid(&torn)) {
        fputs("Torn record with uncommitted record_crc was accepted\n", stderr);
        return 1;
    }

    uint32_t gen = 7;

    if (journal_page_gen(&gen) != 7) {
        fputs("Page generation read failed\n", stderr);
        return 1;
    }

    if (journal_select_current_page(JOURNAL_PAGE_GEN_ERASED,
                                    JOURNAL_PAGE_GEN_ERASED)
            != JOURNAL_PAGE_SELECT_NONE ||
        journal_select_current_page(3, JOURNAL_PAGE_GEN_ERASED)
            != JOURNAL_PAGE_SELECT_0 ||
        journal_select_current_page(JOURNAL_PAGE_GEN_ERASED, 4)
            != JOURNAL_PAGE_SELECT_1 ||
        journal_select_current_page(5, 4)
            != JOURNAL_PAGE_SELECT_0 ||
        journal_select_current_page(4, 5)
            != JOURNAL_PAGE_SELECT_1 ||
        journal_select_current_page(5, 5)
            != JOURNAL_PAGE_SELECT_AMBIGUOUS) {
        fputs("Page selection failed\n", stderr);
        return 1;
    }

    struct {
        uint32_t page_gen;
        journal_record_t records[6];
    } page = { .page_gen = 7 };

    page.records[0] = (journal_record_t){
        .seq = 2, .type = JOURNAL_TYPE_ACTIVE_SLOT,
        .value = JOURNAL_SLOT_A, .boot_counter = 0xFFFFFFFF
    };
    page.records[1] = (journal_record_t){
        .seq = 5, .type = JOURNAL_TYPE_STATUS_A,
        .value = JOURNAL_STATUS_PENDING, .boot_counter = 0xFFFFFFFF
    };
    page.records[2] = (journal_record_t){
        .seq = 4, .type = JOURNAL_TYPE_STATUS_B,
        .value = JOURNAL_STATUS_GOOD, .boot_counter = 0xFFFFFFFF
    };
    page.records[3] = (journal_record_t){
        .seq = 8, .type = JOURNAL_TYPE_ACTIVE_SLOT,
        .value = JOURNAL_SLOT_B, .boot_counter = 0xFFFFFFFF
    };
    page.records[4] = (journal_record_t){
        .seq = 9, .type = JOURNAL_TYPE_STATUS_A,
        .value = JOURNAL_STATUS_GOOD,
        .record_crc = 0xFFFFFFFF, .boot_counter = 0xFFFFFFFF
    };
    page.records[5] = (journal_record_t){
        .seq = 10, .type = JOURNAL_TYPE_STATUS_A,
        .value = JOURNAL_STATUS_GOOD, .boot_counter = 0xFFFFFFFF
    };

    for (size_t i = 0; i < 4; i++) {
        page.records[i].record_crc = journal_record_crc(&page.records[i]);
    }
    page.records[5].record_crc = journal_record_crc(&page.records[5]);

    const journal_record_t *active;
    const journal_record_t *status_a;
    const journal_record_t *status_b;
    const void *next_free;

    journal_scan_page(&page, sizeof page, &active, &status_a, &status_b, &next_free);
    if (active != &page.records[3] ||
        status_a != &page.records[1] ||
        status_b != &page.records[2] ||
        next_free != &page.records[4]) {
        fputs("Page scan failed to select latest records, stop at torn record, "
              "or report the free slot\n", stderr);
        return 1;
    }

    page.records[4].type = 99;
    page.records[4].record_crc = journal_record_crc(&page.records[4]);

    if (!journal_record_is_valid(&page.records[4])) {
        fputs("Unknown-type test record has an invalid CRC\n", stderr);
        return 1;
    }

    journal_scan_page(&page, sizeof page, &active, &status_a, &status_b, &next_free);
    if (active != &page.records[3] ||
        status_a != &page.records[1] ||
        status_b != &page.records[2] ||
        next_free != &page.records[4]) {
        fputs("Page scan continued past an unknown record type\n", stderr);
        return 1;
    }

    journal_scan_page(&page, sizeof(uint32_t), &active, &status_a, &status_b, &next_free);
    if (active != NULL || status_a != NULL || status_b != NULL ||
        next_free != (const uint8_t *)&page + sizeof(uint32_t)) {
        fputs("Header-only page scan found records or misplaced next_free\n",
              stderr);
        return 1;
    }

    // A page_size smaller than the page_gen word itself is degenerate;
    // next_free has nothing sensible to report and stays NULL.
    journal_scan_page(&page, sizeof(uint32_t) - 1, &active, &status_a, &status_b,
                      &next_free);
    if (active != NULL || status_a != NULL || status_b != NULL || next_free != NULL) {
        fputs("Undersized page scan found records or a non-NULL next_free\n",
              stderr);
        return 1;
    }

    struct {
        uint32_t page_gen;
        journal_record_t records[2];
    } full_page = { .page_gen = 7 };

    full_page.records[0] = (journal_record_t){
        .seq = 1, .type = JOURNAL_TYPE_ACTIVE_SLOT,
        .value = JOURNAL_SLOT_A, .boot_counter = 0xFFFFFFFF
    };
    full_page.records[1] = (journal_record_t){
        .seq = 2, .type = JOURNAL_TYPE_STATUS_A,
        .value = JOURNAL_STATUS_PENDING, .boot_counter = 0xFFFFFFFF
    };
    full_page.records[0].record_crc = journal_record_crc(&full_page.records[0]);
    full_page.records[1].record_crc = journal_record_crc(&full_page.records[1]);

    const journal_record_t *full_active;
    const journal_record_t *full_status_a;
    const journal_record_t *full_status_b;
    const void *full_next_free;

    journal_scan_page(&full_page, sizeof full_page,
                      &full_active, &full_status_a, &full_status_b,
                      &full_next_free);
    if (full_active != &full_page.records[0] ||
        full_status_a != &full_page.records[1] ||
        full_next_free != (const uint8_t *)&full_page + sizeof full_page) {
        fputs("Fully-packed page scan did not report next_free past the end\n",
              stderr);
        return 1;
    }

    static const struct {
        uint32_t counter;
        uint32_t used;
    } attempt_cases[] = {
        { 0xFFFFFFFFu, 0u },
        { 0xFFFFFFFEu, 1u },
        { 0xFFFFFFFCu, 2u },
        { 0xFFFFFFF8u, 3u },
        // boot_counter sits outside the CRC; a stray cleared high bit must
        // count as a used attempt, never be ignored.
        { 0x7FFFFFFEu, 2u },
        { 0x00000000u, 32u },
    };

    for (size_t i = 0; i < sizeof attempt_cases / sizeof attempt_cases[0]; i++) {
        journal_record_t counted = { .boot_counter = attempt_cases[i].counter };
        if (journal_attempts_used(&counted) != attempt_cases[i].used) {
            fprintf(stderr, "Attempts used for 0x%08lX: got %lu, expected %lu\n",
                    (unsigned long)attempt_cases[i].counter,
                    (unsigned long)journal_attempts_used(&counted),
                    (unsigned long)attempt_cases[i].used);
            return 1;
        }
    }

    // The consume step clears the lowest set bit; three consumes reach 0xFFFFFFF8.
    uint32_t counter = 0xFFFFFFFFu;
    for (int i = 0; i < 3; i++) {
        counter &= counter - 1u;
    }
    if (counter != 0xFFFFFFF8u) {
        fputs("Lowest-set-bit clearing did not reach 0xFFFFFFF8\n", stderr);
        return 1;
    }

    puts("Journal tests passed");
    return 0;
}
