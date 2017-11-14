/*
 * Charset encoding processing.
 *
 * Copyright (c) 2011,2014,2017 ABC <abc at openwall.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted.
 *
 * There's ABSOLUTELY NO WARRANTY, express or implied.
 */

#ifndef _BLISTS_ENCODING_H
#define _BLISTS_ENCODING_H

#include "buffer.h"

#define ENC_ICONV_BUF_SIZE		1024

extern int enc_allowed_charset(const char *charset);
extern int enc_to_utf8(struct buffer *dst, struct buffer *enc, const char *charset);
extern int enc_utf8_remove_partial(char *ptr, int *lenp);

#endif
