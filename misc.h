/*
 * Miscellaneous system and library call wrappers.
 */

#ifndef _BLISTS_MISC_H
#define _BLISTS_MISC_H

#if 0
/*
 * A select(2)-based sleep() equivalent: no more problems with SIGALRM,
 * subsecond precision.
 */
extern int sleep_select(int sec, int usec);
#endif

/*
 * Obtain or remove a lock.
 */
extern int lock_fd(int fd, int shared);
extern int unlock_fd(int fd);

/*
 * Attempts to write all the supplied data.  Returns the number of bytes
 * written.  Any value that differs from the requested count means that
 * an error has occurred; if the value is -1, errno is set appropriately.
 */
extern int write_loop(int fd, char *buffer, int count);

/*
 * Concatenates a variable number of strings.  The argument list must be
 * terminated with a NULL.  Returns a pointer to malloc(3)'ed memory with
 * the concatenated string, or NULL on error.
 */
extern char *concat(char *s1, ...);

#endif
