#include <string.h>
#include <stdlib.h>

#include "buffer.h"
#include "mime.h"

int mime_init(struct mime_ctx *ctx, struct buffer *src)
{
	ctx->src = src;
	ctx->entities = NULL;
	ctx->depth = 0;
	return buffer_init(&ctx->dst, src->end - src->ptr);
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

static void mime_process_header(struct mime_ctx *ctx, char *header)
{
	struct mime_entity *entity;
	char *p, *a, *v;

	if (strncasecmp(header, "Content-Type:", 13)) return;

	/* XXX: allocate new entities as boundaries are seen, not here */
	if (ctx->depth >= MIME_DEPTH_MAX ||
	    !(entity = malloc(sizeof(*entity)))) {
		ctx->dst.error = -1;
		return;
	}

	entity->next = ctx->entities;
	entity->boundary = NULL;
	ctx->entities = entity;
	ctx->depth++;

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

char *mime_decode_header(struct mime_ctx *ctx)
{
	char *src_header, *dst_header;
	size_t src_length, dst_offset;

	src_header = mime_skip_header(ctx);
	if (!src_header)
		return NULL;

	src_length = ctx->src->ptr - src_header;
	if (src_length > 0 && src_header[src_length - 1] == '\n')
		src_length--;

	dst_offset = ctx->dst.ptr - ctx->dst.start;

/* XXX: actually decode it */
	buffer_append(&ctx->dst, src_header, src_length);
	buffer_append(&ctx->dst, "", 1);
	if (ctx->dst.error)
		return NULL;

	dst_header = ctx->dst.start + dst_offset;

	if (*dst_header == 'C' || *dst_header == 'c')
		mime_process_header(ctx, dst_header);

	return dst_header;
}

char *mime_next_body_part(struct mime_ctx *ctx)
{
	struct mime_entity *entity;
	char *p, *end;
	size_t length;

/* Forget the last non-multipart content type processed */
	if (ctx->entities && !ctx->entities->boundary)
		free_entities_to(ctx, ctx->entities->next);

/* If the current body part is not multipart, we have nothing to do */
	if (!ctx->entities || !ctx->entities->boundary)
		return ctx->src->ptr;

	p = ctx->src->ptr;
	end = ctx->src->end;
	do {
		if (end - p < 3)
			break;
		if (p[0] == '-' && p[1] == '-') {
			p += 2;
			entity = ctx->entities;
			do {
				if (!entity->boundary) continue;
				length = strlen(entity->boundary);
				if (length <= end - p &&
				    !memcmp(p, entity->boundary, length)) {
					free_entities_to(ctx, entity);
					return ctx->src->ptr = p - 2;
				}
			} while ((entity = entity->next));
		}
		p = memchr(p, '\n', end - p);
		if (!p)
			break;
		p++;
	} while (1);

	return end;
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
