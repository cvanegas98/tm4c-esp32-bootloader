// Christian Vanegas
// Date: 2026-09-24
// Description: Bench test for flash word programming.

#include <stdint.h>

#include "flash.h"
#include "tm4c123gh6pm.h"

#define TEST_PAGE   0x0001C000u
#define W3_ADDRESS  0x0001C010u
#define W4_ADDRESS  0x0001C020u
#define W8_ADDRESS  0x0001C030u
#define W10_ADDRESS 0x0001C034u

#define PF_RED      0x02u  // PF1
#define PF_BLUE     0x04u  // PF2
#define PF_GREEN    0x08u  // PF3
#define PF_LEDS     (PF_RED | PF_BLUE | PF_GREEN)
#define PF_YELLOW   (PF_RED | PF_GREEN)
#define PF_CYAN     (PF_BLUE | PF_GREEN)

enum {W1, W2, W3, W4, W5, W6, W7, W8, W9, W10, TEST_COUNT};

// Inspect these in the debugger. W9 is a bitmask of changed neighbors.
volatile uint32_t setup_result;
volatile uint32_t test_results[TEST_COUNT];
volatile uint32_t w4_results[3];
volatile uint32_t failed_tests;

static uint32_t flash_word(uint32_t address) {
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

    // A previous run's runtime protection needs a power-on reset.
    // Check this before erasing anything.
    if ((FLASH_FMPPE1_R & (1u << 24)) == 0u) {
        set_leds(PF_CYAN);
        for (;;) {}
    }

    setup_result = flash_erase_page(TEST_PAGE);
    if (setup_result != 0u) {
        set_leds(PF_RED);
        for (;;) {}
    }

    test_results[W1] = flash_write_word(0x0001C002u, 0x12345678u);
    if (test_results[W1] != FLASH_ERR_INVALID_ADDRESS) {
        failed_tests |= 1u << W1;
    }

    test_results[W2] = flash_write_word(0x00007FFCu, 0x12345678u);
    if (test_results[W2] != FLASH_ERR_INVALID_ADDRESS) {
        failed_tests |= 1u << W2;
    }

    test_results[W3] = flash_write_word(W3_ADDRESS, 0x12345678u);
    if (test_results[W3] != 0u ||
        flash_word(W3_ADDRESS) != 0x12345678u) {
        failed_tests |= 1u << W3;
    }

    w4_results[0] = flash_write_word(W4_ADDRESS, 0xFFFFFFFEu);
    if (w4_results[0] != 0u ||
        flash_word(W4_ADDRESS) != 0xFFFFFFFEu) {
        failed_tests |= 1u << W4;
    }

    w4_results[1] = flash_write_word(W4_ADDRESS, 0xFFFFFFFCu);
    if (w4_results[1] != 0u ||
        flash_word(W4_ADDRESS) != 0xFFFFFFFCu) {
        failed_tests |= 1u << W4;
    }

    w4_results[2] = flash_write_word(W4_ADDRESS, 0xFFFFFFF8u);
    if (w4_results[2] != 0u ||
        flash_word(W4_ADDRESS) != 0xFFFFFFF8u) {
        failed_tests |= 1u << W4;
    }
    test_results[W4] =
        w4_results[0] | w4_results[1] | w4_results[2];

    test_results[W5] = flash_write_word(W3_ADDRESS, 0x12345678u);
    if (test_results[W5] != 0u ||
        flash_word(W3_ADDRESS) != 0x12345678u) {
        failed_tests |= 1u << W5;
    }

    test_results[W6] = flash_write_word(W3_ADDRESS, 0x0000FFFFu);
    if (test_results[W6] != FLASH_ERR_ZERO_TO_ONE ||
        flash_word(W3_ADDRESS) != 0x12345678u) {
        failed_tests |= 1u << W6;
    }

    test_results[W7] = flash_write_word(W3_ADDRESS, 0xFFFFFFFFu);
    if (test_results[W7] != FLASH_ERR_ZERO_TO_ONE ||
        flash_word(W3_ADDRESS) != 0x12345678u) {
        failed_tests |= 1u << W7;
    }

    test_results[W8] = flash_write_word(W8_ADDRESS, 0xFFFFFFFFu);
    if (test_results[W8] != 0u ||
        flash_word(W8_ADDRESS) != 0xFFFFFFFFu) {
        failed_tests |= 1u << W8;
    }

    // Check the words immediately before and after W3 and W4.
    if (flash_word(W3_ADDRESS - 4u) != 0xFFFFFFFFu) {
        test_results[W9] |= 1u << 0;
    }
    if (flash_word(W3_ADDRESS + 4u) != 0xFFFFFFFFu) {
        test_results[W9] |= 1u << 1;
    }
    if (flash_word(W4_ADDRESS - 4u) != 0xFFFFFFFFu) {
        test_results[W9] |= 1u << 2;
    }
    if (flash_word(W4_ADDRESS + 4u) != 0xFFFFFFFFu) {
        test_results[W9] |= 1u << 3;
    }
    if (test_results[W9] != 0u) {
        failed_tests |= 1u << W9;
    }

    // Last: protect the 2 KB block containing the test page.
    // This changes runtime protection only; never write FMC.COMT.
    FLASH_FMPPE1_R &= ~(1u << 24);

    test_results[W10] = flash_write_word(W10_ADDRESS, 0x12345678u);
    if (test_results[W10] != FLASH_FCRIS_ARIS || flash_word(W10_ADDRESS) != 0xFFFFFFFFu) {
        failed_tests |= 1u << W10;
    }

    set_leds(failed_tests == 0u ? PF_GREEN : PF_RED);
    for (;;) {}
}
