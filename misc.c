/*
 * Miscellaneous system and library call wrappers.
 * See misc.h for the descriptions.
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

int sleep_select(int sec, int usec)
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

int write_loop(int fd, char *buffer, int count)
{
	int offset, block;

	offset = 0;
	while (count > 0) {
		block = write(fd, &buffer[offset], count);

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

char *concat(char *s1, ...)
{
	va_list args;
	char *s, *p, *result;
	unsigned long l, m, n;

	m = n = strlen(s1);
	va_start(args, s1);
	while ((s = va_arg(args, char *))) {
		l = strlen(s);
		if ((m += l) < l) break;
	}
	va_end(args);
	if (s || m >= INT_MAX) return NULL;

	result = malloc(m + 1);
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
	if (s || m != n || p - result != n) {
		free(result);
		return NULL;
	}

	*p = 0;
	return result;
}
