#ifndef _BLISTS_INDEX_H
#define _BLISTS_INDEX_H

#include "params.h"

#define N_ADAY ((MAX_YEAR - MIN_YEAR + 1) * 366)

struct idx_message {
	unsigned long offset, size;
	unsigned char y, m, d;
};

#endif
