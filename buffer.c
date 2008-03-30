#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "buffer.h"

int buffer_init(struct buffer *buf, size_t size)
{
	if (!size) size = BUFFER_GROW_SIZE;
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

	new_size = buf->ptr - buf->start + length + BUFFER_GROW_SIZE;
	if (new_size > BUFFER_GROW_MAX) return buf->error = -1;
	new_start = realloc(buf->start, new_size);
	if (!new_start) return buf->error = -1;

	buf->ptr = new_start + (buf->ptr - buf->start);
	buf->start = new_start;
	buf->end = new_start + new_size;
	return 0;
}

int buffer_append(struct buffer *buf, char *what, size_t length)
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

int buffer_appendf(struct buffer *buf, char *fmt, ...)
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
			length = n + 1;
			if (n < size) {
				buf->ptr += length;
				return 0;
			}
		} else
			length = size << 1;
	} while (length > size);

	return buf->error = -1;
}
