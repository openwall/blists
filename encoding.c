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

#include <string.h>
#include <stdlib.h>
#include <iconv.h>
#include <errno.h>

#include "buffer.h"
#include "encoding.h"

#define UTF8_CHARSET			"UTF-8"
#define DEFAULT_CHARSET			"latin1"
#define UNKNOWN_CHARSET			"latin1"
#define MAX_CHARSET_LEN			70

static const char *charset_whitelist[] = {
	"us-ascii$",
	"iso-8859-",
	"utf-7$",
	"koi8-r$",
	"koi8-u$",
	"windows-",
	"cp",
	"gb2312$",
	"gbk$",
	"gb18030$",
	"big5$",
	"iso-2022-jp$",
	"utf-8$", /* redundant in enc_to_utf8(), may be needed elsewhere */
	NULL
};

static inline int simple_tolower(char ch)
{
	if (ch >= 'A' && ch <= 'Z')
		return ch + ('a' - 'A');
	return ch;
}

static int match_charset(const char *charset, const char *mask)
{
	for (; *mask; mask++, charset++) {
		if (*mask == '$')
			return !*charset;
		if (*mask != simple_tolower(*charset))
			return 0;
	}
	/* allow up to 8 digits */
	unsigned int i;
	for (i = 0; *charset && i < 8; i++, charset++) {
		if (*charset < '0' || *charset > '9')
			return 0;
	}
	return !*charset;
}

int enc_allowed_charset(const char *charset)
{
	const char **p;

	for (p = charset_whitelist; *p; p++)
		if (match_charset(charset, *p))
			return 1;
	return 0;
}

/* convert text from `enc' buffer to `dst' by `charset' */
int enc_to_utf8(struct buffer *dst, struct buffer *enc, const char *charset)
{
	char *iptr = enc->start;
	size_t inlen = enc->ptr - enc->start;
	char charset_buf[MAX_CHARSET_LEN];
	size_t i;
	const char *p;

	if (!charset)
		charset = UNKNOWN_CHARSET;

	/* sanitize charset string */
	p = charset;
	i = 0;
	while (i < sizeof(charset_buf) - 1 &&
	    ((*p >= 'a' && *p <= 'z') ||
	     (*p >= 'A' && *p <= 'Z') ||
	     (*p >= '0' && *p <= '9') ||
	     (*p == '-')))
		charset_buf[i++] = *p++;
	if (!*p || *p == '?') {
		charset_buf[i] = '\0';
		charset = charset_buf;
	} else {
		charset = UNKNOWN_CHARSET;
	}

	if (!strcasecmp(UTF8_CHARSET, charset) /* no recoding needed */ ||
	    !enc_allowed_charset(charset)) {
		buffer_append(dst, iptr, inlen);
	} else {
		iconv_t cd = iconv_open(UTF8_CHARSET, charset);
		char out[ENC_ICONV_BUF_SIZE];

		if (cd == (iconv_t)(-1))
			cd = iconv_open(UTF8_CHARSET, UNKNOWN_CHARSET);
		if (cd == (iconv_t)(-1))
			return -1;
		do {
			char *optr = out;
			size_t outlen = sizeof(out);
			size_t e = iconv(cd, &iptr, &inlen, &optr, &outlen);
			buffer_append(dst, out, optr - out);
			/* if output buffer is full (errno == E2BIG) we
			 * will just continue processing (it will be
			 * resumed on next iteration, because iconv()
			 * also updates iptr and inlen), otherwise
			 * report conversion error with REPLACEMENT
			 * CHARACTER (U+FFFD), which looks like <?>. */
			if (e == (size_t)-1 && errno != E2BIG) {
				buffer_appenduc(dst, 0xFFFD);
				iptr++;
				inlen--;
			}
		} while ((ssize_t)inlen > 0);
		iconv_close(cd);
	}

	enc->ptr = enc->start;
	return dst->error;
}

/* remove trailing partial utf8 character from string by reducing its len */
/* return how many bytes are removed, *lenp is modified to reflect new length */
int enc_utf8_remove_partial(char *ptr, int *lenp)
{
	int len;

	for (len = *lenp; len; ) {
		int s_size = 1; /* sequence size */
		unsigned char ch = *ptr;

		if (ch >= 0xf3)
			/* illegal multi-byte sequence */;
		else if (ch >= 0xf0)
			s_size = 4;
		else if (ch >= 0xe0)
			s_size = 3;
		else if (ch >= 0xc0)
			s_size = 2;
		if (len < s_size)
			break;
		len -= s_size;
		ptr += s_size;
	}

	*lenp -= len;
	return len;
}
