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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "buffer.h"

int buffer_init(struct buffer *buf, size_t size)
{
	if (!size) size = BUFFER_GROW_STEP;
	buf->start = malloc(size);
	if (!buf->start) {
		buf->end = buf->ptr = buf->start;
		return buf->error = -1;
	}

	buf->end = buf->start + size;
	buf->ptr = buf->start;
	return buf->error = 0;
}

void buffer_free(struct buffer *buf)
{
	free(buf->start);
	buf->end = buf->ptr = buf->start = NULL;
	buf->error = -1;
}

static int buffer_grow(struct buffer *buf, size_t length)
{
	char *new_start;
	size_t new_size;

	if (length <= buf->end - buf->ptr) return 0;
	if (length > BUFFER_GROW_MAX || !buf->start) return buf->error = -1;

	new_size = buf->ptr - buf->start + length + BUFFER_GROW_STEP;
	if (new_size > BUFFER_GROW_MAX) return buf->error = -1;
	new_start = realloc(buf->start, new_size);
	if (!new_start) return buf->error = -1;

	buf->ptr = new_start + (buf->ptr - buf->start);
	buf->start = new_start;
	buf->end = new_start + new_size;
	return 0;
}

int buffer_append(struct buffer *buf, const char *what, size_t length)
{
	if (length > buf->end - buf->ptr &&
	    buffer_grow(buf, length))
		return -1;

	memcpy(buf->ptr, what, length);
	buf->ptr += length;
	return 0;
}

int buffer_appendc(struct buffer *buf, char what)
{
	if (buf->ptr >= buf->end && buffer_grow(buf, 1)) return -1;

	*(buf->ptr++) = what;
	return 0;
}

/* append utf-8 char */
void buffer_appenduc(struct buffer *buf, unsigned int what)
{
	if (what <= 0x007f)
		buffer_appendc(buf, what);
	else if (what <= 0x07ff) {
		buffer_appendc(buf, 0xc0 |  (what >> 6));
		buffer_appendc(buf, 0x80 |  (what        & 0x3f));
	} else if (what <= 0xffff) {
		buffer_appendc(buf, 0xe0 |  (what >> 12));
		buffer_appendc(buf, 0x80 | ((what >> 6)  & 0x3f));
		buffer_appendc(buf, 0x80 |  (what        & 0x3f));
	} else if (what <= 0x10ffff) {
		buffer_appendc(buf, 0xf0 |  (what >> 18));
		buffer_appendc(buf, 0x80 | ((what >> 12) & 0x3f));
		buffer_appendc(buf, 0x80 | ((what >> 6)  & 0x3f));
		buffer_appendc(buf, 0x80 |  (what        & 0x3f));
	} else
		buffer_appenduc(buf, 0xfffd); /* replacement character */
}

int buffer_appendf(struct buffer *buf, const char *fmt, ...)
{
	va_list args;
	size_t length, size;
	int n;

	length = 1;
	size = buf->end - buf->ptr;
	do {
		if (length > size) {
			if (buffer_grow(buf, length)) return -1;
			size = buf->end - buf->ptr;
		}

		va_start(args, fmt);
		n = vsnprintf(buf->ptr, size, fmt, args);
		va_end(args);

		if (n >= 0) {
			if (n < size) {
				buf->ptr += n;
				return 0;
			}
			length = n + 1;
		} else
			length = size << 1;
	} while (length > size);

	return buf->error = -1;
}
