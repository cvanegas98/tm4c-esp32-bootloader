// Christian Vanegas
// Date: 2026-09-28
// Description: On-target bench test for reading journal flash pages.

#include <stdbool.h>
#include <stdint.h>

#include "journal.h"
#include "tm4c123gh6pm.h"

#define PF_RED     0x02u
#define PF_BLUE    0x04u
#define PF_GREEN   0x08u
#define PF_LEDS    (PF_RED | PF_BLUE | PF_GREEN)
#define PF_MAGENTA (PF_RED | PF_BLUE)
#define PF_YELLOW  (PF_RED | PF_GREEN)

volatile journal_page_select_t selected_page;
volatile uint32_t failed_tests;

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

static bool matches_record(const journal_record_t *record,
                           uint32_t expected_address,
                           uint32_t seq, uint32_t type, uint32_t value,
                           uint32_t image_crc, uint32_t record_crc) {
    if ((uintptr_t)record != expected_address) {
        return false;
    }

    return record->seq == seq &&
           record->type == type &&
           record->value == value &&
           record->image_crc == image_crc &&
           record->record_crc == record_crc &&
           record->boot_counter == 0xFFFFFFFFu;
}

int main(void) {
    setup_leds();

    const void *page_0 = (const void *)(uintptr_t)JOURNAL_PAGE_0_BASE;
    const void *page_1 = (const void *)(uintptr_t)JOURNAL_PAGE_1_BASE;

    if (journal_page_gen(page_0) != 6u ||
        journal_page_gen(page_1) != 5u) {
            set_leds(PF_MAGENTA);
            for (;;) {}
    }

    const journal_record_t *active;
    const journal_record_t *status_a;
    const journal_record_t *status_b;

    selected_page = journal_read(&active, &status_a, &status_b);

    if (selected_page != JOURNAL_PAGE_SELECT_0) {
        failed_tests |= 1u << 0;
    }

    if (!matches_record(active, JOURNAL_PAGE_0_BASE + 4u + 3u * 24u,
                        4u, JOURNAL_TYPE_ACTIVE_SLOT, JOURNAL_SLOT_B,
                        0u, 0xF420074Cu)) {
        failed_tests |= 1u << 1;
    }

    if (!matches_record(status_a, JOURNAL_PAGE_0_BASE + 4u + 1u * 24u,
                        2u, JOURNAL_TYPE_STATUS_A, JOURNAL_STATUS_PENDING,
                        0xA1B2C3D4u, 0x71E97B54u)) {
        failed_tests |= 1u << 2;
    }

    if (!matches_record(status_b, JOURNAL_PAGE_0_BASE + 4u + 2u * 24u,
                        3u, JOURNAL_TYPE_STATUS_B, JOURNAL_STATUS_GOOD,
                        0x55667788u, 0x3A98EF1Eu)) {
        failed_tests |= 1u << 3;
    }

    set_leds(failed_tests == 0u ? PF_GREEN : PF_RED);
    for (;;) {}
}