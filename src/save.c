#include <stdint.h>
#include <stdbool.h>

#include <stm32f4xx_flash.h>

#define SECTOR_SIZE 16384

struct {
	uint16_t sector_num;
	uintptr_t address;
} saveslots[] = {
	{ FLASH_Sector_1, 0x08004000 },
	{ FLASH_Sector_2, 0x08008000 },
	{ FLASH_Sector_3, 0x0800C000 },
};

#define NELEMENTS(x) (sizeof(x) / sizeof(*(x)))

bool save_writesave(int slot)
{
	if (slot < 0) return false;
	if (slot > NELEMENTS(saveslots)) return false;

	FLASH_Unlock();

	if (FLASH_EraseSector(saveslots[slot].sector_num, VoltageRange_3)
			!= FLASH_COMPLETE) {
		FLASH_Lock();
		return false;
	}
	
	uintptr_t prog_base = saveslots[slot].address;
	uint32_t *ram_base = (uint32_t *) 0x20000000;

	for (int i = 0; i < SECTOR_SIZE; i += 4) {
		FLASH_ProgramWord(prog_base + i, ram_base[i/4]);
	}

	FLASH_Lock();

	return true;
}

bool save_readsave(int slot)
{
	if (slot < 0) return false;
	if (slot > NELEMENTS(saveslots)) return false;

	uint8_t *ram_base = (uint8_t *) 0x20000000;
	uint8_t *flash_base = (uint8_t *) saveslots[slot].address;

	for (int i=0; i<SECTOR_SIZE; i++) {
		ram_base[i] = flash_base[i];
	}

	return true;
}
