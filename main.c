// Christian Vanegas
// Date: 2026-09-23
// Description: Implements flash page erase for the bootloader.

#include <stdint.h>

#include "flash.h"
#include "tm4c123gh6pm.h"

#define PAGE_0   0x0001C000u
#define PAGE_1   0x0001C400u

#define PF_RED   0x02u  // PF1
#define PF_BLUE  0x04u  // PF2
#define PF_GREEN 0x08u  // PF3
#define PF_LEDS  (PF_RED | PF_BLUE | PF_GREEN)
#define PF_MAGENTA (PF_RED | PF_BLUE)
#define PF_YELLOW  (PF_RED | PF_GREEN)

enum {
    T1, T2, T3, T4, T5, T6,
    TEST_COUNT
};

// Inspect these in the debugger. T5 stores the word read from PAGE_1;
// the other entries store flash_erase_page() return values.
volatile uint32_t test_results[TEST_COUNT];
volatile uint32_t failed_tests;
volatile uint32_t precondition_ok;

static uint32_t flash_first_word(uint32_t address) {
    return *(const volatile uint32_t *)(uintptr_t)address;
}

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

int main(void) {
    setup_leds();

    uint32_t pattern_0 = flash_first_word(PAGE_0);
    uint32_t pattern_1 = flash_first_word(PAGE_1);

    if (pattern_0 == 0xFFFFFFFFu || pattern_1 == 0xFFFFFFFFu) {
        set_leds(PF_MAGENTA);
        for (;;) {}
    }
    precondition_ok = 1u;

    test_results[T1] = flash_erase_page(PAGE_0 + 4u);
    if (test_results[T1] != FLASH_ERR_INVALID_ADDRESS ||
        flash_first_word(PAGE_0) != pattern_0) {
        failed_tests |= 1u << T1;
    }

    test_results[T2] = flash_erase_page(0x00007C00u);
    if (test_results[T2] != FLASH_ERR_INVALID_ADDRESS) {
        failed_tests |= 1u << T2;
    }

    test_results[T3] = flash_erase_page(0x00040000u);
    if (test_results[T3] != FLASH_ERR_INVALID_ADDRESS) {
        failed_tests |= 1u << T3;
    }

    set_leds(PF_BLUE);
    test_results[T4] = flash_erase_page(PAGE_0);
    set_leds(0u);
    if (test_results[T4] != 0u) {
        failed_tests |= 1u << T4;
    }

    test_results[T5] = flash_first_word(PAGE_1);
    if (test_results[T5] != pattern_1) {
        failed_tests |= 1u << T5;
    }

    // PAGE_0 and PAGE_1 share protection block 24 in FMPPE1.
    // This changes runtime protection only. Do not issue FMC.COMT.
    FLASH_FMPPE1_R &= ~(1u << 24);

    test_results[T6] = flash_erase_page(PAGE_1);
    if (test_results[T6] != FLASH_FCRIS_ARIS ||
        flash_first_word(PAGE_1) != pattern_1) {
        failed_tests |= 1u << T6;
    }

    set_leds(failed_tests == 0u ? PF_GREEN : PF_RED);
    for (;;) {}
}
