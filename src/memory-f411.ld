MEMORY
{
FLASH (rx)	: ORIGIN = 0x08000000, LENGTH = 16K
/* Leave 192k reserved for now */
USERRAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 28K
RAM (rwx)      : ORIGIN = 0x20007000, LENGTH = 36K
}
