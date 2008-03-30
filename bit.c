#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#include "params.h"
#include "index.h"
#include "misc.h"

/*
 * Attempts to read until EOF, and returns the number of bytes read.
 * We don't expect any signals, so even EINTR is considered an error.
 */
static int read_loop(int fd, char *buffer, int count)
{
	int offset, block;

	offset = 0;
	while (count > 0) {
		block = read(fd, &buffer[offset], count);

		if (block < 0) return block;
		if (!block) return offset;

		offset += block;
		count -= block;
	}

	return offset;
}

static int bit(char *list,
	unsigned int y, unsigned int m, unsigned int d, unsigned int n)
{
	unsigned int aday;
	char *listfile, *idx, *src, *dst, *sptr, *dptr;
	unsigned char c;
	off_t idx_offset;
	int fd, error, trunc;
	unsigned int m0, m1, m1r, n0, n2;
	struct idx_message idx_msg[3];
	unsigned long offset, size_src, size_dst;

	if (y < MIN_YEAR || y > MAX_YEAR ||
	    m < 1 || m > 12 ||
	    d < 1 || d > 31 ||
	    n < 1 || n > 9999) return 1;
	aday = (y - MIN_YEAR) * 366 + (m - 1) * 31 + (d - 1);

	listfile = concat(MAIL_SPOOL_PATH "/", list, NULL); /* never freed */
	if (!listfile) return 1;
	idx = concat(listfile, INDEX_FILENAME_SUFFIX, NULL); /* never freed */
	if (!idx) return 1;

	idx_offset = aday * sizeof(m1);

	fd = open(idx, O_RDONLY);
	if (fd < 0) return 1;
	/* XXX: lock */
	error =
	    lseek(fd, idx_offset, SEEK_SET) != idx_offset ||
	    read_loop(fd, (char *)&m1, sizeof(m1)) != sizeof(m1);
	if (error) {
		close(fd);
		return 5;
	}
	if (!m1 || m1 >= MAX_MAILBOX_MESSAGES) return 1;
	m1r = m1 + n - 3;
	if (m1r > MAX_MAILBOX_MESSAGES) return 2;
	idx_offset = N_ADAY * sizeof(m1) + m1r * sizeof(idx_msg[0]);
	error =
	    lseek(fd, idx_offset, SEEK_SET) != idx_offset ||
	    read_loop(fd, (char *)&idx_msg, sizeof(idx_msg)) != sizeof(idx_msg);

	if (y - MIN_YEAR != idx_msg[1].y ||
	    m != idx_msg[1].m || d != idx_msg[1].d) {
		close(fd);
		return 3;
	}

	n0 = n - 1;
	if (!n0) {
		aday =
		    (unsigned int)idx_msg[0].y * 366 +
		    ((unsigned int)idx_msg[0].m - 1) * 31 +
		    ((unsigned int)idx_msg[0].d - 1);
		idx_offset = aday * sizeof(m0);
		error =
		    lseek(fd, idx_offset, SEEK_SET) != idx_offset ||
		    read_loop(fd, (char *)&m0, sizeof(m0)) != sizeof(m0);
		n0 = m1 - m0;
	}
	n2 = n + 1;
	if (idx_msg[2].y != idx_msg[1].y ||
	    idx_msg[2].m != m || idx_msg[2].d != d)
		n2 = 1;

	if (close(fd) || error) return 4;

	offset = idx_msg[1].offset;
	size_src = idx_msg[1].size;

	trunc = size_src > MAX_MESSAGE_SIZE;
	if (trunc)
		size_src = MAX_MESSAGE_SIZE_TRUNC;
	src = malloc(size_src);
	if (!src) return 1;
	size_dst = size_src * 10 + 1000; /* XXX */
	dst = malloc(size_dst);
	if (!dst) return 1;

	fd = open(listfile, O_RDONLY);
	if (fd < 0) return 1;
	/* XXX: lock */
	error =
	    lseek(fd, offset, SEEK_SET) != offset ||
	    read_loop(fd, src, size_src) != size_src;
	if (close(fd) || error) return 1;

	snprintf(dst, 200,
	    "\n\n"
	    "<a href=\"/lists/%s/%u/%02u/%02u/%u\">prev</a> "
	    "<a href=\"/lists/%s/%u/%02u/%02u/%u\">next</a>\n"
	    "<pre>\n",
	    list, MIN_YEAR + idx_msg[0].y, idx_msg[0].m, idx_msg[0].d, n0,
	    list, MIN_YEAR + idx_msg[2].y, idx_msg[2].m, idx_msg[2].d, n2);
	dptr = dst + strlen(dst);

	sptr = src;
	while (sptr < src + size_src)
	switch ((c = (unsigned char)*sptr++)) {
	case '<':
		memcpy(dptr, "&lt;", 4);
		dptr += 4;
		break;
	case '>':
		memcpy(dptr, "&gt;", 4);
		dptr += 4;
		break;
	case '&':
		memcpy(dptr, "&amp;", 5);
		dptr += 5;
		break;
	case '\t':
	case '\n':
		*dptr++ = c;
		break;
	default:
		if ((c >= 0x20 && c <= 0x7e) || c >= 0xa0)
			*dptr++ = c;
		else
			*dptr++ = '.';
	}

	memcpy(dptr, "</pre>\n", 7);
	dptr += 7;

	if (trunc) {
		memcpy(dptr, "[ TRUNCATED ]<br>\n", 18);
		dptr += 18;
	}

	write_loop(STDOUT_FILENO, dst, dptr - dst);

	return 0;
}

int main(int argc, char **argv)
{
	char *p, *list, c;
	unsigned int y, m, d, n;

	p = getenv("SERVER_PROTOCOL");
	if (!p || strcmp(p, "INCLUDED")) return 1;

	list = getenv("QUERY_STRING_UNESCAPED");
	if (!list) return 1;

	for (p = list; *p; p++) {
		if (*p >= 'a' && *p <= 'z') continue;
		if (p != list && *p == '-') continue;
		if (*p == '/') break;
		return 1;
	}
	*p++ = '\0';

	c = '\0';
	if (sscanf(p, "%u/%u/%u/%u%c", &y, &m, &d, &n, &c) < 4 || c) return 1;

	return bit(list, y, m, d, n);
}
