// Christian Vanegas
// Date: 2026-09-28
// Description: Host tests for metadata journal record CRCs.

#include "journal.h"

#include <stdio.h>

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

    puts("Journal tests passed");
    return 0;
}
