/*
 * Miscellaneous system and library call wrappers.
 * See misc.h for the descriptions.
 *
 * Written by Solar Designer <solar at openwall.com> in 1998-2008.
 * No copyright is claimed, and the software is hereby placed in the public
 * domain.  In case this attempt to disclaim copyright and place the software
 * in the public domain is deemed null and void, then the software is
 * Copyright (c) 1998-2008 Solar Designer and it is hereby released to the
 * general public under the following terms:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted.
 *
 * There's ABSOLUTELY NO WARRANTY, express or implied.
 */

#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/file.h>
#include <time.h>

#include "params.h"

void logtty(const char *fmt, ...)
{
	va_list args;
	static int tty = -1;

	if (tty == -1)
		tty = isatty(2);
	if (!tty)
		return;

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fflush(stderr);
}

void log_percentage(off_t offset, off_t size)
{
	static time_t last_ts = 0;
	static off_t last_offset = 0;
	time_t now;

	if ((offset - last_offset) < 1000000)
		return;
	now = time(NULL);
	if (last_ts && (now - last_ts) < 1)
		return;
	last_offset = offset;
	last_ts = now;
	logtty(" %6.2f%%\r", (double)offset / (double)size * (double)100.0);
	fflush(stdout);
}

/*
 * A select(2)-based sleep() equivalent: no more problems with SIGALRM,
 * subsecond precision.
 */
static int sleep_select(int sec, int usec)
{
	struct timeval timeout;

	timeout.tv_sec = sec;
	timeout.tv_usec = usec;

	return select(0, NULL, NULL, NULL, &timeout);
}

int lock_fd(int fd, int shared)
{
#if LOCK_FCNTL
	struct flock l;

	memset(&l, 0, sizeof(l));
	l.l_whence = SEEK_SET;
	l.l_type = shared ? F_RDLCK : F_WRLCK;
	logtty("Waiting for the lock...\n");
	while (fcntl(fd, F_SETLKW, &l)) {
		if (errno != EBUSY) return -1;
		sleep_select(1, 0);
	}
#endif

#if LOCK_FLOCK
	logtty("Waiting for the lock...\n");
	while (flock(fd, shared ? LOCK_SH : LOCK_EX)) {
		if (errno != EBUSY) return -1;
		sleep_select(1, 0);
	}
#endif

	return 0;
}

int unlock_fd(int fd)
{
#if LOCK_FCNTL
	struct flock l;

	memset(&l, 0, sizeof(l));
	l.l_whence = SEEK_SET;
	l.l_type = F_UNLCK;
	if (fcntl(fd, F_SETLK, &l)) return -1;
#endif

#if LOCK_FLOCK
	if (flock(fd, LOCK_UN)) return -1;
#endif

	return 0;
}

/* read 'count' bytes resuming incomplete reads */
/* returns how much bytes is read or error (negative value) */
int read_loop(int fd, void *buffer, int count)
{
	int offset, block;

	offset = 0;
	while (count > 0) {
		block = read(fd, (char *)buffer + offset, count);

		if (block < 0) return block;
		if (!block) return offset;

		offset += block;
		count -= block;
	}

	return offset;
}

/* write 'count' bytes resuming incomplete writes */
int write_loop(int fd, const void *buffer, int count)
{
	int offset, block;

	offset = 0;
	while (count > 0) {
		block = write(fd, (char *)buffer + offset, count);

/* If any write(2) fails, we consider that the entire write_loop() has
 * failed to do its job.  We don't even ignore EINTR here.  We also don't
 * retry when a write(2) returns zero, as we could start eating up the
 * CPU if we did. */
		if (block < 0) return block;
		if (!block) return offset;

		offset += block;
		count -= block;
	}

/* Should be equal to the requested size, unless our kernel got crazy. */
	return offset;
}

/* allocate combined string from NULL terminated variable argument list */
char *concat(const char *s1, ...)
{
	va_list args;
	const char *s;
	char *p, *result;
	unsigned long l, m, n;

	m = n = strlen(s1);
	va_start(args, s1);
	while ((s = va_arg(args, char *))) {
		l = strlen(s);
		if ((m += l) < l) break;
	}
	va_end(args);
	if (s || m >= INT_MAX) return NULL;

	result = (char *)malloc(m + 1);
	if (!result) return NULL;

	memcpy(p = result, s1, n);
	p += n;
	va_start(args, s1);
	while ((s = va_arg(args, char *))) {
		l = strlen(s);
		if ((n += l) < l || n > m) break;
		memcpy(p, s, l);
		p += l;
	}
	va_end(args);
	if (s || m != n || p != result + n) {
		free(result);
		return NULL;
	}

	*p = 0;
	return result;
}
