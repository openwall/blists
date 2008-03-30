#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include "params.h"
#include "index.h"
#include "buffer.h"
#include "mime.h"
#include "misc.h"
#include "html.h"

int html_flags = HTML_BODY;

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
			    *(ptr - 2) > ' ' && *ptr > ' ' &&
			    *(ptr + 1) > ' ' && *(ptr + 2) > ' ') {
				buffer_appends(dst, "@...");
				ptr += 3;
				break;
			}
		case '\t':
		case '\n':
			buffer_appendc(dst, c);
		case '\r':
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

	if (html_flags & HTML_HEADER)
		msg = concat("\n<title>The request has failed: ", msg,
		    "</title>\n"
		    "<meta name=\"robots\" content=\"noindex\">\n", NULL);
	else
		msg = concat("\n<p>The request has failed: ", msg,
		    ".\n", NULL);

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
	int fd, error, got, trunc, prev, next;
	idx_msgnum_t m0, m1, m1r;
	struct idx_message idx_msg[3];
	idx_off_t offset;
	idx_size_t size;
	struct buffer src, dst;
	struct mime_ctx mime;
	char *p, *q, *date, *from, *to, *subject, *body, *bend;

	if (y < MIN_YEAR || y > MAX_YEAR ||
	    m < 1 || m > 12 ||
	    d < 1 || d > 31 ||
	    n < 1 || n > 999999)
		return html_error("Invalid date or message number");
	aday = YMD2ADAY(y - MIN_YEAR, m, d);

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
	    lock_fd(fd, 1) ||
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
		aday = YMD2ADAY(idx_msg[0].y, idx_msg[0].m, idx_msg[0].d);
		idx_offset = aday * sizeof(m0);
		error =
		    lseek(fd, idx_offset, SEEK_SET) != idx_offset ||
		    read_loop(fd, (char *)&m0, sizeof(m0)) != sizeof(m0);
		if (m1 > m0)
			n0 = m1 - m0;
		else
			error = 1;
	}

	if (unlock_fd(fd)) error = 1;

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
	if (close(fd) || error || mime_init(&mime, &src)) {
		buffer_free(&dst);
		buffer_free(&src);
		return html_error(NULL);
	}

	date = from = to = subject = body = NULL;
	while (src.end - src.ptr > 9 && *src.ptr != '\n') {
		switch (*src.ptr) {
		case 'D':
		case 'd':
			if (!strncasecmp(src.ptr, "Date:", 5)) {
				date = mime_decode_header(&mime);
				continue;
			}
			break;
		case 'F':
		case 'f':
			if (!strncasecmp(src.ptr, "From:", 5)) {
				from = mime_decode_header(&mime);
				continue;
			}
			break;
		case 'T':
		case 't':
			if (!strncasecmp(src.ptr, "To:", 3)) {
				to = mime_decode_header(&mime);
				continue;
			}
			break;
		case 'S':
		case 's':
			if (!strncasecmp(src.ptr, "Subject:", 8)) {
				subject = mime_decode_header(&mime);
				continue;
			}
			break;
		case 'C':
		case 'c':
			mime_decode_header(&mime);
			continue;
		}
		mime_skip_header(&mime);
	}
	if (*src.ptr == '\n') body = ++src.ptr;

	if ((p = subject))
	while ((p = strchr(p, '['))) {
		if (strncasecmp(++p, list, strlen(list))) continue;
		q = p + strlen(list);
		if (*q != ']') continue;
		if (*++q == ' ') q++;
		memmove(--p, q, strlen(q) + 1);
	}

	buffer_appends(&dst, "\n");

	if (html_flags & HTML_HEADER) {
		buffer_appends(&dst, "<title>");
		buffer_append_html(&dst, list, strlen(list));
		if (subject && strlen(subject) > 9) {
			buffer_appends(&dst, " - ");
			buffer_append_html(&dst,
			    subject + 9, strlen(subject + 9));
		}
		buffer_appends(&dst, "</title>\n");
	}

	if (html_flags & HTML_BODY) {
		if (prev) {
			buffer_appends(&dst, "<a href=\"");
			if (n == 1)
				buffer_appendf(&dst, "../../../%u/%02u/%02u/",
				    MIN_YEAR + idx_msg[0].y,
				    idx_msg[0].m, idx_msg[0].d);
			buffer_appendf(&dst, "%u\">[&lt;prev]</a> ", n0);
		}
		if (next) {
			buffer_appends(&dst, "<a href=\"");
			if (n2 == 1)
				buffer_appendf(&dst, "../../../%u/%02u/%02u/",
				    MIN_YEAR + idx_msg[2].y,
				    idx_msg[2].m, idx_msg[2].d);
			buffer_appendf(&dst, "%u\">[next&gt;]</a> ", n2);
		}
		if (idx_msg[1].t.pn) {
			buffer_appends(&dst, "<a href=\"");
			if (idx_msg[1].t.py != idx_msg[1].y ||
			    idx_msg[1].t.pm != idx_msg[1].m ||
			    idx_msg[1].t.pd != idx_msg[1].d)
				buffer_appendf(&dst, "../../../%u/%02u/%02u/",
				    MIN_YEAR + idx_msg[1].t.py,
				    idx_msg[1].t.pm, idx_msg[1].t.pd);
			buffer_appendf(&dst, "%u\">[&lt;thread-prev]</a> ",
			    idx_msg[1].t.pn);
		}
		if (idx_msg[1].t.nn) {
			buffer_appends(&dst, "<a href=\"");
			if (idx_msg[1].t.ny != idx_msg[1].y ||
			    idx_msg[1].t.nm != idx_msg[1].m ||
			    idx_msg[1].t.nd != idx_msg[1].d)
				buffer_appendf(&dst, "../../../%u/%02u/%02u/",
				    MIN_YEAR + idx_msg[1].t.ny,
				    idx_msg[1].t.nm, idx_msg[1].t.nd);
			buffer_appendf(&dst, "%u\">[thread-next&gt;]</a> ",
			    idx_msg[1].t.nn);
		}
		buffer_appends(&dst,
		    "<a href=\"..\">[month]</a>"
		    " <a href=\"../..\">[year]</a>"
		    " <a href=\"../../..\">[list]</a>\n");

		buffer_appends(&dst,
		    "<pre style=\"white-space: pre-wrap\">\n");
		if (date)
			buffer_append_header(&dst, date);
		if (from)
			buffer_append_header(&dst, from);
		if (to)
			buffer_append_header(&dst, to);
		if (subject)
			buffer_append_header(&dst, subject);
		do {
			if (mime.entities->boundary) {
				body = mime_next_body_part(&mime);
				if (!body || body >= src.end) break;
				body = mime_next_body(&mime);
			}
			if (mime.entities->boundary)
				body = NULL;
			else
			if (strncasecmp(mime.entities->type, "text/", 5) ||
			    !strcasecmp(mime.entities->type, "text/html")) {
				buffer_appends(&dst, "\n[ CONTENT OF TYPE ");
				buffer_append_html(&dst, mime.entities->type,
				    strlen(mime.entities->type));
				buffer_appends(&dst, " SKIPPED ]\n");
				body = NULL;
			}
			if (body) {
				body = mime_decode_body(&mime);
				if (!body) break;
				bend = src.ptr;
			} else {
				bend = mime_skip_body(&mime);
				if (!bend) break;
				continue;
			}
			buffer_appendc(&dst, '\n');
			buffer_append_html(&dst, body, mime.dst.ptr - body);
			mime.dst.ptr = body;
		} while (bend < src.end && mime.entities);
		buffer_appends(&dst, "</pre>\n");

		if (trunc)
			buffer_appends(&dst, "[ TRUNCATED ]\n");
	}

	buffer_free(&src);

	if (mime.dst.error || dst.error) {
		mime_free(&mime);
		buffer_free(&dst);
		return html_error(NULL);
	}

	mime_free(&mime);

	write_loop(STDOUT_FILENO, dst.start, dst.ptr - dst.start);

	buffer_free(&dst);

	return 0;
}

int html_month_index(char *list, unsigned int y, unsigned int m)
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
	    lock_fd(fd, 1) ||
	    lseek(fd, idx_offset, SEEK_SET) != idx_offset ||
	    read_loop(fd, (char *)mn, sizeof(mn)) != sizeof(mn);
	if (unlock_fd(fd)) error = 1;
	if (close(fd) || error || buffer_init(&dst, 0))
		return html_error(NULL);

	buffer_appends(&dst, "\n");

	if (html_flags & HTML_HEADER) {
		buffer_appends(&dst, "<title>");
		buffer_append_html(&dst, list, strlen(list));
		buffer_appendf(&dst, " mailing list - %u/%02u</title>\n", y, m);
	}

	if (html_flags & HTML_BODY) {
		buffer_appends(&dst,
		    "<a href=\"..\">[year]</a>"
		    " <a href=\"../..\">[list]</a>\n");

		buffer_appends(&dst, "<p><h2>");
		buffer_append_html(&dst, list, strlen(list));
		buffer_appendf(&dst, " mailing list - %u/%02u</h2>\n", y, m);

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
				if (!total)
					buffer_appends(&dst,
					    "<p>Messages by day:\n<p>\n");
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
			buffer_appends(&dst, "<p>No messages\n");
	}

	if (dst.error) {
		buffer_free(&dst);
		return html_error(NULL);
	}

	write_loop(STDOUT_FILENO, dst.start, dst.ptr - dst.start);

	buffer_free(&dst);

	return 0;
}

int html_year_index(char *list, unsigned int y)
{
	unsigned int min_y, max_y, m, d, d1, aday, rday;
	char *idx_file;
	off_t idx_offset;
	int fd, error;
	idx_msgnum_t *mn, mp, count, monthly_total, yearly_total, total;
	size_t mn_size;
	struct buffer dst;

	aday = 0;
	mn_size = (N_ADAY + 1) * sizeof(idx_msgnum_t);
	min_y = MIN_YEAR;
	max_y = MAX_YEAR;
	if (y) {
		if (y < min_y || y > max_y)
			return html_error("Invalid date");
		aday = (y - min_y) * (12 * 31);
		mn_size = (12 * 31 + 1) * sizeof(idx_msgnum_t);
		min_y = max_y = y;
	}

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

	mn = malloc(mn_size);
	if (!mn) {
		close(fd);
		return html_error(NULL);
	}

	idx_offset = aday * sizeof(idx_msgnum_t);
	error =
	    lock_fd(fd, 1) ||
	    (idx_offset && lseek(fd, idx_offset, SEEK_SET) != idx_offset) ||
	    read_loop(fd, (char *)mn, mn_size) != mn_size;
	if (unlock_fd(fd)) error = 1;
	if (close(fd) || error || buffer_init(&dst, 0)) {
		free(mn);
		return html_error(NULL);
	}

	buffer_appends(&dst, "\n");

	if (html_flags & HTML_HEADER) {
		buffer_appends(&dst, "<title>");
		buffer_append_html(&dst, list, strlen(list));
		buffer_appends(&dst, " mailing list");
		if (min_y == max_y)
			buffer_appendf(&dst, " - %u", y);
		buffer_appends(&dst, "</title>\n");
	}

	if (html_flags & HTML_BODY) {
		if (min_y == max_y)
			buffer_appends(&dst, "<a href=\"..\">[list]</a>\n");

		buffer_appends(&dst, "<p><h2>");
		buffer_append_html(&dst, list, strlen(list));
		buffer_appends(&dst, " mailing list");
		if (min_y == max_y)
			buffer_appendf(&dst, " - %u", y);
		buffer_appends(&dst, "</h2>\n");

		total = 0;
		rday = (max_y - min_y + 1) * (12 * 31) + (31 + 1);
		for (y = max_y; y >= min_y; y--) {
			yearly_total = 0;
			for (m = 12; m >= 1; m--) {
				d1 = 0; /* never used */
				monthly_total = 0;
				rday -= 2 * 31;
				mp = mn[rday - 1];
				for (d = 1; d <= 31; d++, rday++) {
					if (!mn[rday]) continue;
					if (mp > 0) {
						if (mn[rday] > 0)
							count = mn[rday] - mp;
						else
							count = -mn[rday];
						if (count <= 0) {
							buffer_free(&dst);
							free(mn);
							return html_error(NULL);
						}
						d1 = d;
						monthly_total += count;
					}
					mp = mn[rday];
				}
				if (monthly_total) {
					if (!total && !yearly_total)
						buffer_appends(&dst,
						    "<p>Messages by month:\n"
						    "<p>\n");
					yearly_total += monthly_total;
					buffer_appends(&dst, "<a href=\"");
					if (min_y != max_y)
						buffer_appendf(&dst, "%u/", y);
					buffer_appendf(&dst,
					    "%02u/\">%u/%02u</a>: ",
					    m, y, m);
					if (monthly_total != 1) {
						buffer_appendf(&dst,
						    "%u messages<br>\n",
						    monthly_total);
						continue;
					}
					buffer_appends(&dst, "<a href=\"");
					if (min_y != max_y)
						buffer_appendf(&dst, "%u/", y);
					buffer_appendf(&dst,
					    "%02u/%02u/1\">1 message</a><br>\n",
					    m, d1);
				}
			}
			total += yearly_total;
		}

		free(mn);

		if (total)
			buffer_appendf(&dst, "<p>%u message%s\n",
			    total, total == 1 ? "" : "s");
		else
			buffer_appends(&dst, "<p>No messages\n");
	}

	if (dst.error) {
		buffer_free(&dst);
		return html_error(NULL);
	}

	write_loop(STDOUT_FILENO, dst.start, dst.ptr - dst.start);

	buffer_free(&dst);

	return 0;
}
