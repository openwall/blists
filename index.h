#ifndef _BLISTS_INDEX_H
#define _BLISTS_INDEX_H

#include <sys/types.h>

#include "params.h"

#define N_ADAY ((MAX_YEAR - MIN_YEAR + 1) * 366)

typedef unsigned int idx_msgnum_t;
typedef off_t idx_off_t;
typedef off_t idx_size_t;
typedef unsigned char idx_ymd_t;

struct idx_message {
	idx_off_t offset;
	idx_size_t size;
	idx_ymd_t y, m, d;
};

#endif
