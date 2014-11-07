/*
 * Charset encoding processing.
 *
 * Copyright (c) 2011,2014 ABC <abc at openwall.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted.
 *
 * There's ABSOLUTELY NO WARRANTY, express or implied.
 */

#ifndef _BLISTS_ENCODING_H
#define _BLISTS_ENCODING_H

#include "buffer.h"

#define ICONV_BUF_SIZE			1024
#define UTF8_CHARSET			"UTF-8"
#define DEFAULT_CHARSET			"latin1"
#define UNKNOWN_CHARSET			"latin1"

extern void encoding_to_utf8(struct buffer *dst, struct buffer *enc, char *charset);
extern int utf8_remove_trailing_partial_character(char *ptr, int *lenp);

#endif
