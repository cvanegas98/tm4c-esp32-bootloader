// Christian Vanegas
// Date: 2026-09-23
// Description: Declares the bootloader's flash page-erase interface.

#ifndef FLASH_H
#define FLASH_H

#include <stdint.h>

#define FLASH_PAGE_BYTES             1024u
#define FLASH_ERR_INVALID_ADDRESS    0x80000000u
#define FLASH_ERR_VERIFY_FAILED      0x40000000u

// Erases one 1 KB page. Returns 0 on success, FLASH_ERR_* for a software
// failure, or FCRIS error bits for a hardware failure.
uint32_t flash_erase_page(uint32_t address);

#endif // FLASH_H
