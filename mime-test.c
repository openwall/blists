/* Experimental unit-test for blists/mime.c.
 * gcc -o mime-test mime-test.c mime.o buffer.o encoding.o
 */

#include <stdio.h>
#include <string.h>
#include <err.h>

#include "buffer.h"
#include "encoding.h"
#include "mime.h"

void test_decode_header(char *istr, char *ostr)
{
	struct buffer src, dst;
	struct mime_ctx mime;
	size_t ilen = strlen(istr);
	size_t olen = strlen(ostr);

	if (buffer_init(&src, 1024))
		errx(1, "buffer_init(src) error\n");

	if (buffer_init(&dst, 1024))
		errx(1, "buffer_init(dst) error\n");

	if (mime_init(&mime, &src))
		errx(1, "mime_init() error\n");


	buffer_append(&src, istr, ilen);
	decode_header(&mime, src.start, ilen);

	if (mime.dst.ptr - mime.dst.start != olen)
		errx(1, "decode_header: incorrect output len (`%s' -> `%.*s' %d != %d `%s')\n",
		    istr,
		    mime.dst.ptr - mime.dst.start,
		    mime.dst.start,
		    mime.dst.ptr - mime.dst.start,
		    olen,
		    ostr);

	if (memcmp(mime.dst.start, ostr, olen) != 0)
		errx(1, "decode_header: incorrect output (`%s' -> `%.*s' != `%s')\n",
		    istr,
		    mime.dst.ptr - mime.dst.start,
		    mime.dst.start,
		    ostr);

	printf("decode_header: [%s] OK\n", istr);

	mime_free(&mime);
	buffer_free(&src);
	buffer_free(&dst);
}

void test_decode_header_inv(char *istr)
{
	char *ostr = strdup(istr);

	test_decode_header(istr, ostr);
	free(ostr);
}

void test_encoded_words(void)
{
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
	test_decode_header("=?invalud?Q?test?=",	"test"); // unknown encoding

	/* test in russian */
	test_decode_header("=?koi8-r?B?1MXT1A==?=",     "\xd1\x82\xd0\xb5\xd1\x81\xd1\x82");
	test_decode_header("=?KOI8-R?Q?=D4=C5=D3=D4?=", "\xd1\x82\xd0\xb5\xd1\x81\xd1\x82");
	test_decode_header("=?CP1251?q?=F2=E5=F1=F2?=", "\xd1\x82\xd0\xb5\xd1\x81\xd1\x82");

	/* from rfc2047 */ 
	test_decode_header("(=?ISO-8859-1?Q?a?=)",			 "(a)");
	test_decode_header("(=?ISO-8859-1?Q?a?= b)",			 "(a b)");
	test_decode_header("(=?ISO-8859-1?Q?a?= =?ISO-8859-1?Q?b?=)",	 "(ab)");
	test_decode_header("(=?ISO-8859-1?Q?a?=  =?ISO-8859-1?Q?b?=)",	 "(ab)");
	test_decode_header("(=?ISO-8859-1?Q?a?= \t =?ISO-8859-1?Q?b?=)", "(ab)");

	test_decode_header(" (=?ISO-8859-1?Q?a?= =?ISO-8859-1?Q?b?=)",  " (ab)");
	test_decode_header("x (=?ISO-8859-1?Q?a?= =?ISO-8859-1?Q?b?=)", "x (ab)");
	test_decode_header("(=?ISO-8859-1?Q?a?= x =?ISO-8859-1?Q?b?=)", "(a xb)"); // this is incorrect

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

	// not too long encoded-word (75 bytes)
	test_decode_header(
	    "=?utf-8?Q?123456789012345678901234567890123456789012345678901234567890123?=",
	              "123456789012345678901234567890123456789012345678901234567890123");
	// too long encoded-word (76 bytes)
	test_decode_header_inv(
	    "=?utf-8?Q?1234567890123456789012345678901234567890123456789012345678901234?=");
}

int main(int argc, char **argv)
{
	test_encoded_words();
	return 0;
}
