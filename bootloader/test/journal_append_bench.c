// Christian Vanegas
// Date: 2026-09-28
// Description: On-target bench tests for journal append and page bounds.

#include <stdint.h>

#include "journal.h"
#include "tm4c123gh6pm.h"

// Set to 0 for the normal append pattern, 1 for the full-page pattern.
#define BENCH_FULL_PAGE 1

#define PF_RED     0x02u
#define PF_BLUE    0x04u
#define PF_GREEN   0x08u
#define PF_LEDS    (PF_RED | PF_BLUE | PF_GREEN)
#define PF_MAGENTA (PF_RED | PF_BLUE)
#define PF_YELLOW  (PF_RED | PF_GREEN)

#define EXPECTED_GEN_0 6u
#define EXPECTED_GEN_1 5u

volatile uint32_t append_result;
volatile uint32_t failed_tests;
volatile journal_page_select_t selected_page;
volatile uint32_t other_gen_before;
volatile uint32_t other_gen_after;

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

int main(void) {
    setup_leds();

    const void *page_0 = (const void *)(uintptr_t)JOURNAL_PAGE_0_BASE;
    const void *page_1 = (const void *)(uintptr_t)JOURNAL_PAGE_1_BASE;

    if (journal_page_gen(page_0) != EXPECTED_GEN_0 ||
        journal_page_gen(page_1) != EXPECTED_GEN_1) {
            set_leds(PF_MAGENTA);  // Wrong or missing flash pattern.
            for (;;) {}
    }

    const journal_record_t *active;
    const journal_record_t *status_a;
    const journal_record_t *status_b;
    const void *next_free;

    selected_page = journal_read(&active, &status_a, &status_b, &next_free);

#if BENCH_FULL_PAGE

    // 42 records occupy all complete slots; only 12 bytes remain.
    if (selected_page != JOURNAL_PAGE_SELECT_0 ||
        (uintptr_t)next_free != JOURNAL_PAGE_0_BASE + 4u + 42u * 24u) {
            set_leds(PF_MAGENTA);
            for (;;) {}
    }

    other_gen_before = journal_page_gen(page_1);

    append_result = journal_append(JOURNAL_TYPE_STATUS_A, JOURNAL_STATUS_GOOD, 0xAABBCCDDu);

    if (append_result != JOURNAL_ERR_NO_ROOM) {
        failed_tests |= 1u << 0;
    }

    other_gen_after = journal_page_gen(page_1);
    if (other_gen_after != other_gen_before) {
        failed_tests |= 1u << 1;
    }

#else

    // The normal pattern has four records; its highest seq is 4.
    if (selected_page != JOURNAL_PAGE_SELECT_0 ||
        active == NULL || active->seq != 4u ||
        status_a == NULL || status_a->seq != 2u ||
        status_b == NULL || status_b->seq != 3u ||
        (uintptr_t)next_free != JOURNAL_PAGE_0_BASE + 4u + 4u * 24u) {
            set_leds(PF_MAGENTA);
            for (;;) {}
    }

    append_result = journal_append(JOURNAL_TYPE_STATUS_A, JOURNAL_STATUS_GOOD, 0xAABBCCDDu);

    if (append_result != 0u) {
        failed_tests |= 1u << 0;
        show_result();  // Do not inspect a record that was not appended.
    }

    selected_page = journal_read(&active, &status_a, &status_b, &next_free);

    if (selected_page != JOURNAL_PAGE_SELECT_0) {
        failed_tests |= 1u << 1;
    }

    if (status_a == NULL ||
        status_a->seq != 5u ||
        status_a->type != JOURNAL_TYPE_STATUS_A ||
        status_a->value != JOURNAL_STATUS_GOOD ||
        status_a->image_crc != 0xAABBCCDDu ||
        status_a->boot_counter != 0xFFFFFFFFu ||
        !journal_record_is_valid(status_a)) {
            failed_tests |= 1u << 2;
    }

#endif

    show_result();
}
