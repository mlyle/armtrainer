#ifndef _RANDOMUTIL_H
#define _RANDOMUTIL_H

#include <stdint.h>

uint64_t random_next();
void random_blendseed(uint32_t seed);

#endif // _RANDOMUTIL_H