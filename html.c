#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include "params.h"
#include "index.h"
#include "buffer.h"
#include "misc.h"

static void buffer_append_html(struct buffer *dst, char *what, size_t length)
{
	char *ptr, *end;
	unsigned char c;

	ptr = what;
	end = what + length;

	while (ptr < end) {
		switch ((c = (unsigned char)*ptr++)) {
		case '<':
			buffer_appends(dst, "&lt;");
			break;
		case '>':
			buffer_appends(dst, "&gt;");
			break;
		case '&':
			buffer_appends(dst, "&amp;");
			break;
		case '@':
			if (ptr - 1 > what && ptr + 3 < end &&
			    *(ptr - 2) >= ' ' && *ptr >= ' ') {
				buffer_appends(dst, "@...");
				ptr += 3;
				break;
			}
		case '\n':
		case '\t':
			buffer_appendc(dst, c);
			break;
		default:
			if ((c >= 0x20 && c <= 0x7e) || c >= 0xa0)
				buffer_appendc(dst, c);
			else
				buffer_appendc(dst, '.');
		}
	}
}

static void buffer_append_header(struct buffer *dst, char *what)
{
	buffer_append_html(dst, what, strlen(what));
	buffer_appendc(dst, '\n');
}

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
	unsigned int aday, n0, n2;
	char *list_file, *idx_file;
	off_t idx_offset;
	int fd, error, got, trunc, prev, next, seen_nl;
	idx_msgnum_t m0, m1, m1r;
	struct idx_message idx_msg[3];
	idx_off_t offset;
	idx_size_t size;
	struct buffer src, dst;
	unsigned char c;
	char *date, *from, *to, *subject, *body;

	if (y < MIN_YEAR || y > MAX_YEAR ||
	    m < 1 || m > 12 ||
	    d < 1 || d > 31 ||
	    n < 1 || n > 999999)
		return html_error("Invalid date or message number");
	aday = ((y - MIN_YEAR) * 12 + (m - 1)) * 31 + (d - 1);

	list_file = concat(MAIL_SPOOL_PATH "/", list, NULL);
	if (!list_file) return html_error(NULL);
	idx_file = concat(list_file, INDEX_FILENAME_SUFFIX, NULL);
	if (!idx_file) {
		free(list_file);
		return html_error(NULL);
	}

	fd = open(idx_file, O_RDONLY);
	error = errno;
	free(idx_file);
	if (fd < 0) {
		free(list_file);
		return html_error(error == ENOENT ?
		    "No such mailing list" : NULL);
	}

	idx_offset = aday * sizeof(idx_msgnum_t);
	error =
	    lseek(fd, idx_offset, SEEK_SET) != idx_offset ||
	    read_loop(fd, (char *)&m1, sizeof(m1)) != sizeof(m1);
	if (error || m1 < 1 || m1 >= MAX_MAILBOX_MESSAGES) {
		close(fd);
		free(list_file);
		return html_error((error || m1 > 0) ? NULL : "No such message");
	}
	m1r = m1 + n - (1 + 1); /* both m1 and n are 1-based; m1r is 0-based */
	idx_offset = (N_ADAY + 1) * sizeof(idx_msgnum_t) +
	    m1r * sizeof(idx_msg[1]);
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
		    ((unsigned int)idx_msg[0].y * 12 +
		    ((unsigned int)idx_msg[0].m - 1)) * 31 +
		    ((unsigned int)idx_msg[0].d - 1);
		idx_offset = aday * sizeof(m0);
		error =
		    lseek(fd, idx_offset, SEEK_SET) != idx_offset ||
		    read_loop(fd, (char *)&m0, sizeof(m0)) != sizeof(m0);
		if (m1 > m0)
			n0 = m1 - m0;
		else
			error = 1;
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
	size = idx_msg[1].size;

	trunc = size > MAX_MESSAGE_SIZE;
	if (trunc)
		size = MAX_MESSAGE_SIZE_TRUNC;
	if (buffer_init(&src, size)) {
		free(list_file);
		return html_error(NULL);
	}
	if (buffer_init(&dst, size)) {
		buffer_free(&src);
		free(list_file);
		return html_error(NULL);
	}

	fd = open(list_file, O_RDONLY);
	free(list_file);
	if (fd < 0) {
		buffer_free(&dst);
		buffer_free(&src);
		return html_error(NULL);
	}
	error =
	    lseek(fd, offset, SEEK_SET) != offset ||
	    read_loop(fd, src.start, size) != size;
	if (close(fd) || error) {
		buffer_free(&dst);
		buffer_free(&src);
		return html_error(NULL);
	}

	buffer_appends(&dst, "\n\n");
	if (prev)
		buffer_appendf(&dst,
		    "<a href=\"../../../%u/%02u/%02u/%u\">[&lt;prev]</a>",
		    MIN_YEAR + idx_msg[0].y, idx_msg[0].m, idx_msg[0].d, n0);
	if (next)
		buffer_appendf(&dst,
		    "%s<a href=\"../../../%u/%02u/%02u/%u\">[next&gt;]</a>",
		    prev ? " " : "",
		    MIN_YEAR + idx_msg[2].y, idx_msg[2].m, idx_msg[2].d, n2);

	if (prev || next)
		buffer_appendc(&dst, ' ');
	buffer_appends(&dst, "<a href=\"..\">[month]</a>");

	date = from = to = subject = body = NULL;
	seen_nl = 1;
	while (src.ptr < src.end - 9) {
		c = (unsigned char)*src.ptr++;

		if (seen_nl)
		switch (c) {
		case 'D':
		case 'd':
			if (!strncasecmp(src.ptr, "ate:", 4))
				date = src.ptr - 1;
			break;
		case 'F':
		case 'f':
			if (!strncasecmp(src.ptr, "rom:", 4))
				from = src.ptr - 1;
			break;
		case 'T':
		case 't':
			if (!strncasecmp(src.ptr, "o:", 2))
				to = src.ptr - 1;
			break;
		case 'S':
		case 's':
			if (!strncasecmp(src.ptr, "ubject:", 2))
				subject = src.ptr - 1;
			break;
		}

		if (seen_nl && c != '\t' && src.ptr > src.start + 1)
			*(src.ptr - 2) = '\0';

		if (c == '\n') {
			if (seen_nl) {
				body = src.ptr;
				break;
			}
			seen_nl = 1;
			continue;
		}

		seen_nl = 0;
	}
	if (src.ptr > src.start)
		*(src.ptr - 1) = '\0';

	buffer_appends(&dst, "\n<pre>\n");
	if (date)
		buffer_append_header(&dst, date);
	if (from)
		buffer_append_header(&dst, from);
	if (to)
		buffer_append_header(&dst, to);
	if (subject)
		buffer_append_header(&dst, subject);
	if (body) {
		buffer_appendc(&dst, '\n');
		buffer_append_html(&dst, body, src.end - body);
	}
	buffer_appends(&dst, "</pre>\n");

	buffer_free(&src);

	if (trunc)
		buffer_appends(&dst, "[ TRUNCATED ]<br>\n");

	if (dst.error) {
		buffer_free(&dst);
		return html_error(NULL);
	}

	write_loop(STDOUT_FILENO, dst.start, dst.ptr - dst.start);

	buffer_free(&dst);

	return 0;
}

int html_index(char *list, unsigned int y, unsigned int m)
{
	unsigned int d, n, aday, dp;
	char *idx_file;
	off_t idx_offset;
	int fd, error;
	idx_msgnum_t mn[32], mp, count, total;
	struct buffer dst;

	if (y < MIN_YEAR || y > MAX_YEAR ||
	    m < 1 || m > 12)
		return html_error("Invalid date");
	aday = ((y - MIN_YEAR) * 12 + (m - 1)) * 31;

	idx_file = concat(MAIL_SPOOL_PATH "/", list,
	    INDEX_FILENAME_SUFFIX, NULL);
	if (!idx_file)
		return html_error(NULL);

	fd = open(idx_file, O_RDONLY);
	error = errno;
	free(idx_file);
	if (fd < 0)
		return html_error(error == ENOENT ?
		    "No such mailing list" : NULL);

	idx_offset = aday * sizeof(idx_msgnum_t);
	error =
	    lseek(fd, idx_offset, SEEK_SET) != idx_offset ||
	    read_loop(fd, (char *)mn, sizeof(mn)) != sizeof(mn);
	if (close(fd) || error)
		return html_error(NULL);

	if (buffer_init(&dst, 0))
		return html_error(NULL);

	buffer_appends(&dst, "\n\n");

	buffer_appendf(&dst, "<p><h2>%04u/%02u\n</h2><p>", y, m);

	total = 0;
	dp = 0;
	mp = mn[0];
	for (d = 1; d <= 31; d++) {
		if (!mn[d]) continue;
		if (mp > 0) {
			if (mn[d] > 0)
				count = mn[d] - mp;
			else
				count = -mn[d];
			if (count <= 0) {
				buffer_free(&dst);
				return html_error(NULL);
			}
			total += count;
			if (count > 999) count = 999;
			buffer_appendf(&dst, "<b>%u</b>:", dp + 1);
			for (n = 1; n <= count; n++)
				buffer_appendf(&dst,
				    " <a href=\"%02u/%u\">%u</a>",
				    dp + 1, n, n);
			buffer_appends(&dst, "<br>\n");
		}
		mp = mn[d];
		dp = d;
	}

	if (total)
		buffer_appendf(&dst, "<p>%u message%s\n",
		    total, total == 1 ? "" : "s");
	else
		buffer_appends(&dst, "No messages\n");

	if (dst.error) {
		buffer_free(&dst);
		return html_error(NULL);
	}

	write_loop(STDOUT_FILENO, dst.start, dst.ptr - dst.start);

	buffer_free(&dst);

	return 0;
}
