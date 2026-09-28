// Christian Vanegas
// Date: 2026-09-28
// Description: On-target bench tests for journal boot counters.

#include <stdbool.h>
#include <stdint.h>

#include "journal.h"
#include "tm4c123gh6pm.h"

// 1: consume three attempts on page0.bin
// 2: reject ACTIVE_SLOT and a missing STATUS_B
// 3: consume after compaction; write must land on page 1
#define BENCH_SCENARIO 3

#if BENCH_SCENARIO < 1 || BENCH_SCENARIO > 3
#error "BENCH_SCENARIO must be 1, 2, or 3"
#endif

#define PF_RED     0x02u
#define PF_BLUE    0x04u
#define PF_GREEN   0x08u
#define PF_LEDS    (PF_RED | PF_BLUE | PF_GREEN)
#define PF_MAGENTA (PF_RED | PF_BLUE)
#define PF_YELLOW  (PF_RED | PF_GREEN)

volatile uint32_t failed_tests;
volatile uint32_t consume_results[3];
volatile uint32_t observed_counters[3];
volatile uint32_t observed_attempts[3];
volatile uint32_t ensure_result;
volatile journal_page_select_t selected_page;

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

static void show_result(void) {
    set_leds(failed_tests == 0u ? PF_GREEN : PF_RED);
    for (;;) {}
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

#if BENCH_SCENARIO == 2

// Keep an exact before-image of both pages for the no-write test.
static uint32_t flash_snapshot[2u * JOURNAL_PAGE_SIZE / sizeof(uint32_t)];

static void snapshot_pages(void) {
    const volatile uint32_t *page_0 = (const volatile uint32_t *)(uintptr_t)JOURNAL_PAGE_0_BASE;
    const volatile uint32_t *page_1 = (const volatile uint32_t *)(uintptr_t)JOURNAL_PAGE_1_BASE;
    const uint32_t words_per_page = JOURNAL_PAGE_SIZE / sizeof(uint32_t);

    for (uint32_t i = 0; i < words_per_page; i++) {
        flash_snapshot[i] = page_0[i];
        flash_snapshot[words_per_page + i] = page_1[i];
    }
}

static bool pages_unchanged(void) {
    const volatile uint32_t *page_0 = (const volatile uint32_t *)(uintptr_t)JOURNAL_PAGE_0_BASE;
    const volatile uint32_t *page_1 = (const volatile uint32_t *)(uintptr_t)JOURNAL_PAGE_1_BASE;
    const uint32_t words_per_page = JOURNAL_PAGE_SIZE / sizeof(uint32_t);

    for (uint32_t i = 0; i < words_per_page; i++) {
        if (page_0[i] != flash_snapshot[i] || page_1[i] != flash_snapshot[words_per_page + i]) {
            return false;
        }
    }
    return true;
}

#endif

int main(void) {
    setup_leds();

    const void *page_0 = (const void *)(uintptr_t)JOURNAL_PAGE_0_BASE;
    const void *page_1 = (const void *)(uintptr_t)JOURNAL_PAGE_1_BASE;

    const journal_record_t *active;
    const journal_record_t *status_a;
    const journal_record_t *status_b;
    const void *next_free;

    selected_page = journal_read(&active, &status_a, &status_b, &next_free);

    if (journal_page_gen(page_0) != 6u ||
        !page_is_erased(JOURNAL_PAGE_1_BASE) ||
        selected_page != JOURNAL_PAGE_SELECT_0) {
        set_leds(PF_MAGENTA);
        for (;;) {}
    }

#if BENCH_SCENARIO == 1

    // page0.bin: ACTIVE, STATUS_A, STATUS_B, ACTIVE.
    if ((uintptr_t)next_free != JOURNAL_PAGE_0_BASE + 4u + 4u * 24u ||
        (uintptr_t)status_a != JOURNAL_PAGE_0_BASE + 4u + 24u ||
        status_a->seq != 2u ||
        status_a->boot_counter != 0xFFFFFFFFu ||
        !journal_record_is_valid(status_a) ||
        status_b == NULL) {
        set_leds(PF_MAGENTA);
        for (;;) {}
    }

    static const uint32_t expected_counters[3] = {
        0xFFFFFFFEu, 0xFFFFFFFCu, 0xFFFFFFF8u
    };

    for (uint32_t i = 0; i < 3u; i++) {
        consume_results[i] = journal_consume_attempt(JOURNAL_TYPE_STATUS_A);

        selected_page = journal_read(&active, &status_a, &status_b, &next_free);

        if (selected_page != JOURNAL_PAGE_SELECT_0 ||
            (uintptr_t)status_a != JOURNAL_PAGE_0_BASE + 4u + 24u) {
            failed_tests |= 1u << 0;
            show_result();
        }

        observed_counters[i] = status_a->boot_counter;
        observed_attempts[i] = journal_attempts_used(status_a);

        if (consume_results[i] != 0u ||
            observed_counters[i] != expected_counters[i] ||
            observed_attempts[i] != i + 1u ||
            !journal_record_is_valid(status_a)) {
            failed_tests |= 1u << (i + 1u);
        }
    }

    if (!page_is_erased(JOURNAL_PAGE_1_BASE)) {
        failed_tests |= 1u << 4;
    }

#elif BENCH_SCENARIO == 2

    // page0_missing.bin has ACTIVE and STATUS_A, but no STATUS_B.
    if ((uintptr_t)next_free != JOURNAL_PAGE_0_BASE + 4u + 2u * 24u ||
        active == NULL ||
        status_a == NULL ||
        status_b != NULL) {
        set_leds(PF_MAGENTA);
        for (;;) {}
    }

    snapshot_pages();

    consume_results[0] = journal_consume_attempt(JOURNAL_TYPE_ACTIVE_SLOT);
    consume_results[1] = journal_consume_attempt(JOURNAL_TYPE_STATUS_B);

    if (consume_results[0] != JOURNAL_ERR_NO_RECORD) {
        failed_tests |= 1u << 0;
    }
    if (consume_results[1] != JOURNAL_ERR_NO_RECORD) {
        failed_tests |= 1u << 1;
    }
    if (!pages_unchanged()) {
        failed_tests |= 1u << 2;
    }

    selected_page = journal_read(&active, &status_a, &status_b, &next_free);
    if (selected_page != JOURNAL_PAGE_SELECT_0 ||
        status_b != NULL ||
        status_a == NULL ||
        !journal_record_is_valid(status_a)) {
        failed_tests |= 1u << 3;
    }

#else

    // page0_compact.bin is full; page 1 starts fully erased.
    if ((uintptr_t)next_free != JOURNAL_PAGE_0_BASE + 4u + 42u * 24u ||
        (uintptr_t)status_a != JOURNAL_PAGE_0_BASE + 4u + 41u * 24u ||
        status_a->seq != 42u ||
        status_a->boot_counter != 0xFFFFFFFFu ||
        !journal_record_is_valid(status_a)) {
        set_leds(PF_MAGENTA);
        for (;;) {}
    }

    ensure_result = journal_ensure_room();
    if (ensure_result != 0u) {
        failed_tests |= 1u << 0;
        show_result();
    }

    selected_page = journal_read(&active, &status_a, &status_b, &next_free);

    if (selected_page != JOURNAL_PAGE_SELECT_1 ||
        journal_page_gen(page_1) != 7u ||
        !page_is_erased(JOURNAL_PAGE_0_BASE) ||
        (uintptr_t)status_a != JOURNAL_PAGE_1_BASE + 4u + 24u ||
        status_a->seq != 42u ||
        status_a->boot_counter != 0xFFFFFFFFu ||
        !journal_record_is_valid(status_a)) {
        failed_tests |= 1u << 1;
        show_result();
    }

    consume_results[0] = journal_consume_attempt(JOURNAL_TYPE_STATUS_A);

    selected_page = journal_read(&active, &status_a, &status_b, &next_free);

    if (selected_page != JOURNAL_PAGE_SELECT_1 ||
        (uintptr_t)status_a != JOURNAL_PAGE_1_BASE + 4u + 24u) {
        failed_tests |= 1u << 2;
        show_result();
    }

    observed_counters[0] = status_a->boot_counter;
    observed_attempts[0] = journal_attempts_used(status_a);

    if (consume_results[0] != 0u ||
        observed_counters[0] != 0xFFFFFFFEu ||
        observed_attempts[0] != 1u ||
        !journal_record_is_valid(status_a) ||
        !page_is_erased(JOURNAL_PAGE_0_BASE)) {
        failed_tests |= 1u << 3;
    }

#endif

    show_result();
}
