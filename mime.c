#include <string.h>
#include <stdlib.h>

#include "buffer.h"
#include "mime.h"

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

	if (new_entity(ctx)) {
		buffer_free(&ctx->dst);
		return -1;
	}

	return 0;
}

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

static void process_header(struct mime_ctx *ctx, char *header)
{
	struct mime_entity *entity;
	char *p, *a, *v;

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
	if (strncasecmp(entity->type, "multipart/", 10)) return;

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
		if (!strcasecmp(a, "boundary")) {
			entity->boundary = v;
			return;
		}
	} while (1);
}

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

static void decode_header(struct buffer *dst, char *header, size_t length)
{
	char *done, *p, *q, *end, *charset, *encoding;

	done = p = header;
	end = header + length;

	while (p < end) {
		if (*p++ != '=') continue;
		if (p >= end) break;
		if (*p != '?') continue;
		q = p;
		charset = ++q;
		while (q < end && *q++ != '?');
		if (q >= end) continue;
		encoding = q++;
		if (q >= end) continue;
		if (*q++ != '?') continue;
		while (q < end && *q++ != '?');
		if (q >= end) continue;
		if (*q != '=') continue;
		buffer_append(dst, done, --p - done);
		done = ++q;
		if (*encoding == 'Q' || *encoding == 'q')
			decode_qp(dst, encoding + 2, q - encoding - 4, 1);
		else if (*encoding == 'B' || *encoding == 'b')
			decode_base64(dst, encoding + 2, q - encoding - 4);
		else
			done = p++;
		p = done;
	}

	buffer_append(dst, done, end - done);
}

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

	decode_header(&ctx->dst, header, length);
	buffer_append(&ctx->dst, "", 1);
	if (ctx->dst.error)
		return NULL;

	header = ctx->dst.start + dst_offset;

	if (*header == 'C' || *header == 'c')
		process_header(ctx, header);

	return header;
}

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

char *mime_next_body_part(struct mime_ctx *ctx)
{
	return find_next_boundary(ctx, 0);
}

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

char *mime_skip_body(struct mime_ctx *ctx)
{
/* Forget the last non-multipart content type processed */
	if (!ctx->entities->boundary)
		free_entities_to(ctx, ctx->entities->next);

	return find_next_boundary(ctx, 1);
}

char *mime_decode_body(struct mime_ctx *ctx)
{
	char *body, *bend, *encoding;
	size_t length, dst_offset;

	encoding = ctx->entities->encoding;

	body = ctx->src->ptr;
	bend = mime_skip_body(ctx);
	if (!bend)
		return NULL;

	length = bend - body;

	dst_offset = ctx->dst.ptr - ctx->dst.start;

	if (encoding && !strcasecmp(encoding, "quoted-printable"))
		decode_qp(&ctx->dst, body, length, 0);
	else if (encoding && !strcasecmp(encoding, "base64"))
		decode_base64(&ctx->dst, body, length);
	else
		buffer_append(&ctx->dst, body, length);
	if (ctx->dst.error)
		return NULL;

	return ctx->dst.start + dst_offset;
}
