/*
 * Copyright (c) 2006,2008,2009 Solar Designer <solar at openwall.com>
 * Copyright (c) 2011 ABC <abc at openwall.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted.
 *
 * There's ABSOLUTELY NO WARRANTY, express or implied.
 */

#define _GNU_SOURCE
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

/* Please don't remove this (although you may) */
static char *footer =
	"<p><a href=\"http://www.openwall.com/blists/\">Powered by blists</a>"
	" - <a href=\"http://lists.openwall.net\">more mailing lists</a>\n";

static char *month_name[] = {
	"January", "February", "March", "April", "May", "June",
	"July", "August", "September", "October", "November", "December"
};

/*
 * Checks if the hostname ending just before end belongs to domain.
 */
static int match_domain(char *hostname, char *end, char *domain)
{
	size_t hostname_length = end - hostname;
	size_t domain_length = strlen(domain);
	if (hostname_length < domain_length) return 0;
	if (strncasecmp(end - domain_length, domain, domain_length)) return 0;
	return hostname_length == domain_length ||
	    *(end - domain_length - 1) == '.';
}

static char *detect_url(char *what, char *colon, char *end,
	size_t *url_length, int *safe)
{
	char *ptr, *url, *hostname;

	if (colon - what >= 5 && !memcmp((ptr = colon - 5), "https", 5))
		url = ptr;
	else if (colon - what >= 4 && !memcmp((ptr = colon - 4), "http", 4))
		url = ptr;
	else if (colon - what >= 3 && !memcmp((ptr = colon - 3), "ftp", 3))
		url = ptr;
	else
		return NULL;

	if (end - colon <= 3) return NULL;
	if (memcmp(colon, "://", 3)) return NULL;

	ptr = hostname = colon + 3;
	while (ptr < end &&
	    ((*ptr >= 'a' && *ptr <= 'z') ||
	     (*ptr >= 'A' && *ptr <= 'Z') ||
	     (*ptr >= '0' && *ptr <= '9') ||
	     ((*ptr == '-' || *ptr == '.') && ptr > hostname)))
		ptr++;
	while (ptr > hostname && *(ptr - 1) == '.')
		ptr--;
	if (ptr <= hostname) return NULL;

/*
 * We add rel="nofollow" on links to URLs except in safe domains (those
 * where we expect to be no pages that a spammer would want to promote).
 * Yes, these are hard-coded for now.  Feel free to edit.
 */
	*safe = match_domain(hostname, ptr, "openwall.com") ||
		match_domain(hostname, ptr, "openwall.net") ||
		match_domain(hostname, ptr, "openwall.org") ||
		match_domain(hostname, ptr, "openwall.info");

	if (ptr == end || *ptr != '/') {
		/* Let's not detect URLs with userinfo or port */
		if (*ptr == '@' || *ptr == ':')
			return NULL;
		*url_length = ptr - url;
		return url;
	}

	/* RFC 3986 path-abempty [ "?" query ] [ "#" fragment ] */
	while (ptr < end &&
	    ((*ptr >= 'a' && *ptr <= 'z') ||
	     (*ptr >= 'A' && *ptr <= 'Z') ||
	     (*ptr >= '0' && *ptr <= '9') ||
	     *ptr == '/' ||
	     *ptr == '-' || *ptr == '.' || *ptr == '_' || *ptr == '~' ||
	     *ptr == '%' ||
	     *ptr == '!' || *ptr == '$' || *ptr == '&' || *ptr == '\'' ||
	     *ptr == '(' || *ptr == ')' || *ptr == '*' || *ptr == '+' ||
	     *ptr == ',' || *ptr == ';' || *ptr == '=' ||
	     *ptr == ':' || *ptr == '@' ||
	     *ptr == '?' || *ptr == '#')) {
		/* Let's not detect URLs with likely e-mail addresses because
		 * we'd need to obfuscate the addresses, breaking the URLs. */
		if (*ptr == '@')
			return NULL;
		ptr++;
	}

	/* These characters are unlikely to be part of the URL in practice */
	while (--ptr > hostname &&
	    (*ptr == '.' || *ptr == '!' || *ptr == ')' || *ptr == ',' ||
	     *ptr == ';' || *ptr == ':' || *ptr == '?'))
		;
	ptr++;

	*url_length = ptr - url;
	return url;
}

static void buffer_append_html_generic(struct buffer *dst, char *what,
	size_t length, int quotes, int detect_urls)
{
	char *ptr, *end, *url;
	size_t url_length;
	int url_safe;
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
		case '"':
			if (quotes)
				buffer_appends(dst, "&quot;");
			else
				buffer_appendc(dst, c);
			break;
		case ':':
			url = NULL;
			if (detect_urls && ptr < end && *ptr == '/')
				url = detect_url(what, ptr - 1, end,
				    &url_length, &url_safe);
			if (url && url_length <= MAX_URL_LENGTH &&
			    dst->ptr - dst->start >= ptr - 1 - url) {
				dst->ptr -= ptr - 1 - url;
				buffer_appends(dst, "<a href=\"");
				buffer_append_html_generic(dst,
				    url, url_length, 1, 0);
				if (url_safe)
					buffer_appends(dst, "\">");
				else
					buffer_appends(dst,
					    "\" rel=\"nofollow\">");
				buffer_append_html_generic(dst,
				    url, url_length, 0, 0);
				buffer_appends(dst, "</a>");
				ptr = url + url_length;
				break;
			} else
				buffer_appendc(dst, c);
			break;
		case '@':
			if (ptr - what >= 2 && end - ptr >= 4 &&
			    *(ptr - 2) > ' ' && *ptr > ' ' &&
			    *(ptr + 1) > ' ' && *(ptr + 2) > ' ') {
				buffer_appends(dst, "&#64;...");
				ptr += 3;
				break;
			}
			/* FALLTHRU */
		case '\t':
		case '\n':
			buffer_appendc(dst, c);
		case '\r':
			break;
		default:
			if (c >= 0x20)
				buffer_appendc(dst, c);
			else
				buffer_appendc(dst, '.');
		}
	}
}

static void buffer_append_html(struct buffer *dst, char *what, size_t length)
{
	buffer_append_html_generic(dst, what, length, 0, 0);
}

static void buffer_appends_html(struct buffer *dst, char *what)
{
	buffer_append_html(dst, what, strlen(what));
}

static void buffer_append_header(struct buffer *dst, char *what)
{
	buffer_appends_html(dst, what);
	buffer_appendc(dst, '\n');
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
		    ".\n", footer, NULL);

	write_loop(STDOUT_FILENO, msg, strlen(msg));
	free(msg);

	return 1;
}

static int html_send(struct buffer *dst)
{
	if (html_flags & HTML_BODY)
		buffer_appends(dst, footer);

	if (dst->error) {
		buffer_free(dst);
		return html_error(NULL);
	}

	write_loop(STDOUT_FILENO, dst->start, dst->ptr - dst->start);
	buffer_free(dst);

	return 0;
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
	char *p, *q, *date, *from, *to, *cc, *subject, *body, *bend;

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
	    read_loop(fd, &m1, sizeof(m1)) != sizeof(m1);
	if (error || m1 < 1 || m1 >= MAX_MAILBOX_MESSAGES) {
		close(fd);
		free(list_file);
		return html_error((error || m1 > 0) ? NULL : "No such message");
	}
	m1r = m1 + n - (1 + 1); /* both m1 and n are 1-based; m1r is 0-based */
	idx_offset = IDX2IDX(m1r);
	prev = next = 1;
	if (m1r >= 1) {
		idx_offset -= sizeof(idx_msg[0]);
		error = lseek(fd, idx_offset, SEEK_SET) != idx_offset;
		got = error ? -1 :
		    read_loop(fd, &idx_msg, sizeof(idx_msg));
		if (got != sizeof(idx_msg)) {
			error = got != sizeof(idx_msg[0]) * 2;
			idx_msg[2] = idx_msg[1];
			next = 0;
		}
	} else {
		prev = 0;
		error = lseek(fd, idx_offset, SEEK_SET) != idx_offset;
		got = error ? -1 :
		    read_loop(fd, &idx_msg[1], sizeof(idx_msg[1]) * 2);
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
		    read_loop(fd, &m0, sizeof(m0)) != sizeof(m0);
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
		return html_error("mbox open error");
	}
	error =
	    lseek(fd, offset, SEEK_SET) != offset ||
	    read_loop(fd, src.start, size) != size;
	if (close(fd) || error || mime_init(&mime, &src)) {
		buffer_free(&dst);
		buffer_free(&src);
		return html_error("mbox read error");
	}

	date = from = to = cc = subject = body = NULL;
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
			if (!strncasecmp(src.ptr, "CC:", 3))
				cc = mime_decode_header(&mime);
			else
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
		buffer_appends_html(&dst, list);
		if (subject && strlen(subject) > 9) {
			buffer_appends(&dst, " - ");
			buffer_appends_html(&dst, subject + 9);
		}
		buffer_appends(&dst, "</title>\n");
		if (html_flags & HTML_CENSOR)
			buffer_appends(&dst,
			    "<meta name=\"robots\" content=\"noindex\">\n");
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
		    "<a href=\".\">[day]</a>"
		    " <a href=\"..\">[month]</a>"
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
		if (cc)
			buffer_append_header(&dst, cc);
		if (subject)
			buffer_append_header(&dst, subject);
		if (!(html_flags & HTML_CENSOR))
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
				buffer_appends_html(&dst, mime.entities->type);
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
			buffer_append_html_generic(&dst,
			    body, mime.dst.ptr - body, 0, 1);
			mime.dst.ptr = body;
		} while (bend < src.end && mime.entities);
		buffer_appends(&dst, "</pre>\n");

		if (html_flags & HTML_CENSOR)
			buffer_appends(&dst, "[ REMOVED ]\n");
		else
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

	return html_send(&dst);
}

/* output From and Subject strings */
static void output_strings(struct buffer *dst, struct idx_message *m,
    int close_a)
{
	char *from, *subj;
	int from_len, subj_len;

	from = m->strings;
	from_len = strnlen(from, sizeof(m->strings));

	subj = m->strings + from_len + 1;
	if (from_len + 1 < sizeof(m->strings)) {
		subj_len = strnlen(subj, sizeof(m->strings) - from_len - 1);
	} else
		subj_len = 0;

	if (subj_len) {
		buffer_append_html(dst, subj, subj_len);
		if (m->flags & IDX_F_SUBJECT_TRUNC)
			buffer_appends(dst, "&hellip;");
	} else
		buffer_appends(dst, "(no subject)");
	if (close_a)
		buffer_appends(dst, "</a>");
	buffer_appends(dst, " (");
	buffer_append_html(dst, from, from_len);
	if (m->flags & IDX_F_FROM_TRUNC)
		buffer_appends(dst, "&hellip;");
	buffer_appends(dst, ")");
}

int html_day_index(char *list, unsigned int y, unsigned int m, unsigned int d)
{
	unsigned int aday;
	char *idx_file;
	off_t idx_offset;
	off_t size, size_n;
	int fd, error, got;
	idx_msgnum_t mx[2]; /* today, next day */
	struct buffer dst;
	struct idx_message *mp;
	int prev; /* have prev message = 1 */
	int count; /* how many messages in this month */
	int next; /* flag & index to next message */

	if (y < MIN_YEAR || y > MAX_YEAR ||
	    m < 1 || m > 12 ||
	    d < 1 || d > 31)
		return html_error("Invalid date");
	aday = YMD2ADAY(y - MIN_YEAR, m, d);

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
	/* read two consecutive aday entries
	 * will need them to determine message count for this day */
	idx_offset = aday * sizeof(idx_msgnum_t);
	error =
	    lock_fd(fd, 1) ||
	    lseek(fd, idx_offset, SEEK_SET) != idx_offset ||
	    read_loop(fd, &mx, sizeof(mx)) != sizeof(mx);
	if (error || mx[0] < 1 || mx[0] >= MAX_MAILBOX_MESSAGES) {
		unlock_fd(fd);
		close(fd);
		return html_error((error || mx[0] > 0) ? NULL : "No messages"
		    " for this day");
	}
	if (mx[1] > 0)
		count = mx[1] - mx[0];
	else
		count = -mx[1];
	size = count * sizeof(struct idx_message);
	idx_offset = IDX2IDX(mx[0] - 1);
	if (mx[0] > 1) {
		/* read one more entry for Prev day quick link */
		size += sizeof(struct idx_message);
		idx_offset -= sizeof(struct idx_message);
		prev = 1;
	} else
		prev = 0;

	/* read one more entry for Next day quick link */
	size_n = size + sizeof(struct idx_message);

	error = !(mp = malloc(size_n)) ||
		lseek(fd, idx_offset, SEEK_SET) != idx_offset;
	got = error ? -1 :
	    read_loop(fd, mp, size_n);
	if (got != size && got != size_n) {
		unlock_fd(fd);
		close(fd);
		if (mp) free(mp);
		return html_error("Index error");
	}
	next = (got == size_n) ? count + prev : 0;

	if (unlock_fd(fd)) error = 1;
	if (close(fd) || error || buffer_init(&dst, 0)) {
		free(mp);
		return html_error(NULL);
	}

	buffer_appends(&dst, "\n");

	if (html_flags & HTML_HEADER) {
		buffer_appends(&dst, "<title>");
		buffer_appends_html(&dst, list);
		buffer_appendf(&dst, " mailing list - %u/%02u/%02u</title>\n",
		    y, m, d);
	}

	if (html_flags & HTML_BODY) {
		int i;
		if (prev) {
			buffer_appends(&dst, "<a href=\"");
			buffer_appendf(&dst,
			    "../../../%u/%02u/%02u/\">[&lt;prev day]</a> ",
			    MIN_YEAR + mp[0].y,
			    mp[0].m, mp[0].d);
		}
		if (next) {
			buffer_appends(&dst, "<a href=\"");
			buffer_appendf(&dst,
			    "../../../%u/%02u/%02u/\">[next day&gt;]</a> ",
			    MIN_YEAR + mp[next].y,
			    mp[next].m, mp[next].d);
		}
		buffer_appends(&dst,
		    "<a href=\"..\">[month]</a>"
		    " <a href=\"../..\">[year]</a>"
		    " <a href=\"../../..\">[list]</a>\n");

		buffer_appends(&dst, "<p><h2>");
		buffer_appends_html(&dst, list);
		buffer_appendf(&dst, " mailing list - %u/%02u/%02u</h2>\n",
		    y, m, d);

		if (count)
			buffer_appends(&dst, "<ul>\n");
		for (i = 0; i < count; i++) {
			struct idx_message *msg = mp + i + prev;

			buffer_appendf(&dst, "<li><a href=\"%u\">", i + 1);
			output_strings(&dst, msg, 1);
			buffer_appends(&dst, "\n");
		}
		if (count)
			buffer_appends(&dst, "</ul>\n");

		buffer_appendf(&dst, "<p>%u message%s\n",
		    count, count == 1 ? "" : "s");
	}

	free(mp);

	return html_send(&dst);
}

int html_month_index(char *list, unsigned int y, unsigned int m)
{
	unsigned int d, n, aday, dp;
	char *idx_file;
	off_t idx_offset;
	int fd, error;
	idx_msgnum_t mn[32], mp, count, total;
	struct buffer dst;
	int first; /* first message of this month */
	off_t size, size_n;
	struct idx_message *msgp = NULL, *msg = NULL;
	int prev = 0, next = 0;

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
	if (lock_fd(fd, 1) ||
	    lseek(fd, idx_offset, SEEK_SET) != idx_offset ||
	    read_loop(fd, mn, sizeof(mn)) != sizeof(mn)) {
		unlock_fd(fd);
		close(fd);
		return html_error("Index error");
	}

	/* quickly calculate how many messages we have in this month */
	total = 0;
	first = 0;
	mp = mn[0];
	for (d = 1; d <= 31; d++) {
		if (!mn[d]) continue;
		if (mp > 0) {
			/* Remember index of first message */
			if (first == 0)
				first = mp;
			count = (mn[d] > 0) ? mn[d] - mp : -mn[d];
			if (count <= 0) {
				buffer_free(&dst);
				unlock_fd(fd);
				close(fd);
				return html_error(NULL);
			}
			total += count;
		}
		mp = mn[d];
	}
	/* have messages, allocate and read them */
	if (total && first) {
		off_t got;

		first--;
		size = total * sizeof(struct idx_message);
		idx_offset = IDX2IDX(first);
		/* we need to read prev and next messages too */
		if (first) {
			size += sizeof(struct idx_message);
			idx_offset -= sizeof(struct idx_message);
			prev = 1;
		}
		size_n = size + sizeof(struct idx_message);

		if (!(msgp = malloc(size_n)) ||
		    lseek(fd, idx_offset, SEEK_SET) != idx_offset ||
		    (got = read_loop(fd, msgp, size_n)) == -1 ||
			(got != size && got != size_n)) {
			unlock_fd(fd);
			close(fd);
			if (msgp) free(msgp);
			return html_error("Index error");
		}
		msg = msgp + prev;
		next = (got == size_n) ? total + prev : 0;
	}

	if (unlock_fd(fd)) error = 1;
	if (close(fd) || error || buffer_init(&dst, 0))
		return html_error(NULL);

	buffer_appends(&dst, "\n");

	if (html_flags & HTML_HEADER) {
		buffer_appends(&dst, "<title>");
		buffer_appends_html(&dst, list);
		buffer_appendf(&dst, " mailing list - %u/%02u</title>\n", y, m);
	}

	if (html_flags & HTML_BODY) {
		if (prev) {
			buffer_appends(&dst, "<a href=\"");
			buffer_appendf(&dst,
			    "../../%u/%02u/\">[&lt;prev month]</a> ",
			    MIN_YEAR + msgp[0].y, msgp[0].m);

		}
		if (next) {
			buffer_appends(&dst, "<a href=\"");
			buffer_appendf(&dst,
			    "../../%u/%02u/\">[next month&gt;]</a> ",
			    MIN_YEAR + msgp[next].y, msgp[next].m);

		}
		buffer_appends(&dst,
		    "<a href=\"..\">[year]</a>"
		    " <a href=\"../..\">[list]</a>\n");

		buffer_appends(&dst, "<p><h2>");
		buffer_appends_html(&dst, list);
		buffer_appendf(&dst, " mailing list - %u/%02u</h2>\n", y, m);

		if (!total || !msg) {
			buffer_free(&dst);
			return html_error("No messages for this day.");
		}

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

				buffer_appendf(&dst, "<b>%s %u</b> "
				    "(<a href=\"%02u/\">%u message%s</a>)<br>\n"
				    "<ul>\n",
				    month_name[m - 1], dp + 1,
				    dp + 1, count, count == 1 ? "" : "s");

				int maxn = count;
				if (count >= MAX_SHORT_MSG_LIST)
					maxn = MAX_SHORT_MSG_LIST;
				if (count > MAX_SHORT_MSG_LIST)
					maxn--;

				for (n = 1; n <= maxn; n++) {
					buffer_appendf(&dst,
					    "<li><a href=\"%02u/%u\">",
					    dp + 1, n);
					output_strings(&dst, msg++, 1);
					buffer_appends(&dst, "\n");
				}
				msg += count - maxn;
				if (count > MAX_SHORT_MSG_LIST)
					buffer_appendf(&dst, "<li>"
					    "<a href=\"%02u/\">"
					    "%u more messages"
					    "</a>\n", d, count - maxn);
				buffer_appends(&dst, "</ul>\n");
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

	if (msgp) free(msgp);

	return html_send(&dst);
}

int html_year_index(char *list, unsigned int y)
{
	unsigned int min_y, max_y, m, d, aday, rday;
	char *idx_file;
	off_t idx_offset;
	int fd, error;
	idx_msgnum_t *mn, count, monthly_total, total;
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
	if (lock_fd(fd, 1) ||
	    (idx_offset && lseek(fd, idx_offset, SEEK_SET) != idx_offset) ||
	    read_loop(fd, mn, mn_size) != mn_size) {
		unlock_fd(fd);
		close(fd);
		free(mn);
		return html_error(NULL);
	}

	/* find first and next index for Prev and Next year links */
	int first = 0;
	int lastn = 0;
	rday = YMD2ADAY(min_y - MIN_YEAR, 1, 1) - aday;
	unsigned int eday = YMD2ADAY(max_y - MIN_YEAR + 1, 1, 1) - aday;
	for (; rday < eday; rday++)
		if (mn[rday] > 0) {
			if (!first)
				first = mn[rday];
			if (mn[rday + 1] <= 0)
				lastn = mn[rday] + -mn[rday + 1];
			else
				lastn = mn[rday + 1];
		}
	int prev = 0;
	int next = 0;
	if (first || lastn) {
		struct idx_message msg;
		off_t size = sizeof(struct idx_message);
		if (first > 1) {
			idx_offset = IDX2IDX(first - 2);
			if (lseek(fd, idx_offset, SEEK_SET) != idx_offset ||
			    read_loop(fd, &msg, size) != size) {
				unlock_fd(fd);
				close(fd);
				free(mn);
				return html_error("Index error");
			}
			prev = MIN_YEAR + msg.y;
		}
		if (lastn > 1) {
			idx_offset = IDX2IDX(lastn);
			if (lseek(fd, idx_offset, SEEK_SET) == idx_offset &&
			    read_loop(fd, &msg, size) == size)
				next = MIN_YEAR + msg.y;
		}
	}

	/* read Recent messages */
	struct idx_message *msg = NULL;
	int recent_count = 0;
	int i;
	if (min_y != max_y && lastn > 1) {
		int recent_offset = 0;
		recent_count = MAX_RECENT_MSG_LIST;
		if ((lastn - 1) < recent_count)
			recent_count = lastn - 1;
		size_t size = recent_count * sizeof(struct idx_message);
		recent_offset = lastn - recent_count;
		idx_offset = IDX2IDX(recent_offset - 1);
		if (!(msg = malloc(size)) ||
		    lseek(fd, idx_offset, SEEK_SET) != idx_offset ||
		    (read_loop(fd, msg, size) != size))
			recent_count = 0;

		/* resolve to message number in the day and cache in offset field */
		rday = YMD2ADAY(min_y - MIN_YEAR, 1, 1) - aday;
		i = 0;
		for (; rday < eday; rday++)
			if (mn[rday] > 0 && recent_offset >= mn[rday]) {
				count = aday_count(&mn[rday]);
				while (recent_offset < (mn[rday] + count)) {
					msg[i].offset = recent_offset - mn[rday] + 1;
					recent_offset++;
					i++;
					if (i > recent_count)
						break;
				}
			}
	}
	if (unlock_fd(fd)) error = 1;
	if (close(fd) || error || buffer_init(&dst, 0)) {
		free(mn);
		return html_error(NULL);
	}

	buffer_appends(&dst, "\n");

	if (html_flags & HTML_HEADER) {
		buffer_appends(&dst, "<title>");
		buffer_appends_html(&dst, list);
		buffer_appends(&dst, " mailing list");
		if (min_y == max_y)
			buffer_appendf(&dst, " - %u", y);
		buffer_appends(&dst, "</title>\n");
	}

	if (html_flags & HTML_BODY) {
		if (prev) {
			buffer_appendf(&dst,
			    "<a href=\"../%u/\">[prev year]</a>\n", prev);
		}
		if (next) {
			buffer_appendf(&dst,
			    "<a href=\"../%u/\">[next year]</a>\n", next);
		}
		if (min_y == max_y)
			buffer_appends(&dst, "<a href=\"..\">[list]</a>\n");

		buffer_appends(&dst, "<p><h2>");
		buffer_appends_html(&dst, list);
		buffer_appends(&dst, " mailing list");
		if (min_y == max_y)
			buffer_appendf(&dst, " - %u", y);
		buffer_appends(&dst, "</h2>\n");


		total = 0;
		/* output short year-o-month index */
		int o_header = 0;
		int o_year = 0;
		int o_month = -1;
		for (y = max_y; y >= min_y; y--) {
			rday = YMD2ADAY(y - MIN_YEAR, 1, 1) - aday;
			for (m = 1; m <= 12; m++) {
				monthly_total = 0;
				for (d = 1; d <= 31; d++, rday++) {
					if (mn[rday] <= 0) continue;
					if (mn[rday + 1] > 0)
						count = mn[rday + 1] - mn[rday];
					else
						count = -mn[rday + 1];
					monthly_total += count;
				}
				if (!monthly_total)
					continue;
				if (!o_header) {
					buffer_appends(&dst,
					    "\n<table border=0 "
					    "class=calendar><tr><th>"
					    "<th>Jan<th>Feb<th>Mar"
					    "<th>Apr<th>May<th>Jun"
					    "<th>Jul<th>Aug<th>Sep"
					    "<th>Oct<th>Nov<th>Dec\n");

					o_header++;
				}
				if (o_year != y) {
					if (o_month >= 0)
						for (o_month++; o_month <= 12;
						    o_month++)
							buffer_appends(&dst,
							    "<td class=ccell>"
							    "&nbsp;");
					buffer_appendf(&dst, "\n<tr><td>");
					if (min_y != max_y)
						buffer_appendf(&dst,
						    "<a href=\"%u/\">", y);
					buffer_appendf(&dst, "<b>%4u</b>", y);
					if (min_y != max_y)
						buffer_appends(&dst, "</a>");
					o_year = y;
					o_month = 0;
				}
				for (o_month++; o_month < m; o_month++)
					buffer_appends(&dst, "<td class=ccell>"
					    "&nbsp;");
				buffer_appendf(&dst, "<td class=ccell>"
				    "<a href=\"");
				if (min_y != max_y)
					buffer_appendf(&dst, "%u/", y);
				buffer_appendf(&dst, "%02u/\">%u</a>",
				    m, monthly_total);
				o_month = m;

				total += monthly_total;
			}
		}
		if (o_header) {
			if (o_year)
				for (o_month++; o_month <= 12; o_month++)
					buffer_appends(&dst, "<td class=ccell>"
					    "&nbsp;");
			buffer_appends(&dst, "\n</table>\n");
		}

		/* output Recent messages */
		if (msg && recent_count) {
			buffer_appends(&dst, "<br>Recent messages:<br>\n<ul>\n");
			for (i = recent_count - 1; i >= 0; i--) {
				buffer_appendf(&dst,
				    "<li>%04u/%02u/%02u #%u: <a href=\"%04u/%02u/%02u/%u\">\n",
				    msg[i].y + MIN_YEAR, msg[i].m, msg[i].d, (int)msg[i].offset,
				    msg[i].y + MIN_YEAR, msg[i].m, msg[i].d, (int)msg[i].offset);
				output_strings(&dst, &msg[i], 1);
				buffer_appends(&dst, "\n");
			}
			buffer_appends(&dst, "</ul>\n");
		}

		free(mn);
		if (msg)
			free(msg);

		if (total)
			buffer_appendf(&dst, "<p>%u message%s\n",
			    total, total == 1 ? "" : "s");
		else
			buffer_appends(&dst, "<p>No messages\n");
	}

	return html_send(&dst);
}
