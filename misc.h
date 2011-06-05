/*
 * Miscellaneous system and library call wrappers.
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
extern int read_loop(int fd, void *buffer, int count);

/*
 * Attempts to write all the supplied data.  Returns the number of bytes
 * written.  Any value that differs from the requested count means that
 * an error has occurred; if the value is -1, errno is set appropriately.
 */
extern int write_loop(int fd, const void *buffer, int count);

/*
 * Concatenates a variable number of strings.  The argument list must be
 * terminated with a NULL.  Returns a pointer to malloc(3)'ed memory with
 * the concatenated string, or NULL on error.
 */
extern char *concat(const char *s1, ...);

#endif
