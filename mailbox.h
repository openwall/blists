/*
 * Mailbox access.
 */

#ifndef _BLISTS_MAILBOX_H
#define _BLISTS_MAILBOX_H

#define MSG_ALLOC_STEP			0x1000

/*
 * Opens, parses, and closes the mailbox.  Returns a non-zero value on error.
 */
extern int mailbox_parse(char *mailbox);

#endif
