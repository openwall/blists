#ifndef _BLISTS_PARAMS_H
#define _BLISTS_PARAMS_H

/*
 * The directory with mailboxes and index files, used by the CGI program
 * only.  (bindex assumes that full pathnames are supplied to it.)
 */
#define MAIL_SPOOL_PATH			"../../blists"

#define MIN_YEAR			1990
#define MAX_YEAR			2009

/*
 * The suffix to append to a mailbox filename to form the corresponding
 * index file's name.
 */
#define INDEX_FILENAME_SUFFIX		".idx"

/*
 * Truncate messages larger than the maximum size at the (possibly lower)
 * truncation point.
 */
#define MAX_MESSAGE_SIZE		102400
#define MAX_MESSAGE_SIZE_TRUNC		20480

/*
 * Introduce some sane limits on the mailbox size in order to prevent
 * a single huge mailbox from stopping the entire service.
 */
#define MAX_MAILBOX_MESSAGES		2097152
#define MAX_MAILBOX_BYTES		2147483647

/*
 * Locking method your system uses for user mailboxes.  It is important
 * that you set this correctly.
 *
 * *BSDs use flock(2), others typically use fcntl(2).
 */
#define LOCK_FCNTL			1
#define LOCK_FLOCK			0

/*
 * File buffer size to use while parsing the mailbox.  Can be changed.
 */
#define FILE_BUFFER_SIZE		0x10000

/*
 * The mailbox parsing code isn't allowed to truncate lines earlier than
 * this length.  Keep this at least as large as the longest header line
 * that we need to parse, but not too large for performance reasons.
 */
#define LINE_BUFFER_SIZE		0x1000

#endif
