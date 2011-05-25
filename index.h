#ifndef _BLISTS_INDEX_H
#define _BLISTS_INDEX_H

#include <sys/types.h>

#include "params.h"

#define N_ADAY \
	((MAX_YEAR - MIN_YEAR + 1) * 12 * 31)
#define YMD2ADAY(y, m, d) \
	(((unsigned int)(y) * 12 + \
	((unsigned int)(m) - 1)) * 31 + \
	((unsigned int)(d) - 1))
#define IDX2IDX(a) \
	((N_ADAY + 1) * sizeof(idx_msgnum_t) + \
	(a) * sizeof(struct idx_message))

#define IDX_F_HAVE_MSGID		1
#define IDX_F_HAVE_IRT			2
#define IDX_F_FROM_TRUNC		4
#define IDX_F_SUBJECT_TRUNC		8

#define IDX_STRINGS_SIZE		160
#define IDX_SUBJECT_MINGUALEN		120

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
		idx_msgnum_t pn, nn;
		idx_ymd_t py, pm, pd, ny, nm, nd;
	} t;
	idx_ymd_t y, m, d;
	idx_flags_t flags;
	char strings[IDX_STRINGS_SIZE];
};

#endif
