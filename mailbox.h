/*
 * Initial mbox file parsing.
 *
 * Copyright (c) 2006 Solar Designer <solar at openwall.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted.
 *
 * There's ABSOLUTELY NO WARRANTY, express or implied.
 */

#ifndef _BLISTS_MAILBOX_H
#define _BLISTS_MAILBOX_H

#define MSG_ALLOC_STEP			0x1000

/*
 * Opens, parses, and closes the mailbox.  Returns a non-zero value on error.
 */
extern int mailbox_parse(char *mailbox);

#endif
