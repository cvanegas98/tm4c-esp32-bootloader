// Christian Vanegas
// Date: 2026-09-24
// Description: Implements flash page erase and write functions for the bootloader.

#include "flash.h"
#include "tm4c123gh6pm.h"

#define FLASH_FIRST_WRITABLE_ADDRESS 0x00008000u
#define FLASH_LAST_PAGE_ADDRESS      0x0003FC00u
#define FLASH_LAST_WORD_ADDRESS      0x0003FFFCu
#define FLASH_FMC_WRKEY_ALT          0x71D50000u

#define FLASH_ERROR_BITS \
    (FLASH_FCRIS_VOLTRIS | FLASH_FCRIS_INVDRIS | FLASH_FCRIS_ARIS | \
     FLASH_FCRIS_ERRIS | FLASH_FCRIS_PROGRIS)

#define FLASH_CLEAR_FLAGS \
    (FLASH_FCMISC_VOLTMISC | FLASH_FCMISC_INVDMISC | FLASH_FCMISC_AMISC | \
     FLASH_FCMISC_ERMISC | FLASH_FCMISC_PROGMISC | FLASH_FCMISC_PMISC)

// FMA must already contain the target address; FMD must also be set for WRITE.
static uint32_t flash_issue_command(uint32_t command) {
    // FCMISC is write-one-to-clear.
    FLASH_FCMISC_R = FLASH_CLEAR_FLAGS;

    uint32_t key = (FLASH_BOOTCFG_R & FLASH_BOOTCFG_KEY) ? FLASH_FMC_WRKEY : FLASH_FMC_WRKEY_ALT;

    FLASH_FMC_R = key | command;

    // Instruction fetches from flash stall during the operation.
    // The watchdog catches a controller that never finishes.
    while ((FLASH_FMC_R & command) != 0u) {}

    uint32_t errors = FLASH_FCRIS_R & FLASH_ERROR_BITS;

    // PRIS reports completion, not failure; do not leave it pending.
    FLASH_FCMISC_R = FLASH_FCMISC_PMISC;

    return errors;
}

// Requires a system clock <= 40 MHz while executing from flash (MEM#14).
// No interrupt handler may start a flash operation or change FMA/FMD/FMC.
uint32_t flash_erase_page(uint32_t address) {
    if ((address & (FLASH_PAGE_BYTES - 1u)) != 0u ||
        address < FLASH_FIRST_WRITABLE_ADDRESS ||
        address > FLASH_LAST_PAGE_ADDRESS) {
        return FLASH_ERR_INVALID_ADDRESS;
    }

    FLASH_FMA_R = address;

    uint32_t errors = flash_issue_command(FLASH_FMC_ERASE);
    if (errors != 0u) {
        return errors;
    }

    const volatile uint32_t *page = (const volatile uint32_t *)(uintptr_t)address;

    for (uint32_t i = 0; i < FLASH_PAGE_BYTES / sizeof(uint32_t); i++) {
        if (page[i] != UINT32_MAX) {
            return FLASH_ERR_VERIFY_FAILED;
        }
    }

    return 0u;
}

// Same clock and interrupt-handler requirements as flash_erase_page().
uint32_t flash_write_word(uint32_t address, uint32_t value) {
    if ((address & 3u) != 0u ||
        address < FLASH_FIRST_WRITABLE_ADDRESS ||
        address > FLASH_LAST_WORD_ADDRESS) {
        return FLASH_ERR_INVALID_ADDRESS;
    }

    const volatile uint32_t *word = (const volatile uint32_t *)(uintptr_t)address;
    uint32_t current = *word;

    if (current == value) {
        return 0u;  // Already programmed: skip.
    }

    if ((current & value) != value) {
        return FLASH_ERR_ZERO_TO_ONE;
    }

    FLASH_FMD_R = value;
    FLASH_FMA_R = address;

    uint32_t errors = flash_issue_command(FLASH_FMC_WRITE);
    if (errors != 0u) {
        return errors;
    }

    if (*word != value) {
        return FLASH_ERR_VERIFY_FAILED;
    }

    return 0u;
}
