/*
 * Miscellaneous system and library call wrappers.
 * See misc.h for the descriptions.
 *
 * Written by Solar Designer <solar at openwall.com> in 1998-2008, and placed
 * in the public domain.  There's absolutely no warranty.
 */

#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/file.h>

#include "params.h"

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
	while (fcntl(fd, F_SETLKW, &l)) {
		if (errno != EBUSY) return -1;
		sleep_select(1, 0);
	}
#endif

#if LOCK_FLOCK
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
