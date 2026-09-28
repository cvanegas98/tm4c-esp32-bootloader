// Christian Vanegas
// Date: 2026-09-28
// Description: On-target bench tests for journal compaction.

#include <stdbool.h>
#include <stdint.h>

#include "journal.h"
#include "tm4c123gh6pm.h"

// 0: full page, erased target
// 1: full page, stale target
// 2: page already has room
// 3: full page, partially cleared boot counter
#define BENCH_SCENARIO 1

#if BENCH_SCENARIO < 0 || BENCH_SCENARIO > 3
#error "BENCH_SCENARIO must be 0, 1, 2, or 3"
#endif

#if BENCH_SCENARIO == 3
#define STATUS_A_COUNTER 0xFFFFFFFCu
#else
#define STATUS_A_COUNTER 0xFFFFFFFFu
#endif

#define PF_RED     0x02u
#define PF_BLUE    0x04u
#define PF_GREEN   0x08u
#define PF_LEDS    (PF_RED | PF_BLUE | PF_GREEN)
#define PF_MAGENTA (PF_RED | PF_BLUE)
#define PF_YELLOW  (PF_RED | PF_GREEN)

volatile uint32_t ensure_result;
volatile uint32_t failed_tests;
volatile journal_page_select_t selected_before;
volatile journal_page_select_t selected_after;

static void set_leds(uint32_t leds) {
    GPIO_PORTF_DATA_R = (GPIO_PORTF_DATA_R & ~PF_LEDS) | (leds & PF_LEDS);
}

static void setup_leds(void) {
    SYSCTL_RCGCGPIO_R |= SYSCTL_RCGCGPIO_R5;
    while ((SYSCTL_PRGPIO_R & SYSCTL_PRGPIO_R5) == 0u) {}

    GPIO_PORTF_AFSEL_R &= ~PF_LEDS;
    GPIO_PORTF_AMSEL_R &= ~PF_LEDS;
    GPIO_PORTF_PCTL_R &= ~0x0000FFF0u;
    GPIO_PORTF_DIR_R |= PF_LEDS;
    GPIO_PORTF_DEN_R |= PF_LEDS;
    set_leds(0u);
}

void HardFault_Handler(void) {
    setup_leds();
    set_leds(PF_YELLOW);
    for (;;) {}
}

static bool page_is_erased(uint32_t base) {
    const volatile uint32_t *words = (const volatile uint32_t *)(uintptr_t)base;

    for (uint32_t i = 0; i < JOURNAL_PAGE_SIZE / sizeof(uint32_t); i++) {
        if (words[i] != 0xFFFFFFFFu) {
            return false;
        }
    }
    return true;
}

static bool matches_record(const journal_record_t *record,
                           uint32_t address, uint32_t seq, uint32_t type,
                           uint32_t value, uint32_t image_crc,
                           uint32_t boot_counter) {
    if ((uintptr_t)record != address) {
        return false;
    }

    return record->seq == seq &&
           record->type == type &&
           record->value == value &&
           record->image_crc == image_crc &&
           record->boot_counter == boot_counter &&
           journal_record_is_valid(record);
}

int main(void) {
    setup_leds();

    const void *page_0 = (const void *)(uintptr_t)JOURNAL_PAGE_0_BASE;
    const void *page_1 = (const void *)(uintptr_t)JOURNAL_PAGE_1_BASE;

    const journal_record_t *active;
    const journal_record_t *status_a;
    const journal_record_t *status_b;
    const void *next_free;

    selected_before = journal_read(&active, &status_a, &status_b, &next_free);

    // Every pattern starts with page 0 as the current page.
    if (journal_page_gen(page_0) != 6u || selected_before != JOURNAL_PAGE_SELECT_0) {
        set_leds(PF_MAGENTA);
        for (;;) {}
    }

#if BENCH_SCENARIO != 2

    // Page 0 contains 42 records, with seq 42 as the latest STATUS_A.
    if ((uintptr_t)next_free != JOURNAL_PAGE_0_BASE + 4u + 42u * 24u ||
        !matches_record(active, JOURNAL_PAGE_0_BASE + 4u + 3u * 24u,
                        4u, JOURNAL_TYPE_ACTIVE_SLOT, JOURNAL_SLOT_B,
                        0u, 0xFFFFFFFFu) ||
        !matches_record(status_a, JOURNAL_PAGE_0_BASE + 4u + 41u * 24u,
                        42u, JOURNAL_TYPE_STATUS_A,
                        JOURNAL_STATUS_PENDING, 0xA1B2C3D4u,
                        STATUS_A_COUNTER) ||
        !matches_record(status_b, JOURNAL_PAGE_0_BASE + 4u + 2u * 24u,
                        3u, JOURNAL_TYPE_STATUS_B,
                        JOURNAL_STATUS_GOOD, 0x55667788u,
                        0xFFFFFFFFu)) {
        set_leds(PF_MAGENTA);
        for (;;) {}
    }

#if BENCH_SCENARIO == 0 || BENCH_SCENARIO == 3
    if (!page_is_erased(JOURNAL_PAGE_1_BASE)) {
        set_leds(PF_MAGENTA);
        for (;;) {}
    }
#else
    if (journal_page_gen(page_1) != 5u) {
        set_leds(PF_MAGENTA);
        for (;;) {}
    }
#endif

#else

    // This pattern has only four records, so ensure_room should do nothing.
    if (journal_page_gen(page_1) != 5u ||
        (uintptr_t)next_free != JOURNAL_PAGE_0_BASE + 4u + 4u * 24u ||
        !matches_record(active, JOURNAL_PAGE_0_BASE + 4u + 3u * 24u,
                        4u, JOURNAL_TYPE_ACTIVE_SLOT, JOURNAL_SLOT_B,
                        0u, 0xFFFFFFFFu) ||
        !matches_record(status_a, JOURNAL_PAGE_0_BASE + 4u + 1u * 24u,
                        2u, JOURNAL_TYPE_STATUS_A,
                        JOURNAL_STATUS_PENDING, 0xA1B2C3D4u,
                        0xFFFFFFFFu) ||
        !matches_record(status_b, JOURNAL_PAGE_0_BASE + 4u + 2u * 24u,
                        3u, JOURNAL_TYPE_STATUS_B,
                        JOURNAL_STATUS_GOOD, 0x55667788u,
                        0xFFFFFFFFu)) {
        set_leds(PF_MAGENTA);
        for (;;) {}
    }

#endif

    ensure_result = journal_ensure_room();
    if (ensure_result != 0u) {
        failed_tests |= 1u << 0;
    }

    selected_after = journal_read(&active, &status_a, &status_b, &next_free);

#if BENCH_SCENARIO != 2

    if (selected_after != JOURNAL_PAGE_SELECT_1 ||
        journal_page_gen(page_1) != 7u ||
        !page_is_erased(JOURNAL_PAGE_0_BASE)) {
        failed_tests |= 1u << 1;
    }

    // Compaction copies the three winners in ACTIVE, STATUS_A, STATUS_B order.
    if ((uintptr_t)next_free != JOURNAL_PAGE_1_BASE + 4u + 3u * 24u ||
        !matches_record(active, JOURNAL_PAGE_1_BASE + 4u,
                        4u, JOURNAL_TYPE_ACTIVE_SLOT, JOURNAL_SLOT_B,
                        0u, 0xFFFFFFFFu) ||
        !matches_record(status_a, JOURNAL_PAGE_1_BASE + 4u + 24u,
                        42u, JOURNAL_TYPE_STATUS_A,
                        JOURNAL_STATUS_PENDING, 0xA1B2C3D4u,
                        STATUS_A_COUNTER) ||
        !matches_record(status_b, JOURNAL_PAGE_1_BASE + 4u + 48u,
                        3u, JOURNAL_TYPE_STATUS_B,
                        JOURNAL_STATUS_GOOD, 0x55667788u,
                        0xFFFFFFFFu)) {
        failed_tests |= 1u << 2;
    }

#else

    if (selected_after != JOURNAL_PAGE_SELECT_0 ||
        journal_page_gen(page_0) != 6u ||
        journal_page_gen(page_1) != 5u ||
        (uintptr_t)next_free != JOURNAL_PAGE_0_BASE + 4u + 4u * 24u) {
        failed_tests |= 1u << 1;
    }

    if (!matches_record(active, JOURNAL_PAGE_0_BASE + 4u + 3u * 24u,
                        4u, JOURNAL_TYPE_ACTIVE_SLOT, JOURNAL_SLOT_B,
                        0u, 0xFFFFFFFFu) ||
        !matches_record(status_a, JOURNAL_PAGE_0_BASE + 4u + 24u,
                        2u, JOURNAL_TYPE_STATUS_A,
                        JOURNAL_STATUS_PENDING, 0xA1B2C3D4u,
                        0xFFFFFFFFu) ||
        !matches_record(status_b, JOURNAL_PAGE_0_BASE + 4u + 48u,
                        3u, JOURNAL_TYPE_STATUS_B,
                        JOURNAL_STATUS_GOOD, 0x55667788u,
                        0xFFFFFFFFu)) {
        failed_tests |= 1u << 2;
    }

#endif

    set_leds(failed_tests == 0u ? PF_GREEN : PF_RED);
    for (;;) {}
}
