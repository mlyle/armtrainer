MEMORY
{
FLASH (rx)	: ORIGIN = 0x08000000, LENGTH = 48K
	/* Leave 464k of flash reserved for now */
USERRAM (rwx)	: ORIGIN = 0x20000000, LENGTH = 32K
RAM (rwx)	: ORIGIN = 0x20008000, LENGTH = 224K
}

