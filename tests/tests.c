/*
 * Experimental unit-test for blists/mime.c.
 *
 * Copyright (c) 2017 ABC <abc at openwall.com>
 * Copyright (c) 2017 Solar Designer <solar at openwall.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted.
 *
 * There's ABSOLUTELY NO WARRANTY, express or implied.
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <err.h>

#include "../buffer.h"
#include "../encoding.h"
#include "../mime.h"
#include "../misc.h"

#include "../buffer.c"
#include "../encoding.c"
#include "../mime.c"
#include "../misc.c"

static void test_decode_header(char *istr, char *ostr)
{
	struct buffer src, dst;
	struct mime_ctx mime;
	size_t ilen = strlen(istr);
	size_t olen = strlen(ostr);

	if (buffer_init(&src, 1024))
		errx(1, "  buffer_init(src) error\n");

	if (buffer_init(&dst, 1024))
		errx(1, "  buffer_init(dst) error\n");

	if (mime_init(&mime, &src))
		errx(1, "  mime_init() error\n");

	buffer_append(&src, istr, ilen);
	decode_header(&mime, src.start, ilen);

	if (mime.dst.ptr - mime.dst.start != olen || memcmp(mime.dst.start, ostr, olen))
		errx(1, "  decode_header: incorrect output (`%s' -> `%.*s' [%llu] vs `%s' [%llu])\n",
		    istr,
		    (int)(mime.dst.ptr - mime.dst.start), /* hack only suitable for tests */
		    mime.dst.start, /* not NUL terminated; works due to the hack above */
		    (unsigned long long)(mime.dst.ptr - mime.dst.start),
		    ostr,
		    (unsigned long long)olen);

	printf("  decode_header: [%s] OK\n", istr);

	mime_free(&mime);
	buffer_free(&src);
	buffer_free(&dst);
}

static void test_decode_header_inv(char *istr)
{
	char *ostr = strdup(istr);

	test_decode_header(istr, ostr);
	free(ostr);
}

static void test_encoded_words(void)
{
	printf(" Test decode_header(encoded-words)\n");
	/* test encoded-word */
	test_decode_header("test",			"test");
	test_decode_header("=?utf-8?Q?test?=",		"test");
	test_decode_header("=?cp1251?Q?test?=",		"test");
	test_decode_header("=?cp1252?q?=74=65=73=74?=",	"test");
	test_decode_header("=?koi8-r?B?dGVzdA==?=",	"test");
	test_decode_header("=?Utf-8?Q?test=20?=",	"test ");
	test_decode_header("=?Utf-8?Q?test=20?= ",	"test  ");
	test_decode_header("=?Utf-8?Q?=20test?= ",	" test ");
	test_decode_header(" =?Utf-8?Q?=20test?=",	"  test");
	test_decode_header("==?utf-8?Q?test?==",	"=test=");
	test_decode_header("=?invalid?Q?test?=",	"test"); // unknown encoding

	/* encoded text is "test" in Russian */
	test_decode_header("=?koi8-r?B?1MXT1A==?=",     "\xd1\x82\xd0\xb5\xd1\x81\xd1\x82");
	test_decode_header("=?KOI8-R?Q?=D4=C5=D3=D4?=", "\xd1\x82\xd0\xb5\xd1\x81\xd1\x82");
	test_decode_header("=?CP1251?q?=F2=E5=F1=F2?=", "\xd1\x82\xd0\xb5\xd1\x81\xd1\x82");

	/* truncations of the above to test other cases in base64 decoding */
	test_decode_header("=?koi8-r?B?1MU=?=",         "\xd1\x82\xd0\xb5");
	test_decode_header("=?koi8-r?B?1MXT?=",         "\xd1\x82\xd0\xb5\xd1\x81");

	/* from rfc2047 */
	test_decode_header("(=?ISO-8859-1?Q?a?=)",			 "(a)");
	test_decode_header("(=?ISO-8859-1?Q?a?= b)",			 "(a b)");
	test_decode_header("(=?ISO-8859-1?Q?a?= =?ISO-8859-1?Q?b?=)",	 "(ab)");
	test_decode_header("(=?ISO-8859-1?Q?a?=  =?ISO-8859-1?Q?b?=)",	 "(ab)");
	test_decode_header("(=?ISO-8859-1?Q?a?= \t =?ISO-8859-1?Q?b?=)", "(ab)");

	test_decode_header(" (=?ISO-8859-1?Q?a?= =?ISO-8859-1?Q?b?=)",	 " (ab)");
	test_decode_header("x (=?ISO-8859-1?Q?a?= =?ISO-8859-1?Q?b?=)",	 "x (ab)");
	test_decode_header("(=?ISO-8859-1?Q?a?= x =?ISO-8859-1?Q?b?=)",	 "(a x b)");

	test_decode_header("(=?ISO-8859-1?Q?a_b?=)",			 "(a b)");
	test_decode_header("(=?ISO-8859-1?Q?a?= =?ISO-8859-2?Q?_b?=)",	 "(a b)");

	/* test improperly encoded mime-words */
	test_decode_header_inv("=??utf-8?Q?test?=");	// duplicated '?'
	test_decode_header_inv("=?utf-8??Q?test?=");
	test_decode_header_inv("=?utf-8?Q??test?=");
	test_decode_header_inv("=?utf-8?Q?test?\?=");
	test_decode_header_inv("=?u\tf-8?Q?test?=");	// invalid characters
	test_decode_header_inv("=?uf-8?\t?test?=");
	test_decode_header_inv("=?utf-8?Q?\test?=");
	test_decode_header_inv("=?utf-8?QQ?test?=");	// invalid encodings
	test_decode_header_inv("=?utf-8?Z?test?=");
	test_decode_header_inv("=??Q?test?=");		// zero len
	test_decode_header_inv("=?utf-8??test?=");
	test_decode_header_inv("=?utf-8?Q?\?=");

	/* not too long encoded-word (75 bytes) */
	test_decode_header(
	    "=?utf-8?Q?123456789012345678901234567890123456789012345678901234567890123?=",
	              "123456789012345678901234567890123456789012345678901234567890123");
#if 0
	/* too long encoded-word (76 bytes) */
	test_decode_header_inv(
	    "=?utf-8?Q?1234567890123456789012345678901234567890123456789012345678901234?=");
#endif
}

static void test_process_header()
{
	struct buffer src, dst;
	struct mime_ctx mime;
	struct mime_entity *entity;
	char *istr;

	printf(" Test process_header()\n");

	if (buffer_init(&src, 1024))
		errx(1, "  buffer_init(src) error\n");

	if (buffer_init(&dst, 1024))
		errx(1, "  buffer_init(dst) error\n");

	if (mime_init(&mime, &src))
		errx(1, "  mime_init() error\n");

	/* test #1 */
	istr  = "Content-Type: text/x-log; charset=US-ASCII; name=log\n"
		"Content-Transfer-Encoding: quoted-printable\n";
	buffer_append(&src, istr, strlen(istr) + 1);
	src.ptr = src.start;
	mime_decode_header(&mime);
	mime_decode_header(&mime);
	entity = mime.entities;
	if (!entity)
		errx(1, "  entities is NULL\n");
	if (!entity->type || strcmp(entity->type, "text/x-log"))
		errx(1, "  type is wrong (%s)\n", entity->type);
	if (!entity->charset || strcmp(entity->charset, "US-ASCII"))
		errx(1, "  charset is wrong (%s)\n", entity->charset);
	if (!entity->filename || strcmp(entity->filename, "log"))
		errx(1, "  filename is wrong (%s)\n", entity->filename);
	if (entity->disposition != CONTENT_UNSET)
		errx(1, "  disposition is wrong (%d)\n",
		    entity->disposition);
	if (!entity->encoding ||
	    strcmp(entity->encoding, "quoted-printable"))
		errx(1, "  encoding is wrong (%s)\n", entity->encoding);
	printf("  Test #1 Content-Type [text]: OK\n");

	/* test #2 */
	src.ptr = src.start;
	istr  = "content-type: multipart/signed;"
		" Charset=\"us-ascii\";"
		" Boundary=\"=BOUNDARY=\"\n";
	buffer_append(&src, istr, strlen(istr) + 1);
	src.ptr = src.start;
	mime_decode_header(&mime);
	entity = mime.entities;
	if (!entity->type || strcmp(entity->type, "multipart/signed"))
		errx(1, "  type is wrong (%s)\n", entity->type);
	if (!entity->charset || strcmp(entity->charset, "us-ascii"))
		errx(1, "  charset is wrong (%s)\n", entity->charset);
	if (!entity->boundary || strcmp(entity->boundary, "=BOUNDARY="))
		errx(1, "  boundary is wrong (%s)\n", entity->boundary);
	if (!entity->filename)
		errx(1, "  filename is wrong (%s)\n", entity->filename);
	if (entity->disposition != CONTENT_UNSET)
		errx(1, "  disposition is wrong (%d)\n",
		    entity->disposition);
	printf("  Test #2 Content-Type [multipart]: OK\n");

	/* test #3 */
	src.ptr = src.start;
	istr  = "Content-Disposition: attachment; filename=smime.p7s\n";
	buffer_append(&src, istr, strlen(istr) + 1);
	src.ptr = src.start;
	mime_decode_header(&mime);
	entity = mime.entities;
	if (!entity->type || strcmp(entity->type, "multipart/signed"))
		errx(1, "  type is wrong (%s)\n", entity->type);
	if (!entity->charset || strcmp(entity->charset, "us-ascii"))
		errx(1, "  charset is wrong (%s)\n", entity->charset);
	if (!entity->boundary || strcmp(entity->boundary, "=BOUNDARY="))
		errx(1, "  boundary is wrong (%s)\n", entity->boundary);
	if (!entity->filename || strcmp(entity->filename, "smime.p7s"))
		errx(1, "  filename is wrong (%s)\n", entity->filename);
	if (entity->disposition != CONTENT_ATTACHMENT)
		errx(1, "  disposition is wrong (%d)\n",
		    entity->disposition);
	printf("  Test #3 Content-Disposition [filename]: OK\n");

	mime_free(&mime);
	buffer_free(&src);
	buffer_free(&dst);
}

static void test_multipart()
{
	struct buffer src;
	struct mime_ctx mime;
	struct stat stat;
	char *body, *bend = NULL;
	size_t size;
	unsigned int part;

	printf(" Test mime decoding(multipart)\n");
	int fd = open("multipart.eml", O_RDONLY);
	if (fd == -1)
		err(1, "multipart.eml");
	if (fstat(fd, &stat) == -1)
		err(1, "multipart.eml");
	size = stat.st_size;
	if (buffer_init(&src, size))
		errx(1, "  buffer_init(src) error\n");
	if (read_loop(fd, src.start, size) != size)
		errx(1, "multipart.eml read error");
	if (mime_init(&mime, &src))
		errx(1, "  mime_init() error\n");

	body = NULL;
	while (src.end - src.ptr > 9 && *src.ptr != '\n') {
		switch (*src.ptr) {
			case 'C':
			case 'c':
				mime_decode_header(&mime);
				continue;
		}
		mime_skip_header(&mime);
	}
	if (*src.ptr == '\n')
	       	body = ++src.ptr;

	part = 0;
	do {
		char *text;
		char *ctype;
		char *charset;

		if (mime.entities->boundary) {
			body = mime_next_body_part(&mime);
			if (!body || body >= src.end)
				break;
			body = mime_next_body(&mime);
		}
		if (mime.entities->boundary)
			body = NULL;
		else
			part++;
		switch (part) {
		case 1:
			text    = "header\n";
			ctype   = "text/plain";
			charset = "ISO-8859-7";
			break;
		case 2:
			text    = "attachment\n";
			ctype   = "text/x-patch";
			charset = "windows-1251";
			break;
		case 3:
			text    = "footer\n";
			ctype   = "text/plain";
			charset = "us-ascii";
			break;
		default:
			errx(1, "  invlid mime section\n");
		}
		if (body) {
			if (strcmp(charset, mime.entities->charset))
				errx(1, "  mime decoding error (wrong charset %s)\n",
				    mime.entities->charset);
			if (strcmp(ctype, mime.entities->type))
				errx(1, "  mime decoding error (wrong type %s)\n",
				    mime.entities->type);

			/* frees mime.entities struct */
			body = mime_decode_body(&mime, RECODE_NO, NULL);
			if (!body)
				break;
			bend = src.ptr;
		} else {
			bend = mime_skip_body(&mime);
			if (!bend)
				break;
			continue;
		}

		if (strncmp(text, body, strlen(text)))
			errx(1, "  mime decoding error (wrong content)\n");
		else
			printf("  mime decode part #%d OK\n", part);

		mime.dst.ptr = body;
	} while (bend < src.end && mime.entities);

	if (part != 3)
		errx(1, "  mime decoding error (too few secrions)\n");
	buffer_free(&src);
	mime_free(&mime);
}

int main(int argc, char **argv)
{
	printf("Unit-test for blists\n");
	test_encoded_words();
	test_process_header();
	test_multipart();
	printf("Success\n");
	return 0;
}
