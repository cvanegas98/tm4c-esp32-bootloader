// Christian Vanegas
// Date: 2026-09-24
// Description: Declares the bootloader's flash page-erase and write interfaces.

#ifndef FLASH_H
#define FLASH_H

#include <stdint.h>

#define FLASH_PAGE_BYTES             1024u
#define FLASH_ERR_INVALID_ADDRESS    0x80000000u
#define FLASH_ERR_VERIFY_FAILED      0x40000000u
#define FLASH_ERR_ZERO_TO_ONE        0x20000000u

// Return 0 on success, FLASH_ERR_* for a software failure,
// or FCRIS error bits for a hardware failure.
uint32_t flash_erase_page(uint32_t address);
uint32_t flash_write_word(uint32_t address, uint32_t value);

#endif // FLASH_H
