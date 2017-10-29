/*
 * MIME message parsing.
 *
 * Written by Solar Designer <solar at openwall.com> in 2006.
 * No copyright is claimed, and the software is hereby placed in the public
 * domain.  In case this attempt to disclaim copyright and place the software
 * in the public domain is deemed null and void, then the software is
 * Copyright (c) 2006 Solar Designer and it is hereby released to the
 * general public under the following terms:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted.
 *
 * There's ABSOLUTELY NO WARRANTY, express or implied.
 */

#include <string.h>
#include <stdlib.h>

#include "buffer.h"
#include "mime.h"
#include "encoding.h"

static int new_entity(struct mime_ctx *ctx)
{
	struct mime_entity *entity;

	if (ctx->depth >= MIME_DEPTH_MAX ||
	    !(entity = malloc(sizeof(*entity))))
		return ctx->dst.error = -1;

	entity->next = ctx->entities;
	entity->type = "text/plain";
	entity->boundary = NULL;
	entity->encoding = NULL;
	entity->charset = NULL;
	ctx->entities = entity;
	ctx->depth++;

	return 0;
}

int mime_init(struct mime_ctx *ctx, struct buffer *src)
{
	ctx->entities = NULL;
	ctx->depth = 0;
	ctx->src = src;

	if (buffer_init(&ctx->dst, src->end - src->ptr)) return -1;
	if (buffer_init(&ctx->enc, ICONV_BUF_SIZE)) return -1;

	if (new_entity(ctx)) {
		buffer_free(&ctx->dst);
		buffer_free(&ctx->enc);
		return -1;
	}

	return 0;
}

/* free all entities up to specified */
static void free_entities_to(struct mime_ctx *ctx, struct mime_entity *end)
{
	struct mime_entity *entity;

	while ((entity = ctx->entities) != end) {
		ctx->entities = entity->next;
		free(entity);
		ctx->depth--;
	}
}

void mime_free(struct mime_ctx *ctx)
{
	free_entities_to(ctx, NULL);
	buffer_free(&ctx->dst);
}

/* eat one header out of src, multi-line if need,
 * return pointer to it */
char *mime_skip_header(struct mime_ctx *ctx)
{
	char *p, *q, *end;

	p = ctx->src->ptr;
	end = ctx->src->end;
	if (p >= end)
		/* end of message */
		return NULL;

	q = memchr(p, '\n', end - p);
	if (q == p) {
		/* end of headers */
		ctx->src->ptr = ++p;
		return NULL;
	}

	while (q) {
		q++;
		if (q == end || (*q != '\t' && *q != ' ')) {
			ctx->src->ptr = q;
			return p;
		}
		/* multi-line header */
		q = memchr(q, '\n', end - q);
	}

	/* no newline after last header - and no body */
	ctx->src->ptr = end;
	return p;
}

/* Content-Transfer-Encoding: {7bit,8bit,quoted-printable,base64} */
/* Content-Type: text/plain; charset="us-ascii"; delsp=yes; format=flowed */
/* Content-Type: multipart/mixed; boundary="xxxxxxxxxxxxxxxx" */

/* process header field with mime meaning */
static void process_header(struct mime_ctx *ctx, char *header)
{
	struct mime_entity *entity;
	char *p, *a, *v;
	int multipart = 0;

	if (!strncasecmp(header, "Content-Transfer-Encoding:", 26)) {
		p = header + 26;
		while (*p == ' ' || *p == '\t' || *p == '\n') p++;
		ctx->entities->encoding = p;
		return;
	}

	if (strncasecmp(header, "Content-Type:", 13)) return;

	entity = ctx->entities;
	entity->boundary = NULL;

	p = header + 13;
	while (*p == ' ' || *p == '\t' || *p == '\n') p++;
	entity->type = p;
	while (*p && *p != ';') p++;
	if (!*p) return;

	*p++ = '\0';
	if (!strncasecmp(entity->type, "multipart/", 10))
		multipart++;

	do {
		while (*p == ' ' || *p == '\t' || *p == '\n') p++;
		a = p;
		while (*p && *p != '=') p++;
		if (!*p) return;
		*p++ = '\0';
		if (*p == '"') {
			v = ++p;
			while (*p && *p != '"') p++;
			if (!*p) return;
			*p++ = '\0';
		} else
			v = p;
		while (*p && *p != ';') p++;
		if (*p) *p++ = '\0';
		if (multipart && !strcasecmp(a, "boundary"))
			entity->boundary = v;
		else if (!strcasecmp(a, "charset"))
			entity->charset = v;
		if (entity->boundary && entity->charset)
			return;
	} while (1);
}

/* from `encoded' to `dst' */
static void decode_qp(struct buffer *dst, char *encoded, size_t length,
    int header)
{
	unsigned char c, *p, *end;
	unsigned int v;

	p = (unsigned char *)encoded;
	end = p + length;

	while (p < end) {
		c = *p++;
		if (c == '_' && header)
			c = 0x20; /* "hexadecimal 20" as per RFC 2047 */
		else if (c == '=' && p < end) {
			c = *p++;
			if (c == '\n') continue;
			if (c >= '0' && c <= '9' && p < end)
				v = c - '0';
			else if (c >= 'A' && c <= 'F' && p < end)
				v = c - ('A' - 10);
			else {
				buffer_appendc(dst, '=');
				p--;
				continue;
			}
			v <<= 4;
			c = *p++;
			if (c >= '0' && c <= '9')
				v |= c - '0';
			else if (c >= 'A' && c <= 'F')
				v |= c - ('A' - 10);
			else {
				buffer_appendc(dst, '=');
				p -= 2;
				continue;
			}
			c = v;
		}
		buffer_appendc(dst, c);
	}
}

/* from `encoded' to `dst' */
static void decode_base64(struct buffer *dst, char *encoded, size_t length)
{
	unsigned char c, *p, *end;
	unsigned int i, v;

	p = (unsigned char *)encoded;
	end = p + length;

	while (p < end) {
		c = *p++;
		if (c == '\n') continue;

		if (end - p < 3) return;
		i = 0;
		v = 0;
		do {
			if (c >= 'A' && c <= 'Z')
				v |= c - 'A';
			else if (c >= 'a' && c <= 'z')
				v |= c - ('a' - 26);
			else if (c >= '0' && c <= '9')
				v |= c - ('0' - 52);
			else if (c == '+')
				v |= 62;
			else if (c == '/')
				v |= 63;
			else if (c == '=')
				break;
			else
				return;
			if (++i >= 4) break;
			v <<= 6;
			c = *p++;
		} while (1);

		switch (i) {
		case 4:
			buffer_appendc(dst, v >> 16);
			buffer_appendc(dst, v >> 8);
			buffer_appendc(dst, v);
			continue;
		case 3:
			buffer_appendc(dst, v >> 16);
			buffer_appendc(dst, v >> 8);
			return;
		case 2:
			buffer_appendc(dst, v >> 10);
		default:
			return;
		}
	}
}

static inline int istokenchar(char ch)
{
	/* rfc2047#section-2 token */
	switch (ch) {
		/* SPACE */
		case ' ':
		/* CTLs */
		case '\0' ... '\037':
		case '\177':
		/* especials */
		case '(':
		case ')':
		case ',':
		case '.':
		case '/':
		case ':' ... '@': /* :;<=>?@ */
		case '[':
		case ']':
			return 0;
		default:
			return 1;
	}
}

static inline int isencodedchar(unsigned char ch)
{
	/* rfc2047#section-2 encoded-text */
	if (ch < 33 || ch > 126 || /* non-printable ASCII */
	    ch == '?')
		return 0;
	return 1;
}

static inline int islinearwhitespace(char ch)
{
	switch (ch) {
		case ' ':
		case '\t':
		case '\r':
		case '\n':
			return 1;
		default:
			return 0;
	}
}

/* decode mime-encoded-words, ex: =?charset?encoding?encoded_text?= */
void decode_header(struct mime_ctx *ctx, char *header, size_t length)
{
	char *done, *p, *q, *end, *charset, *encoding;
	struct buffer *dst = &ctx->dst;

	done = p = header;
	end = header + length;

	while (p < end) {
		char *r;

		if (*p++ != '=') continue;
		if (p >= end) break;
		if (*p != '?') continue;
		q = p;
		charset = ++q;
		if (q >= end) continue;
		if (!istokenchar(*q++)) continue;
		for (; q < end && istokenchar(*q); q++);
		if (q >= end) continue;
		if (*q++ != '?') continue;
		if (q >= end) continue;
		if (*q != 'q' && *q != 'Q' && *q != 'B' && *q != 'b')
			continue;
		encoding = q++;
		if (q >= end) continue;
		if (*q++ != '?') continue;
		if (q >= end) continue;
		if (!isencodedchar(*q++)) continue;
		for (; q < end && isencodedchar(*q); q++);
		if (q >= end) continue;
		if (*q++ != '?') continue;
		if (q >= end) continue;
		if (*q != '=') continue;
		if (q + 1 - (p - 1) > 75) continue;
		/* skip adjacent linear-white-space between previous encoded-word */
		r = --p - 1;
		if (done != header) {
			for (; r >= done && islinearwhitespace(*r); r--);
			if (r >= done)
				r = p - 1;
		}
		buffer_append(dst, done, r + 1 - done);
		done = ++q;
		if (*encoding == 'Q' || *encoding == 'q') {
			decode_qp(&ctx->enc, encoding + 2, q - encoding - 4, 1);
			encoding_to_utf8(dst, &ctx->enc, charset);
		} else if (*encoding == 'B' || *encoding == 'b') {
			decode_base64(&ctx->enc, encoding + 2, q - encoding - 4);
			encoding_to_utf8(dst, &ctx->enc, charset);
		}
		p = done;
	}

	buffer_append(dst, done, end - done);
}

/* get(src), decode, and parse one header field (can be multi-line), put(dst) */
char *mime_decode_header(struct mime_ctx *ctx)
{
	char *header;
	size_t length, dst_offset;

	header = mime_skip_header(ctx);
	if (!header)
		return NULL;

	length = ctx->src->ptr - header;
	if (length > 0 && header[length - 1] == '\n')
		length--;

	dst_offset = ctx->dst.ptr - ctx->dst.start;

	decode_header(ctx, header, length);
	buffer_append(&ctx->dst, "", 1);
	if (ctx->dst.error)
		return NULL;

	header = ctx->dst.start + dst_offset;

	if (*header == 'C' || *header == 'c')
		process_header(ctx, header);

	return header;
}

/* find boundary separator, return pointer to it, or `end', or NULL(error) */
static char *find_next_boundary(struct mime_ctx *ctx, int pre)
{
	struct mime_entity *entity;
	char *p, *end;
	size_t length;

	end = ctx->src->end;

	if (!ctx->entities)
		return end;

	p = ctx->src->ptr;
	do {
		if (end - p < 3)
			break;
		if (p[0] == '-' && p[1] == '-') {
			p += 2;
			entity = ctx->entities;
			do {
/* We may be called for multipart entities only */
				if (!entity->boundary) {
					ctx->dst.error = -1;
					return NULL;
				}
				length = strlen(entity->boundary);
				if (length > end - p ||
				    memcmp(p, entity->boundary, length))
					continue;
				if (length + 2 <= end - p &&
				    p[length] == '-' && p[length + 1] == '-') {
					/* terminating */
					free_entities_to(ctx, entity->next);
					if (pre)
						return ctx->src->ptr = p - 2;
					if (ctx->entities) break;
					return end;
				}
				free_entities_to(ctx, entity);
				if (!pre && new_entity(ctx)) return NULL;
				p -= 2;
				if (pre && p > ctx->src->ptr) p--;
				return ctx->src->ptr = p;
			} while ((entity = entity->next));
		}
		p = memchr(p, '\n', end - p);
		if (!p) break;
		p++;
	} while (1);

	return end;
}

/* find next mime part */
char *mime_next_body_part(struct mime_ctx *ctx)
{
	return find_next_boundary(ctx, 0);
}

/* parse headers of current mime part and return pointer to its body */
char *mime_next_body(struct mime_ctx *ctx)
{
	while (ctx->src->ptr < ctx->src->end) {
		switch (*ctx->src->ptr) {
		case 'C':
		case 'c':
			mime_decode_header(ctx);
			continue;
		case '\n':
			return ++ctx->src->ptr;
		}
		mime_skip_header(ctx);
	}

	return ctx->src->ptr;
}

/* don't parse this mime body */
char *mime_skip_body(struct mime_ctx *ctx)
{
/* Forget the last non-multipart content type processed */
	if (!ctx->entities->boundary)
		free_entities_to(ctx, ctx->entities->next);

	return find_next_boundary(ctx, 1);
}

/* don't skip this mime body */
char *mime_decode_body(struct mime_ctx *ctx)
{
	char *body, *bend, *encoding, *charset;
	size_t length, dst_offset;

	encoding = ctx->entities->encoding;
	charset = ctx->entities->charset;

	body = ctx->src->ptr;
	bend = mime_skip_body(ctx);
	if (!bend)
		return NULL;

	length = bend - body;

	dst_offset = ctx->dst.ptr - ctx->dst.start;

	if (encoding && !strcasecmp(encoding, "quoted-printable"))
		decode_qp(&ctx->enc, body, length, 0);
	else if (encoding && !strcasecmp(encoding, "base64"))
		decode_base64(&ctx->enc, body, length);
	else
		buffer_append(&ctx->enc, body, length);
	encoding_to_utf8(&ctx->dst, &ctx->enc, charset);
	if (ctx->dst.error)
		return NULL;

	return ctx->dst.start + dst_offset;
}
