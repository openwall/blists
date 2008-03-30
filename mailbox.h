/*
 * Mailbox access.
 */

#ifndef _BLISTS_MAILBOX_H
#define _BLISTS_MAILBOX_H

/*
 * Opens, parses, and closes the mailbox.  Returns a non-zero value on error.
 */
extern int mailbox_parse(char *mailbox);

#endif
