/*
 * Copyright (c) 2006,2011 Solar Designer <solar at openwall.com>
 * Copyright (c) 2011 ABC <abc at openwall.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted.
 *
 * There's ABSOLUTELY NO WARRANTY, express or implied.
 */

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
#define IDX2MSG(a) \
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
	idx_hash_t msgid_hash;	/* Message-ID */
	idx_hash_t irt_hash;	/* In-Reply-To or last References tag */
	struct {
		idx_msgnum_t pn; /* prev */
		idx_msgnum_t nn; /* next */
		idx_ymd_t py, pm, pd;
		idx_ymd_t ny, nm, nd;
	} t; /* thread links */
	idx_ymd_t y, m, d;
	idx_flags_t flags;
	char strings[IDX_STRINGS_SIZE];
};

/* return number of messages in this day */
static inline int aday_count(const idx_msgnum_t *mn) {
	if (mn[0] < 1)
		return 0;
	else {
		if (mn[1] <= 0)
			return -mn[1];
		else
			return mn[1] - mn[0];
	}
}

extern int idx_check_header(int fd, off_t *offset_p);
extern int idx_open(const char *idx_file);
extern int idx_close(int fd);
extern int idx_write_header(int fd, off_t offset);
extern int idx_read(int fd, off_t offset, void *buffer, int count);
extern int idx_read_ok(int fd, off_t offset, void *buffer, int count);
extern int idx_read_aday_ok(int fd, int aday, void *buffer, int count);
extern int idx_read_msg_ok(int fd, int idx, void *buffer, int count);

#endif
