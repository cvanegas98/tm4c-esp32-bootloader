// Christian Vanegas
// Date: 2026-09-23
// Description: Implements flash page erase for the bootloader.

#include "flash.h"
#include "tm4c123gh6pm.h"

#define FLASH_FIRST_WRITABLE_ADDRESS 0x00008000u
#define FLASH_LAST_PAGE_ADDRESS 0x0003FC00u
#define FLASH_FMC_WRKEY_ALT 0x71D50000u

#define FLASH_ERASE_ERROR_BITS \
    (FLASH_FCRIS_VOLTRIS | FLASH_FCRIS_INVDRIS | FLASH_FCRIS_ARIS | \
     FLASH_FCRIS_ERRIS | FLASH_FCRIS_PROGRIS)

#define FLASH_CLEAR_FLAGS \
    (FLASH_FCMISC_VOLTMISC | FLASH_FCMISC_INVDMISC | FLASH_FCMISC_AMISC | \
     FLASH_FCMISC_ERMISC | FLASH_FCMISC_PROGMISC | FLASH_FCMISC_PMISC)

// Requires a system clock <= 40 MHz while executing from flash (MEM#14).
// No interrupt handler may start a flash operation or change FMA/FMC.
uint32_t flash_erase_page(uint32_t address) {
    if ((address & (FLASH_PAGE_BYTES - 1u)) != 0u ||
        address < FLASH_FIRST_WRITABLE_ADDRESS ||
        address > FLASH_LAST_PAGE_ADDRESS) {
        return FLASH_ERR_INVALID_ADDRESS;
    }

    // FCMISC is write-one-to-clear. Remove flags from earlier operations.
    FLASH_FCMISC_R = FLASH_CLEAR_FLAGS;

    // BOOTCFG.KEY selects which write key the controller accepts.
    uint32_t key = (FLASH_BOOTCFG_R & FLASH_BOOTCFG_KEY) ? FLASH_FMC_WRKEY : FLASH_FMC_WRKEY_ALT;

    FLASH_FMA_R = address;
    FLASH_FMC_R = key | FLASH_FMC_ERASE;

    // Flash instruction fetches stall during erase, so a polling timeout cannot
    // advance during the operation. The watchdog catches a hung controller.
    while ((FLASH_FMC_R & FLASH_FMC_ERASE) != 0u) {}

    uint32_t errors = FLASH_FCRIS_R & FLASH_ERASE_ERROR_BITS;
    if (errors != 0u) {
        return errors;
    }

    // Independently confirm that every word in the page is erased.
    const volatile uint32_t *page = (const volatile uint32_t *)(uintptr_t)address;

    for (uint32_t i = 0; i < FLASH_PAGE_BYTES / sizeof(uint32_t); i++) {
        if (page[i] != UINT32_MAX) {
            return FLASH_ERR_VERIFY_FAILED;
        }
    }

    return 0u;
}
