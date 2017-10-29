/*
 * Generic dynamically-allocated auto-growing in-memory buffers.
 *
 * Written by Solar Designer <solar at openwall.com> in 2006.
 * No copyright is claimed, and the software is hereby placed in the public
 * domain.  In case this attempt to disclaim copyright and place the software
 * in the public domain is deemed null and void, then the software is
 * Copyright (c) 2006 Solar Designer and it is hereby released to the
 * general public under the following terms:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted.
 *
 * There's ABSOLUTELY NO WARRANTY, express or implied.
 */

#ifndef _BLISTS_BUFFER_H
#define _BLISTS_BUFFER_H

#include <sys/types.h>

#define BUFFER_GROW_STEP		0x8000
#define BUFFER_GROW_MAX			0x1000000

struct buffer {
	char *start, *end, *ptr;
	int error;
};

extern int buffer_init(struct buffer *buf, size_t size);
extern void buffer_free(struct buffer *buf);

extern int buffer_append(struct buffer *buf, const char *what, size_t length);
extern int buffer_appendc(struct buffer *buf, char what);
extern void buffer_appenduc(struct buffer *buf, unsigned int what);
extern int buffer_appendf(struct buffer *buf, const char *fmt, ...)
#ifdef __GNUC__
	__attribute__ ((format (printf, 2, 3)));
#else
	;
#endif

#define buffer_appends(buf, what) \
	buffer_append((buf), (what), strlen(what))

#endif
