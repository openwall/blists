#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
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

int html_error(char *msg)
{
	if (!msg)
		msg = "Internal server error";

	msg = concat("\n\n<p>The request has failed: ", msg, ".\n", NULL);
	write_loop(STDOUT_FILENO, msg, strlen(msg));
	free(msg);

	return 1;
}

int html_message(char *list,
	unsigned int y, unsigned int m, unsigned int d, unsigned int n)
{
	unsigned int aday;
	char *list_file, *idx_file;
	char *src, *dst, *sptr, *dptr;
	unsigned char c;
	off_t idx_offset;
	int fd, error, got, trunc, prev, next, header;
	idx_msgnum_t m0, m1, m1r, n0, n2;
	struct idx_message idx_msg[3];
	idx_off_t offset;
	idx_size_t size_src, size_dst;

	if (y < MIN_YEAR || y > MAX_YEAR ||
	    m < 1 || m > 12 ||
	    d < 1 || d > 31 ||
	    n < 1 || n > 999999)
		return html_error("Invalid date or message number");
	aday = (y - MIN_YEAR) * 366 + (m - 1) * 31 + (d - 1);

	list_file = concat(MAIL_SPOOL_PATH "/", list, NULL);
	if (!list_file) return html_error(NULL);
	idx_file = concat(list_file, INDEX_FILENAME_SUFFIX, NULL);
	if (!idx_file) {
		free(list_file);
		return html_error(NULL);
	}

	fd = open(idx_file, O_RDONLY);
	free(idx_file);
	if (fd < 0) {
		free(list_file);
		return html_error("No such mailing list");
	}

	idx_offset = aday * sizeof(idx_msgnum_t);
	error =
	    lseek(fd, idx_offset, SEEK_SET) != idx_offset ||
	    read_loop(fd, (char *)&m1, sizeof(m1)) != sizeof(m1);
	if (error || m1 < 1 || m1 >= MAX_MAILBOX_MESSAGES) {
		close(fd);
		free(list_file);
		return html_error(NULL);
	}
	m1r = m1 + n - (1 + 1); /* both m1 and n are 1-based; m1r is 0-based */
	idx_offset = N_ADAY * sizeof(idx_msgnum_t) + m1r * sizeof(idx_msg[1]);
	prev = next = 1;
	if (m1r >= 1) {
		idx_offset -= sizeof(idx_msg[0]);
		error = lseek(fd, idx_offset, SEEK_SET) != idx_offset;
		got = error ? -1 :
		    read_loop(fd, (char *)&idx_msg, sizeof(idx_msg));
		if (got != sizeof(idx_msg)) {
			error = got != sizeof(idx_msg[0]) * 2;
			idx_msg[2] = idx_msg[1];
			next = 0;
		}
	} else {
		prev = 0;
		error = lseek(fd, idx_offset, SEEK_SET) != idx_offset;
		got = error ? -1 :
		    read_loop(fd, (char *)&idx_msg[1], sizeof(idx_msg[1]) * 2);
		if (got != sizeof(idx_msg[1]) * 2) {
			error = got != sizeof(idx_msg[1]);
			idx_msg[2] = idx_msg[1];
			next = 0;
		}
		idx_msg[0] = idx_msg[1];
	}

	n0 = n - 1;
	if (!n0 && prev && !error) {
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

	if (close(fd) || error) {
		free(list_file);
		return html_error(got ? NULL : "No such message");
	}

	n2 = n + 1;
	if (idx_msg[2].y != idx_msg[1].y ||
	    idx_msg[2].m != m || idx_msg[2].d != d)
		n2 = 1;

	if (y - MIN_YEAR != idx_msg[1].y ||
	    m != idx_msg[1].m || d != idx_msg[1].d) {
		free(list_file);
		return html_error("No such message");
	}

	offset = idx_msg[1].offset;
	size_src = idx_msg[1].size;

	trunc = size_src > MAX_MESSAGE_SIZE;
	if (trunc)
		size_src = MAX_MESSAGE_SIZE_TRUNC;
	src = malloc(size_src);
	if (!src) {
		free(list_file);
		return html_error(NULL);
	}
	size_dst = size_src * 10 + 1000; /* XXX */
	dst = malloc(size_dst);
	if (!dst) {
		free(src);
		free(list_file);
		return html_error(NULL);
	}

	fd = open(list_file, O_RDONLY);
	free(list_file);
	if (fd < 0) {
		free(dst);
		free(src);
		return html_error(NULL);
	}
	error =
	    lseek(fd, offset, SEEK_SET) != offset ||
	    read_loop(fd, src, size_src) != size_src;
	if (close(fd) || error) {
		free(dst);
		free(src);
		return html_error(NULL);
	}

	dst[0] = '\n'; dst[1] = '\n';
	dptr = dst + 2;
	if (prev) {
		snprintf(dptr, 250, /* XXX */
		    "<a href=\"/lists/%s/%u/%02u/%02u/%u\">[&lt;prev]</a>",
		    list,
		    MIN_YEAR + idx_msg[0].y, idx_msg[0].m, idx_msg[0].d, n0);
		dptr += strlen(dst);
	}
	if (next) {
		snprintf(dptr, 250, /* XXX */
		    "%s<a href=\"/lists/%s/%u/%02u/%02u/%u\">[next&gt;]</a>",
		    prev ? " " : "", list,
		    MIN_YEAR + idx_msg[2].y, idx_msg[2].m, idx_msg[2].d, n2);
		dptr += strlen(dst);
	}
	memcpy(dptr, "\n<pre>\n", 7);
	dptr += 7;

	header = 1;

	sptr = src;
	while (sptr < src + size_src) {
		if (dptr > dst + size_dst - 500) break; /* XXX */

		switch ((c = (unsigned char)*sptr++)) {
		case '<':
			if (header != 2) {
				memcpy(dptr, "&lt;", 4);
				dptr += 4;
			}
			break;
		case '>':
			if (header != 2) {
				memcpy(dptr, "&gt;", 4);
				dptr += 4;
			}
			break;
		case '&':
			if (header != 2) {
				memcpy(dptr, "&amp;", 5);
				dptr += 5;
			}
			break;
		case '\n':
			if (header == 2) {
				if (*sptr == '\t') break;
				header = 1;
			} else
				*dptr++ = c;
			if (!header) break;
			if (*sptr == '\n') {
				header = 0;
				break;
			}
			if (!strncasecmp(sptr, "From:", 5)) break;
			if (!strncasecmp(sptr, "To:", 3)) break;
			if (!strncasecmp(sptr, "Date:", 5)) break;
			if (!strncasecmp(sptr, "Subject:", 8)) break;
			header = 2;
			break;
		case '\t':
			if (header != 2) *dptr++ = c;
			break;
		default:
			if (header == 2) break;
			if ((c >= 0x20 && c <= 0x7e) || c >= 0xa0)
				*dptr++ = c;
			else
				*dptr++ = '.';
		}
	}

	free(src);

	memcpy(dptr, "</pre>\n", 7);
	dptr += 7;

	if (trunc) {
		memcpy(dptr, "[ TRUNCATED ]<br>\n", 18);
		dptr += 18;
	}

	write_loop(STDOUT_FILENO, dst, dptr - dst);

	free(dst);

	return 0;
}
