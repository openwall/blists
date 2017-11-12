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

#ifndef _BLISTS_MIME_H
#define _BLISTS_MIME_H

#include "buffer.h"

#define MIME_DEPTH_MAX			10

struct mime_entity {
	struct mime_entity *next;
	char *type, *boundary, *encoding, *charset, *filename;
	enum {
		CONTENT_UNSET = 0,
		CONTENT_INLINE,
		CONTENT_ATTACHMENT
	} disposition;
};

struct mime_ctx {
	struct buffer *src, dst, enc;
	struct mime_entity *entities;
	int depth;
};

extern int mime_init(struct mime_ctx *ctx, struct buffer *src);
extern void mime_free(struct mime_ctx *ctx);

extern char *mime_skip_header(struct mime_ctx *ctx);
extern char *mime_decode_header(struct mime_ctx *ctx);

extern char *mime_next_body_part(struct mime_ctx *ctx);
extern char *mime_next_body(struct mime_ctx *ctx);
extern char *mime_skip_body(struct mime_ctx *ctx);
typedef enum { RECODE_YES, RECODE_NO } mime_recode_t;
extern char *mime_decode_body(struct mime_ctx *ctx, mime_recode_t recode, char **bendp);

#endif
