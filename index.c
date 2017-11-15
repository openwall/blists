/*
 * Copyright (c) 2014,2017 ABC <abc at openwall.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted.
 *
 * There's ABSOLUTELY NO WARRANTY, express or implied.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include "index.h"
#include "misc.h"

#define IDX_TAG "blists"
#define IDX_REVISION 2
#define IDX_ENDIANNESS 0x1234

struct idx_header {
	char tag[6];
	short revision;
	short min_year;
	short max_year;
	short endianness;
	off_t offset;
};

int idx_check_header(int fd, off_t *offset_p)
{
	struct idx_header h;

	if (read_loop(fd, &h, sizeof(h)) != sizeof(h))
		return -1;
	if (memcmp(IDX_TAG, h.tag, sizeof(h.tag)) ||
	    h.revision != IDX_REVISION ||
	    h.min_year != MIN_YEAR ||
	    h.max_year != MAX_YEAR ||
	    h.endianness != IDX_ENDIANNESS)
		return -1;
	if (offset_p)
		*offset_p = h.offset;
	return 0;
}

/* open idx file and check its validity */
int idx_open(const char *list)
{
	int fd;
	char *idx_file;
	int error;

	idx_file = concat(MAIL_SPOOL_PATH "/", list,
	    INDEX_FILENAME_SUFFIX, NULL);
	if (!idx_file) {
		errno = ENOMEM;
		return -1;
	}

	fd = open(idx_file, O_RDONLY);
	error = errno;
	free(idx_file);
	if (fd < 0) {
		errno = error;
		return -1;
	}
	if (lock_fd(fd, 1)) {
		error = errno;
		close(fd);
		errno = error;
		return -1;
	}
	if (idx_check_header(fd, NULL) == -1) {
		unlock_fd(fd);
		close(fd);
		errno = ESRCH; /* open() never returns this */
		return -1;
	}
	return fd;
}

int idx_close(int fd)
{
	unlock_fd(fd);
	return close(fd);
}

int idx_write_header(int fd, off_t offset)
{
	struct idx_header h;

	memset(&h, 0, sizeof(h));
	memcpy(h.tag, IDX_TAG, sizeof(h.tag));
	h.revision = IDX_REVISION;
	h.min_year = MIN_YEAR;
	h.max_year = MAX_YEAR;
	h.endianness = IDX_ENDIANNESS;
	h.offset = offset;
	if (lseek(fd, 0, SEEK_SET) == -1)
		return -1;
	return write_loop(fd, &h, sizeof(h)) != sizeof(h);
}

/* seek(+header) and read data */
int idx_read(int fd, off_t offset, void *buffer, int count)
{
	offset += sizeof(struct idx_header);
	if (lseek(fd, offset, SEEK_SET) != offset)
	       return -1;
	return read_loop(fd, buffer, count);
}

/* read ensuring that data is read at whole */
int idx_read_ok(int fd, off_t offset, void *buffer, int count)
{
	return idx_read(fd, offset, buffer, count) == count;
}

/* read by messages-per-day index */
int idx_read_aday_ok(int fd, int aday, void *buffer, int count)
{
	return idx_read_ok(fd, aday * sizeof(idx_msgnum_t), buffer, count);
}

/* read by msgs index */
int idx_read_msg_ok(int fd, int idx, void *buffer, int count)
{
	return idx_read_ok(fd, IDX2MSG(idx), buffer, count);
}
