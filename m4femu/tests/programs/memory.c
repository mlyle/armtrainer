#include <stdint.h>

struct record {
	uint16_t tag;
	uint8_t flags;
	uint8_t length;
	uint32_t value;
};

uint32_t student_main(uint32_t seed, uint32_t count, uint32_t *output)
{
	struct record records[6];
	for (uint32_t i = 0; i < 6; ++i) {
		records[i].tag = (uint16_t)(seed >> (i + 1));
		records[i].flags = (uint8_t)(seed + i * 13);
		records[i].length = (uint8_t)(count - i);
		records[i].value = seed ^ (UINT32_C(0x11111111) * (i + 1));
	}

	uint32_t checksum = 0;
	const uint8_t *bytes = (const uint8_t *)records;
	for (uint32_t i = 0; i < sizeof(records); ++i)
		checksum = (checksum << 5) - checksum + bytes[i];

	for (uint32_t left = 0, right = 5; left < right; ++left, --right) {
		struct record temporary = records[left];
		records[left] = records[right];
		records[right] = temporary;
	}

	output[0] = checksum;
	output[1] = records[0].value;
	output[2] = records[5].value;
	output[3] = ((uint32_t)records[2].tag << 16) |
		((uint32_t)records[2].flags << 8) | records[2].length;
	output[4] = records[1].value + records[4].value;
	return checksum ^ records[0].value ^ records[5].value;
}
