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

#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <iconv.h>

#include "buffer.h"
#include "encoding.h"

/* convert text from `enc' buffer to `dst' by `charset' (non-const) */
void to_main_charset(struct buffer *dst, struct buffer *enc, char *charset)
{
	char *iptr = enc->start;
	size_t inlen = enc->ptr - enc->start;
	char *p;

	/* sanitize charset string */
	p = charset;
	while ((*p >= 'a' && *p <= 'z') ||
	    (*p >= 'A' && *p <= 'Z') ||
	    (*p >= '0' && *p <= '9') ||
	    (*p == '-'))
		p++;
	if (*p == '?')
		*p = 0;
	else
		charset = UNKNOWN_CHARSET;

	if (!strcasecmp(MAIN_CHARSET, charset))
		buffer_append(dst, iptr, inlen);
	else {
		iconv_t cd = iconv_open(MAIN_CHARSET, charset);
		char out[ICONV_BUF_SIZE];

		if (cd == (iconv_t)(-1))
			cd = iconv_open(MAIN_CHARSET, UNKNOWN_CHARSET);
		assert(cd != (iconv_t)(-1));
		do {
			char *optr = out;
			size_t outlen = sizeof(out);
			int e = iconv(cd, &iptr, &inlen, &optr, &outlen);
			buffer_append(dst, out, optr - out);
			if (inlen == 0)
				break;
			if (e == -1) {
				buffer_appendc(dst, '?');
				iptr++;
				inlen--;
			}
		} while ((int)inlen > 0);
		iconv_close(cd);
	}

	enc->ptr = enc->start;
}

