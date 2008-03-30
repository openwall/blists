#ifndef _BLISTS_INDEX_H
#define _BLISTS_INDEX_H

#include <sys/types.h>

#include "params.h"

#define N_ADAY ((MAX_YEAR - MIN_YEAR + 1) * 12 * 31)
#define IDX_F_HAVE_MSGID 1
#define IDX_F_HAVE_IRT 2

typedef int idx_msgnum_t;
typedef off_t idx_off_t;
typedef off_t idx_size_t;
typedef unsigned char idx_hash_t[16];
typedef unsigned char idx_ymd_t;
typedef unsigned char idx_flags_t;

struct idx_message {
	idx_off_t offset;
	idx_size_t size;
	idx_hash_t msgid_hash, irt_hash;
	struct {
		idx_msgnum_t alast;
		idx_msgnum_t pn, nn;
		idx_ymd_t py, pm, pd, ny, nm, nd;
	} t;
	idx_ymd_t y, m, d;
	idx_flags_t flags;
};

#endif
