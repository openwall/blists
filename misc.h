/*
 * Miscellaneous system and library call wrappers.
 *
 * Copyright (c) 1998-2008,2017 Solar Designer <solar at openwall.com>
 * Copyright (c) 2017 ABC <abc at openwall.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted.
 *
 * There's ABSOLUTELY NO WARRANTY, express or implied.
 */

#ifndef _BLISTS_MISC_H
#define _BLISTS_MISC_H

#include <sys/types.h>

/*
 * Obtain or remove a lock.
 */
extern int lock_fd(int fd, int shared);
extern int unlock_fd(int fd);

/*
 * Attempts to read until EOF, and returns the number of bytes read.
 * We don't expect any signals, so even EINTR is considered an error.
 */
extern ssize_t read_loop(int fd, void *buffer, size_t count);

/*
 * Attempts to write all the supplied data.  Returns the number of bytes
 * written.  Any value that differs from the requested count means that
 * an error has occurred; if the value is -1, errno is set appropriately.
 */
extern ssize_t write_loop(int fd, const void *buffer, size_t count);

/*
 * Concatenates a variable number of strings.  The argument list must be
 * terminated with a NULL.  Returns a pointer to malloc(3)'ed memory with
 * the concatenated string, or NULL on error.
 */
extern char *concat(const char *s1, ...);

/* fprintf on stderr but only if it is a tty */
extern void logtty(const char *fmt, ...)
#ifdef __GNUC__
	__attribute__ ((format (printf, 1, 2)));
#else
	;
#endif

extern void log_percentage(off_t offset, off_t size);

#endif
