/*
 * Copyright (c) 2006,2008,2009,2015,2017,2018 Solar Designer <solar at openwall.com>
 * Copyright (c) 2011,2014,2017 ABC <abc at openwall.com>
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
#include "encoding.h"

int html_flags = HTML_BODY;

/* Please don't remove this (although you may) */
static const char * const footer =
	"<p><a href=\"http://www.openwall.com/blists/\">Powered by blists</a>"
	" - <a href=\"http://lists.openwall.net\">more mailing lists</a>\n";

static const char * const month_name[] = {
	"January", "February", "March", "April", "May", "June",
	"July", "August", "September", "October", "November", "December"
};

/*
 * Checks if the hostname ending just before end belongs to domain.
 */
static int match_domain(const char *hostname, const char *end, const char *domain)
{
	size_t hostname_length = end - hostname;
	size_t domain_length = strlen(domain);
	if (hostname_length < domain_length ||
	    strncasecmp(end - domain_length, domain, domain_length))
		return 0;
	return hostname_length == domain_length || *(end - domain_length - 1) == '.';
}

static const char *detect_url(const char *what, const char *colon, const char *end, size_t *url_length, int *safe)
{
	const char *ptr, *url, *hostname;

	if (colon - what >= 5 && !memcmp((ptr = colon - 5), "https", 5))
		url = ptr;
	else if (colon - what >= 4 && !memcmp((ptr = colon - 4), "http", 4))
		url = ptr;
	else if (colon - what >= 3 && !memcmp((ptr = colon - 3), "ftp", 3))
		url = ptr;
	else
		return NULL;

	if (end - colon <= 3)
		return NULL;
	if (memcmp(colon, "://", 3))
		return NULL;

	ptr = hostname = colon + 3;
	while (ptr < end &&
	    ((*ptr >= 'a' && *ptr <= 'z') ||
	     (*ptr >= 'A' && *ptr <= 'Z') ||
	     (*ptr >= '0' && *ptr <= '9') ||
	     ((*ptr == '-' || *ptr == '.') && ptr > hostname)))
		ptr++;
	while (ptr > hostname && *(ptr - 1) == '.')
		ptr--;
	if (ptr <= hostname)
		return NULL;

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
	     *ptr == '?' || *ptr == '#'))
		ptr++;

	/* These characters are unlikely to be part of the URL in practice */
	while (--ptr > hostname &&
	    (*ptr == '.' || *ptr == '!' || *ptr == ')' || *ptr == ',' ||
	     *ptr == ';' || *ptr == ':' || *ptr == '?'))
		;
	ptr++;

	*url_length = ptr - url;
	return url;
}

static int detect_email(const char *what, const char *at, const char *end)
{
	return at > what && end - at > 4 &&
	    *(at - 1) > ' ' && *(at + 1) > ' ' && *(at + 2) > ' ' && *(at + 3) > ' ';
}

static void buffer_append_filename(struct buffer *dst, const char *fn, int text)
{
	if (!fn || !*fn)
		fn = "attachment";

	char prev = 0;
	const char *p;
	for (p = fn; *p && p - fn < MAX_FILENAME_LENGTH; p++) {
		if ((*p >= 'a' && *p <= 'z') ||
		    (*p >= 'A' && *p <= 'Z') ||
		    (*p >= '0' && *p <= '9'))
			buffer_appendc(dst, prev = *p);
		else if (prev != '_')
			buffer_appendc(dst, prev = '_');
	}
	buffer_appends(dst, text ? ".txt" : ".bin");
}

#define BAH_QUOTE			1
#define BAH_DETECT_URLS			2
#define BAH_OBFUSCATE			4

static void buffer_append_html_generic(struct buffer *dst, const char *what, size_t length, int flags)
{
	const char *ptr, *end, *url;
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
			if (flags & BAH_QUOTE)
				buffer_appends(dst, "&quot;");
			else
				buffer_appendc(dst, c);
			break;
		case ':':
			url = NULL;
			if ((flags & BAH_DETECT_URLS) && ptr < end && *ptr == '/')
				url = detect_url(what, ptr - 1, end, &url_length, &url_safe);
			if (url && url_length <= MAX_URL_LENGTH && dst->ptr - dst->start >= ptr - 1 - url) {
				dst->ptr -= ptr - 1 - url;
				buffer_appends(dst, "<a href=\"");
				buffer_append_html_generic(dst, url, url_length, BAH_QUOTE);
				if (url_safe)
					buffer_appends(dst, "\">");
				else
					buffer_appends(dst, "\" rel=\"nofollow\">");
				buffer_append_html_generic(dst, url, url_length, 0);
				buffer_appends(dst, "</a>");
				ptr = url + url_length;
			} else {
				buffer_appendc(dst, c);
			}
			break;
		case '@':
			if (detect_email(what, ptr - 1, end)) {
				if (flags & BAH_OBFUSCATE) {
					buffer_appends(dst, "&#64;...");
					ptr += 3;
				} else {
					/* Always do harmless obfuscation */
					buffer_appends(dst, "&#64;");
				}
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

static void buffer_append_html(struct buffer *dst, const char *what, size_t length)
{
	buffer_append_html_generic(dst, what, length, BAH_OBFUSCATE);
}

static void buffer_appends_html(struct buffer *dst, const char *what)
{
	buffer_append_html(dst, what, strlen(what));
}

static void buffer_append_header(struct buffer *dst, const char *what)
{
	buffer_appends_html(dst, what);
	buffer_appendc(dst, '\n');
}

int html_error_real(const char *file, unsigned int lineno, const char *msg)
{
	char loc[128];
	char *msgt;
	const char *prefix = "The request has failed: ";

	if (!msg)
		msg = "Internal server error";

	const char *p = getenv("SERVER_PROTOCOL");
	if (!p || strcmp(p, "INCLUDED"))
		msgt = concat("Status: 404 Not Found\nContent-Type: text/plain\n\n", prefix, msg, "\n", NULL);
	else if (html_flags & HTML_HEADER)
		msgt = concat("\n<title>", prefix, msg, "</title>\n"
		    "<meta name=\"robots\" content=\"noindex\">\n", NULL);
	else
		msgt = concat("\n<p>", prefix, msg, "\n", footer, NULL);

	write_loop(STDOUT_FILENO, msgt, strlen(msgt));
	free(msgt);

	snprintf(loc, sizeof(loc), " (%s:%u)", file, lineno);
	msgt = concat("The request has failed: ", msg, loc, "\n", NULL);
	write_loop(STDERR_FILENO, msgt, strlen(msgt));
	free(msgt);

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

static int is_attachment(struct mime_ctx *mime)
{
	if (mime->entities->filename && mime->entities->filename[0])
		return 1;
	return 0;
}

static int is_inline(struct mime_ctx *mime)
{
	if (!is_attachment(mime) &&
	    mime->entities->disposition != CONTENT_ATTACHMENT &&
	    !strncasecmp(mime->entities->type, "text/", 5) &&
	    strcasecmp(mime->entities->type, "text/html"))
		return 1; /* show */
	return 0; /* do not show */
}

int html_message(const char *list, unsigned int y, unsigned int m, unsigned int d, unsigned int n)
{
	unsigned int aday, n0, n2;
	char *list_file;
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
	if (!list_file)
		return html_error(NULL);

	fd = idx_open(list);
	if (fd < 0) {
		error = errno;
		free(list_file);
		return html_error(error == ENOENT ?
		    "No such mailing list" : (error == ESRCH ?
		    "Index needs rebuild" : NULL));
	}

	error = !idx_read_aday_ok(fd, aday, &m1, sizeof(m1));
	if (error || m1 < 1 || m1 >= MAX_MAILBOX_MESSAGES) {
		idx_close(fd);
		free(list_file);
		return html_error((error || m1 > 0) ? NULL : "No such message");
	}
	m1r = m1 + n - (1 + 1); /* both m1 and n are 1-based; m1r is 0-based */
	idx_offset = IDX2MSG(m1r);
	prev = next = 1;
	if (m1r >= 1) { /* read idx for previous, current, maybe next message */
		idx_offset -= sizeof(idx_msg[0]);
		got = idx_read(fd, idx_offset, &idx_msg, sizeof(idx_msg));
		if (got != sizeof(idx_msg)) { /* not all 3 */
			error = got != sizeof(idx_msg[0]) * 2; /* must be 2 */
			if (got == sizeof(idx_msg[0])) /* have only previous */
				got = 0; /* "No such message", not our error */
			idx_msg[2] = idx_msg[1];
			next = 0;
		}
	} else { /* read idx for current and maybe next message */
		prev = 0;
		got = idx_read(fd, idx_offset, &idx_msg[1], sizeof(idx_msg[1]) * 2);
		if (got != sizeof(idx_msg[1]) * 2) { /* not both */
			error = got != sizeof(idx_msg[1]); /* must be 1 */
			idx_msg[2] = idx_msg[1];
			next = 0;
		}
		idx_msg[0] = idx_msg[1];
	}

	n0 = n - 1;
	if (!n0 && prev && !error) {
		aday = YMD2ADAY(idx_msg[0].y, idx_msg[0].m, idx_msg[0].d);
		error = !idx_read_aday_ok(fd, aday, &m0, sizeof(m0));
		if (m1 > m0)
			n0 = m1 - m0;
		else
			error = 1;
	}

	if (idx_close(fd) || error) {
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
		size = MAX_MESSAGE_SIZE;
	if (buffer_init(&src, size)) {
		free(list_file);
		return html_error(NULL);
	}
	fd = open(list_file, O_RDONLY);
	free(list_file);
	if (fd < 0) {
		buffer_free(&src);
		return html_error("mbox open error");
	}
	error =
	    lseek(fd, offset, SEEK_SET) != offset ||
	    read_loop(fd, src.start, size) != size;
	if (close(fd) || error || mime_init(&mime, &src)) {
		buffer_free(&src);
		return html_error("mbox read error");
	}
	if (buffer_init(&dst, size)) {
		buffer_free(&src);
		mime_free(&mime);
		return html_error(NULL);
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
	if (src.ptr >= src.end) {
		buffer_free(&src);
		buffer_free(&dst);
		mime_free(&mime);
		return html_error(NULL);
	}
	if (*src.ptr == '\n')
		body = ++src.ptr;

	if ((p = subject)) {
		while ((p = strchr(p, '['))) {
			if (strncasecmp(++p, list, strlen(list)))
				continue;
			q = p + strlen(list);
			if (*q != ']')
				continue;
			if (*++q == ' ') q++;
			memmove(--p, q, strlen(q) + 1);
		}
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
			buffer_appends(&dst, "<meta name=\"robots\" content=\"noindex\">\n");
	}

	if (html_flags & HTML_BODY) {
		unsigned int attachment_count = 0;

		if (prev) {
			buffer_appends(&dst, "<a href=\"");
			if (n == 1)
				buffer_appendf(&dst, "../../../%u/%02u/%02u/",
				    MIN_YEAR + idx_msg[0].y, idx_msg[0].m, idx_msg[0].d);
			buffer_appendf(&dst, "%u\">[&lt;prev]</a> ", n0);
		}
		if (next) {
			buffer_appends(&dst, "<a href=\"");
			if (n2 == 1)
				buffer_appendf(&dst, "../../../%u/%02u/%02u/",
				    MIN_YEAR + idx_msg[2].y, idx_msg[2].m, idx_msg[2].d);
			buffer_appendf(&dst, "%u\">[next&gt;]</a> ", n2);
		}
		if (idx_msg[1].t.pn) {
			buffer_appends(&dst, "<a href=\"");
			if (idx_msg[1].t.py != idx_msg[1].y ||
			    idx_msg[1].t.pm != idx_msg[1].m ||
			    idx_msg[1].t.pd != idx_msg[1].d)
				buffer_appendf(&dst, "../../../%u/%02u/%02u/",
				    MIN_YEAR + idx_msg[1].t.py, idx_msg[1].t.pm, idx_msg[1].t.pd);
			buffer_appendf(&dst, "%u\">[&lt;thread-prev]</a> ", idx_msg[1].t.pn);
		}
		if (idx_msg[1].t.nn) {
			buffer_appends(&dst, "<a href=\"");
			if (idx_msg[1].t.ny != idx_msg[1].y ||
			    idx_msg[1].t.nm != idx_msg[1].m ||
			    idx_msg[1].t.nd != idx_msg[1].d)
				buffer_appendf(&dst, "../../../%u/%02u/%02u/",
				    MIN_YEAR + idx_msg[1].t.ny, idx_msg[1].t.nm, idx_msg[1].t.nd);
			buffer_appendf(&dst, "%u\">[thread-next&gt;]</a> ", idx_msg[1].t.nn);
		}
		buffer_appends(&dst,
		    "<a href=\".\">[day]</a>"
		    " <a href=\"..\">[month]</a>"
		    " <a href=\"../..\">[year]</a>"
		    " <a href=\"../../..\">[list]</a>\n");

		buffer_appends(&dst, "<pre style=\"white-space: pre-wrap\">\n");
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
				if (!body || body >= src.end)
					break;
				body = mime_next_body(&mime);
			}
			if (mime.entities->boundary)
				body = NULL;
			if (!body) {
				bend = mime_skip_body(&mime);
				if (!bend)
					break;
				continue;
			}

			/* mime_decode_body() will break mime vars, so,
			 * remember them now */
			char *filename = mime.entities->filename;
			char *type = mime.entities->type;
			const int isattachment = is_attachment(&mime);
			const int isinline = is_inline(&mime);
			int skip = 0;

			body = mime_decode_body(&mime, isattachment ? RECODE_NO : RECODE_YES, &bend);
			if (!body)
				break;
			if (bend >= src.end)
				skip = trunc;
			bend = src.ptr;
			if (!skip && isattachment) {
				int text = !strncasecmp(type, "text/", 5);
				attachment_count++;
				buffer_appendf(&dst, "\n<span style=\"font-family: times;\"><strong>"
				    "%s attachment \"</strong><a href=\"%u/%u\"%s>",
				    text ? "View" : "Download", n, attachment_count,
				    text ? "" :  " rel=\"nofollow\" download");
				if (filename)
					buffer_appends_html(&dst, filename);
				buffer_appends(&dst, "</a><strong>\" of type \"</strong>");
				buffer_appends_html(&dst, type);
				buffer_appends(&dst, "<strong>\"");
				if (body)
					buffer_appendf(&dst, " (%llu bytes)", (unsigned long long)(mime.dst.ptr - body));
				buffer_appends(&dst, "</strong></span>\n");
				continue;
			} else if (!isinline) {
				skip = 1;
			} else {
				skip = 0; /* do not skip non-attachments */
			}
			if (skip) {
				buffer_appends(&dst, "\n<span style=\"font-family: times;\"><strong>"
				    "Content of type \"</strong>");
				buffer_appends_html(&dst, type);
				buffer_appends(&dst, "<strong>\" skipped</strong></span>\n");
				continue;
			}
			/* inline */
			buffer_appendc(&dst, '\n');
			buffer_append_html_generic(&dst, body, mime.dst.ptr - body, BAH_DETECT_URLS | BAH_OBFUSCATE);
			mime.dst.ptr = body;
		} while (bend < src.end && mime.entities);

		if ((html_flags & HTML_CENSOR) || trunc)
			buffer_appendf(&dst, "\n<span style=\"font-family: times;\"><strong>"
			    "Content %s</strong></span>\n", (html_flags & HTML_CENSOR) ? "removed" : "truncated");

		buffer_appends(&dst, "</pre>\n");
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

int html_attachment(const char *list, unsigned int y, unsigned int m, unsigned int d, unsigned int n, unsigned int a)
{
	unsigned int aday;
	char *list_file;
	off_t idx_offset;
	int fd, error, got, trunc;
	idx_msgnum_t m1, m1r;
	struct idx_message idx_msg;
	idx_off_t offset;
	idx_size_t size;
	struct buffer src, dst;
	struct mime_ctx mime;
	char *body, *bend;

	if (y < MIN_YEAR || y > MAX_YEAR ||
	    m < 1 || m > 12 ||
	    d < 1 || d > 31 ||
	    n < 1 || n > 999999)
		return html_error("Invalid date or message number");
	aday = YMD2ADAY(y - MIN_YEAR, m, d);

	list_file = concat(MAIL_SPOOL_PATH "/", list, NULL);
	if (!list_file)
		return html_error(NULL);

	fd = idx_open(list);
	if (fd < 0) {
		error = errno;
		free(list_file);
		return html_error(error == ENOENT ?
		    "No such mailing list" : (error == ESRCH ?
		    "Index needs rebuild" : NULL));
	}

	error = !idx_read_aday_ok(fd, aday, &m1, sizeof(m1));
	if (error || m1 < 1 || m1 >= MAX_MAILBOX_MESSAGES) {
		idx_close(fd);
		free(list_file);
		return html_error((error || m1 > 0) ? NULL : "No such message");
	}
	m1r = m1 + n - 2; /* both m1 and n are 1-based; m1r is 0-based */
	idx_offset = IDX2MSG(m1r);
	got = idx_read(fd, idx_offset, &idx_msg, sizeof(idx_msg));
	if (got != sizeof(idx_msg))
		error = 1;

	if (idx_close(fd) || error) {
		free(list_file);
		return html_error(got ? NULL : "No such message");
	}

	if (y - MIN_YEAR != idx_msg.y ||
	    m != idx_msg.m ||
	    d != idx_msg.d) {
		free(list_file);
		return html_error("No such message");
	}

	offset = idx_msg.offset;
	size = idx_msg.size;

	trunc = size > MAX_MESSAGE_SIZE;
	if (trunc)
		size = MAX_MESSAGE_SIZE;
	if (buffer_init(&src, size)) {
		free(list_file);
		return html_error(NULL);
	}
	fd = open(list_file, O_RDONLY);
	free(list_file);
	if (fd < 0) {
		buffer_free(&src);
		return html_error("mbox open error");
	}
	error =
	    lseek(fd, offset, SEEK_SET) != offset ||
	    read_loop(fd, src.start, size) != size;
	if (close(fd) || error || mime_init(&mime, &src)) {
		buffer_free(&src);
		return html_error("mbox read error");
	}
	if (buffer_init(&dst, size)) {
		buffer_free(&src);
		mime_free(&mime);
		return html_error(NULL);
	}

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
	if (src.ptr >= src.end) {
		buffer_free(&src);
		buffer_free(&dst);
		mime_free(&mime);
		return html_error(NULL);
	}
	if (*src.ptr == '\n')
		body = ++src.ptr;

	const char *error_msg = "Attachment not found";
	unsigned int attachment_count = 0;
	if (a)
	do {
		if (mime.entities->boundary) {
			body = mime_next_body_part(&mime);
			if (!body || body >= src.end)
				break;
			body = mime_next_body(&mime);
		}
		if (mime.entities->boundary || !is_attachment(&mime) || ++attachment_count != a)
			body = NULL;
		if (!body) {
			bend = mime_skip_body(&mime);
			if (!bend)
				break;
			continue;
		}

		int text = !strncasecmp(mime.entities->type, "text/", 5);
		if (text) {
			buffer_appends(&dst, "Content-Type: text/plain");
			if (mime.entities->charset && enc_allowed_charset(mime.entities->charset))
				buffer_appendf(&dst, "; charset=%s", mime.entities->charset);
			buffer_appendc(&dst, '\n');
		} else {
			buffer_appends(&dst, "Content-Type: application/octet-stream\n");
		}
		buffer_appendf(&dst, "Content-Disposition: %s; filename=\"", text ? "inline" : "attachment");
		buffer_append_filename(&dst, mime.entities->filename, text);
		buffer_appends(&dst, "\"\n");

		body = mime_decode_body(&mime, RECODE_NO, &bend);
		if (trunc && (!body || bend >= src.end)) {
			error_msg = "Attachment is truncated";
			break;
		}

		buffer_appendf(&dst, "Content-Length: %llu\n\n", (unsigned long long)(mime.dst.ptr - body));
		buffer_append(&dst, body, mime.dst.ptr - body);
		error_msg = NULL;
		break;
	} while (bend < src.end && mime.entities);

	buffer_free(&src);

	if (error_msg || mime.dst.error || dst.error) {
		mime_free(&mime);
		buffer_free(&dst);
		return html_error(error_msg);
	}

	mime_free(&mime);

	return html_send(&dst);
}

/* output From and Subject strings */
static void output_strings(struct buffer *dst, struct idx_message *m, int close_a)
{
	char *from, *subj;
	int from_len, subj_len;
	int trunc;

	from = m->strings;
	from_len = strnlen(from, sizeof(m->strings));

	subj = m->strings + from_len + 1;
	if (from_len + 1 < sizeof(m->strings))
		subj_len = strnlen(subj, sizeof(m->strings) - from_len - 1);
	else
		subj_len = 0;

	if (subj_len) {
		trunc = (m->flags & IDX_F_SUBJECT_TRUNC) | enc_utf8_remove_partial(subj, &subj_len);
		buffer_append_html(dst, subj, subj_len);
		if (trunc)
			buffer_appends(dst, "&hellip;");
	} else {
		buffer_appends(dst, "(no subject)");
	}
	if (close_a)
		buffer_appends(dst, "</a>");
	buffer_appends(dst, " (");
	trunc = (m->flags & IDX_F_FROM_TRUNC) | enc_utf8_remove_partial(from, &from_len);
	buffer_append_html(dst, from, from_len);
	if (trunc)
		buffer_appends(dst, "&hellip;");
	buffer_appends(dst, ")");
}

int html_day_index(const char *list, unsigned int y, unsigned int m, unsigned int d)
{
	unsigned int aday;
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

	fd = idx_open(list);
	if (fd < 0)
		return html_error(errno == ENOENT ?
		    "No such mailing list" : NULL);
	/* read two consecutive aday entries
	 * will need them to determine message count for this day */
	error = !idx_read_aday_ok(fd, aday, &mx, sizeof(mx));
	if (error || mx[0] < 1 || mx[0] >= MAX_MAILBOX_MESSAGES) {
		idx_close(fd);
		return html_error((error || mx[0] > 0) ? NULL : "No messages"
		    " for this day");
	}
	if (mx[1] > 0)
		count = mx[1] - mx[0];
	else
		count = -mx[1];
	size = count * sizeof(struct idx_message);
	idx_offset = IDX2MSG(mx[0] - 1);
	if (mx[0] > 1) {
		/* read one more entry for Prev day quick link */
		size += sizeof(struct idx_message);
		idx_offset -= sizeof(struct idx_message);
		prev = 1;
	} else {
		prev = 0;
	}

	/* read one more entry for Next day quick link */
	size_n = size + sizeof(struct idx_message);

	if (!(mp = malloc(size_n)) ||
	    (got = idx_read(fd, idx_offset, mp, size_n)) == -1 ||
	    (got != size && got != size_n)) {
		idx_close(fd);
		free(mp);
		return html_error("Index error");
	}
	next = (got == size_n) ? count + prev : 0;

	if (idx_close(fd) || error || buffer_init(&dst, 0)) {
		free(mp);
		return html_error(NULL);
	}

	buffer_appends(&dst, "\n");

	if (html_flags & HTML_HEADER) {
		buffer_appends(&dst, "<title>");
		buffer_appends_html(&dst, list);
		buffer_appendf(&dst, " mailing list - %u/%02u/%02u</title>\n", y, m, d);
	}

	if (html_flags & HTML_BODY) {
		int i;
		if (prev) {
			buffer_appends(&dst, "<a href=\"");
			buffer_appendf(&dst, "../../../%u/%02u/%02u/\">[&lt;prev day]</a> ",
			    MIN_YEAR + mp[0].y, mp[0].m, mp[0].d);
		}
		if (next) {
			buffer_appends(&dst, "<a href=\"");
			buffer_appendf(&dst, "../../../%u/%02u/%02u/\">[next day&gt;]</a> ",
			    MIN_YEAR + mp[next].y, mp[next].m, mp[next].d);
		}
		buffer_appends(&dst,
		    "<a href=\"..\">[month]</a>"
		    " <a href=\"../..\">[year]</a>"
		    " <a href=\"../../..\">[list]</a>\n");

		buffer_appends(&dst, "<p><h2>");
		buffer_appends_html(&dst, list);
		buffer_appendf(&dst, " mailing list - %u/%02u/%02u</h2>\n", y, m, d);

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

		buffer_appendf(&dst, "<p>%u message%s\n", count, count == 1 ? "" : "s");
	}

	free(mp);

	return html_send(&dst);
}

/* Tomohiko Sakamoto algorithm */
static int dayofweek(unsigned int y, unsigned int m, unsigned int d)
{
	static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};

	y -= m < 3;
	return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}

typedef enum { L_MONTHLY, L_DAILY } html_date_level_t;
static void html_output_month_cal(struct buffer *pdst, idx_msgnum_t *mn, unsigned int y, unsigned int m, html_date_level_t level)
{
	unsigned int d;
	idx_msgnum_t mp, count;

	/* "Monday is the first day of the week according to the international standard ISO 8601" */
	enum { SUNDAY, MONDAY } weekstart = MONDAY;
	buffer_appendf(pdst, "\n<table border=0 class=cal_mon><tr>%s<th>Mon<th>Tue<th>Wed<th>Thu<th>Fri<th>Sat%s",
	    weekstart == SUNDAY ? "<th>Sun" : "",
	    weekstart == MONDAY ? "<th>Sun" : "");

	int leapyear = y % 4 || (y % 100 == 0 && y % 400) ? 0 : 1;
	int daysinmonth = m == 2 ? 28 + leapyear : 31 - (m - 1) % 7 % 2;
	int firstday = dayofweek(y, m, 1);

	mp = mn[0];
	for (d = 1; d <= daysinmonth; d++) {
		unsigned int col = (7 + d + firstday - weekstart - 1) % 7;

		if (d == 1 || col == 0)
			buffer_appends(pdst, "\n<tr>");
		if (d == 1 && col > 0)
			buffer_appendf(pdst, "<td colspan=\"%u\">", col);
		buffer_appendf(pdst, "<td><sup>%u</sup>&nbsp;", d);
		if (mn[d]) {
			if (mp > 0) {
				if (mn[d] > 0)
					count = mn[d] - mp;
				else
					count = -mn[d];
				if (count <= 0)
					return;
				buffer_appends(pdst, "<a href=\"");
				if (level <= L_MONTHLY)
					buffer_appendf(pdst, "%02u/", m);
				buffer_appendf(pdst, "%02u/\">%u</a>", d, count);
			}
			mp = mn[d];
		}
		if (d == daysinmonth && 7 - col - 1 > 0)
			buffer_appendf(pdst, "<td colspan=\"%u\">", 7 - col - 1);
	}
	buffer_appends(pdst, "\n</table>\n");
}

int html_month_index(const char *list, unsigned int y, unsigned int m)
{
	unsigned int d, n, aday, dp;
	int fd;
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

	fd = idx_open(list);
	if (fd < 0)
		return html_error(errno == ENOENT ?
		    "No such mailing list" : NULL);

	if (!idx_read_aday_ok(fd, aday, mn, sizeof(mn))) {
		idx_close(fd);
		return html_error("Index error");
	}

	/* quickly calculate how many messages we have in this month */
	total = 0;
	first = 0;
	mp = mn[0];
	for (d = 1; d <= 31; d++) {
		if (!mn[d])
			continue;
		if (mp > 0) {
			/* Remember index of first message */
			if (first == 0)
				first = mp;
			count = (mn[d] > 0) ? mn[d] - mp : -mn[d];
			if (count <= 0) {
				buffer_free(&dst);
				idx_close(fd);
				return html_error(NULL);
			}
			total += count;
		}
		mp = mn[d];
	}
	/* have messages, allocate and read them */
	if (total && first) {
		off_t got;
		off_t idx_offset;

		first--;
		size = total * sizeof(struct idx_message);
		idx_offset = IDX2MSG(first);
		/* we need to read prev and next messages too */
		if (first) {
			size += sizeof(struct idx_message);
			idx_offset -= sizeof(struct idx_message);
			prev = 1;
		}
		size_n = size + sizeof(struct idx_message);

		if (!(msgp = malloc(size_n)) ||
		    (got = idx_read(fd, idx_offset, msgp, size_n)) == -1 ||
		    (got != size && got != size_n)) {
			idx_close(fd);
			free(msgp);
			return html_error("Index error");
		}
		msg = msgp + prev;
		next = (got == size_n) ? total + prev : 0;
	}

	if (idx_close(fd) || buffer_init(&dst, 0)) {
		free(msgp);
		return html_error(NULL);
	}

	buffer_appends(&dst, "\n");

	if (html_flags & HTML_HEADER) {
		buffer_appends(&dst, "<title>");
		buffer_appends_html(&dst, list);
		buffer_appendf(&dst, " mailing list - %u/%02u</title>\n", y, m);
	}

	if (html_flags & HTML_BODY) {
		if (prev) {
			buffer_appends(&dst, "<a href=\"");
			buffer_appendf(&dst, "../../%u/%02u/\">[&lt;prev month]</a> ",
			    MIN_YEAR + msgp[0].y, msgp[0].m);

		}
		if (next) {
			buffer_appends(&dst, "<a href=\"");
			buffer_appendf(&dst, "../../%u/%02u/\">[next month&gt;]</a> ",
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
			free(msgp);
			return html_error("No messages for this month");
		}

		html_output_month_cal(&dst, mn, y, m, L_DAILY);

		total = 0;
		dp = 0;
		mp = mn[0];
		for (d = 1; d <= 31; d++) {
			if (!mn[d])
				continue;
			if (mp > 0) {
				if (mn[d] > 0)
					count = mn[d] - mp;
				else
					count = -mn[d];
				if (count <= 0) {
					buffer_free(&dst);
					free(msgp);
					return html_error(NULL);
				}
				if (!total)
					buffer_appends(&dst, "<p>Messages by day:\n<p>\n");
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
					buffer_appendf(&dst, "<li><a href=\"%02u/%u\">", dp + 1, n);
					output_strings(&dst, msg++, 1);
					buffer_appends(&dst, "\n");
				}
				msg += count - maxn;
				if (count > MAX_SHORT_MSG_LIST)
					buffer_appendf(&dst, "<li><a href=\"%02u/\">%u more messages</a>\n", d, count - maxn);
				buffer_appends(&dst, "</ul>\n");
			}
			mp = mn[d];
			dp = d;
		}

		if (total)
			buffer_appendf(&dst, "<p>%u message%s\n", total, total == 1 ? "" : "s");
		else
			buffer_appends(&dst, "<p>No messages\n");
	}

	free(msgp);

	return html_send(&dst);
}

int html_year_index(const char *list, unsigned int y)
{
	unsigned int min_y, max_y, m, d, aday, rday;
	int fd;
	idx_msgnum_t *mn, count;
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

	fd = idx_open(list);
	if (fd < 0)
		return html_error(errno == ENOENT ? "No such mailing list" : NULL);

	if (!(mn = malloc(mn_size)) ||
	    !idx_read_aday_ok(fd, aday, mn, mn_size)) {
		idx_close(fd);
		free(mn);
		return html_error(NULL);
	}

	/* find first and next index for Prev and Next year links */
	int first = 0;
	int lastn = 0;
	rday = YMD2ADAY(min_y - MIN_YEAR, 1, 1) - aday;
	unsigned int eday = YMD2ADAY(max_y - MIN_YEAR + 1, 1, 1) - aday;
	int sanity = 0;
	for (; rday < eday; rday++) {
		if (mn[rday] > 0) {
			if (!first)
				first = mn[rday];
			if (mn[rday + 1] <= 0)
				lastn = mn[rday] + -mn[rday + 1];
			else
				lastn = mn[rday + 1];
			/* sanity check of index */
			if (lastn <= mn[rday] ||
			    ((mn[rday] != sanity) && sanity)) {
				buffer_free(&dst);
				free(mn);
				return html_error("Index corrupt");
			}
			sanity = lastn;
		}
	}
	int prev = 0;
	int next = 0;
	if (first || lastn) {
		struct idx_message msg;
		off_t size = sizeof(struct idx_message);
		if (first > 1) {
			if (!idx_read_msg_ok(fd, first - 2, &msg, size)) {
				free(mn);
				return html_error("Index error");
			}
			prev = MIN_YEAR + msg.y;
		}
		if (lastn > 1) {
			if (idx_read_msg_ok(fd, lastn, &msg, size))
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
		if (!(msg = malloc(size)) ||
		    !idx_read_msg_ok(fd, recent_offset - 1, msg, size))
			recent_count = 0;

		/* resolve to message number in the day and cache in offset field */
		rday = YMD2ADAY(min_y - MIN_YEAR, 1, 1) - aday;
		i = 0;
		for (; rday < eday; rday++) {
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
	}
	if (idx_close(fd) || buffer_init(&dst, 0)) {
		free(msg);
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
		if (prev)
			buffer_appendf(&dst, "<a href=\"../%u/\">[prev year]</a>\n", prev);
		if (next)
			buffer_appendf(&dst, "<a href=\"../%u/\">[next year]</a>\n", next);
		if (min_y == max_y)
			buffer_appends(&dst, "<a href=\"..\">[list]</a>\n");

		buffer_appends(&dst, "<p><h2>");
		buffer_appends_html(&dst, list);
		buffer_appends(&dst, " mailing list");
		if (min_y == max_y)
			buffer_appendf(&dst, " - %u", y);
		buffer_appends(&dst, "</h2>\n");

		idx_msgnum_t total = 0, monthly_total[12];
		/* output short year-o-month index */
		int o_header = 0;
		int o_year = 0;
		int o_month = -1;
		for (y = max_y; y >= min_y; y--) {
			rday = YMD2ADAY(y - MIN_YEAR, 1, 1) - aday;
			for (m = 1; m <= 12; m++) {
				monthly_total[m - 1] = 0;
				for (d = 1; d <= 31; d++, rday++) {
					if (mn[rday] <= 0)
						continue;
					if (mn[rday + 1] > 0)
						count = mn[rday + 1] - mn[rday];
					else
						count = -mn[rday + 1];
					monthly_total[m - 1] += count;
				}
				if (!monthly_total[m - 1])
					continue;
				if (!o_header) {
					buffer_appends(&dst,
					    "\n<table border=0 "
					    "class=cal_brief><tr><th>"
					    "<th>Jan<th>Feb<th>Mar"
					    "<th>Apr<th>May<th>Jun"
					    "<th>Jul<th>Aug<th>Sep"
					    "<th>Oct<th>Nov<th>Dec\n");
					o_header++;
				}
				if (o_year != y) {
					if (o_month >= 0) {
						for (o_month++; o_month <= 12; o_month++)
							buffer_appends(&dst, "<td>&nbsp;");
					}
					buffer_appendf(&dst, "\n<tr><td>");
					if (min_y != max_y)
						buffer_appendf(&dst, "<a href=\"%u/\">", y);
					buffer_appendf(&dst, "<b>%4u</b>", y);
					if (min_y != max_y)
						buffer_appends(&dst, "</a>");
					o_year = y;
					o_month = 0;
				}
				for (o_month++; o_month < m; o_month++)
					buffer_appends(&dst, "<td>&nbsp;");
				buffer_appendf(&dst, "<td><a href=\"");
				if (min_y != max_y)
					buffer_appendf(&dst, "%u/", y);
				buffer_appendf(&dst, "%02u/\">%u</a>", m, monthly_total[m - 1]);
				o_month = m;
				total += monthly_total[m - 1];
			}
		}
		if (o_header) {
			if (o_year) {
				for (o_month++; o_month <= 12; o_month++)
					buffer_appends(&dst, "<td>&nbsp;");
			}
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

		free(msg); msg = NULL;

		/* output monthly calendars */
		if (min_y == max_y) {
			y = min_y;
			buffer_appends(&dst, "\n<p>\n<table border=0 class=cal_big>");
			for (m = 1; m <= 12; m++) {
				rday = YMD2ADAY(y - MIN_YEAR, m, 1) - aday;
				if (m % 3 == 1) {
					unsigned int n;
					buffer_appends(&dst, "\n<tr>");
					for (n = m; n < m + 3; n++) {
						if (monthly_total[n - 1])
							buffer_appendf(&dst, "<th><a href=\"%02u/\">%s</a>",
							    n, month_name[n - 1]);
						else
							buffer_appendf(&dst, "<th>%s", month_name[n - 1]);
					}
					buffer_appends(&dst, "\n<tr>");
				}

				buffer_appends(&dst, "<td valign=\"top\">");
				html_output_month_cal(&dst, &mn[rday], y, m, L_MONTHLY);
			}
			buffer_appends(&dst, "</table>");
		}

		if (total)
			buffer_appendf(&dst, "<p>%u message%s\n", total, total == 1 ? "" : "s");
		else
			buffer_appends(&dst, "<p>No messages\n");
	} /* HTML_BODY */

	free(msg);
	free(mn);

	return html_send(&dst);
}
