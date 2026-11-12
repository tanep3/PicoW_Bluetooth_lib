// Compiled only by the explicit Pico CMake integration. Never touches flash.
#include "pico/btstack_flash_bank.h"
#include "hardware/flash.h"
#ifdef pico_flash_bank_get_storage_offset_func
#error "Dynamic/custom SDK flash offset is unsupported by this fixed-layout guard"
#endif
static_assert(PICO_FLASH_BANK_STORAGE_OFFSET == PICOW_BOND_EXPECTED_OFFSET, "Conflicting bank offset");
static_assert(PICO_FLASH_BANK_TOTAL_SIZE == PICOW_BOND_EXPECTED_TOTAL, "Conflicting bank size");
static_assert(PICO_FLASH_BANK_STORAGE_OFFSET % FLASH_SECTOR_SIZE == 0, "Bank offset must be sector aligned");
static_assert(PICO_FLASH_BANK_TOTAL_SIZE >= 2 * FLASH_SECTOR_SIZE, "Need at least two sectors");
static_assert(PICO_FLASH_BANK_TOTAL_SIZE % (2 * FLASH_SECTOR_SIZE) == 0, "Need equal sector-aligned banks");
static_assert(static_cast<unsigned long long>(PICO_FLASH_BANK_STORAGE_OFFSET) + PICO_FLASH_BANK_TOTAL_SIZE <= PICO_FLASH_SIZE_BYTES, "Banks exceed configured flash");

static_assert(FLASH_PAGE_SIZE == 256, "Committed TLV requires the reviewed 256-byte physical page");
