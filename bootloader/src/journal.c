// Christian Vanegas
// Date: 2026-09-28
// Description: Checks metadata journal record CRCs.

#include "journal.h"
#include "crc32.h"

uint32_t journal_record_crc(const journal_record_t *record) {
    return crc32_compute(record, 16);
}

bool journal_record_is_valid(const journal_record_t *record) {
    return record->record_crc == journal_record_crc(record);
}
