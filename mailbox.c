/*
 * Initial mbox file parsing.
 *
 * Copyright (c) 1998-2003,2006,2008,2010,2011,2015 Solar Designer <solar at openwall.com>
 * Copyright (c) 2008 Grigoriy Strokin <grg at openwall.com>
 * Copyright (c) 2011,2017 ABC <abc at openwall.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted.
 *
 * There's ABSOLUTELY NO WARRANTY, express or implied.
 */

#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include "md5/md5.h"

#include "params.h"
#include "index.h"
#include "buffer.h"
#include "mime.h"
#include "misc.h"
#include "mailbox.h"

/*
 * For each absolute day number ("aday"), num_by_aday[] elements can be:
 * > 0 meaning 1-based starting message number for the day or
 * <= 0 meaning sign-changed message count for the previous day.
 * In either case, it is trivial to determine the message count from two
 * adjacent elements.  We allocate one more element (beyond N_ADAY) to
 * possibly hold the sign-changed message count for the last day.
 */
static idx_msgnum_t num_by_aday[N_ADAY + 1];

static idx_msgnum_t msg_num;	/* number of actual messages in msgs[] */
static idx_msgnum_t msg_alloc;	/* (pre)allocated size of msgs[] (in messages) */
static struct idx_message *msgs; /* flat array */
static const char *list;

struct mem_message {
	struct idx_message *msg;
	struct mem_message *next_hash;
};

struct parsed_message {
	idx_off_t raw_offset;	/* Raw, with the "From " line */
	idx_off_t data_offset;	/* Just the message itself */
	idx_size_t raw_size;
	idx_size_t data_size;
	struct tm tm;
	idx_hash_t msgid_hash, irt_hash;
	int have_msgid, have_irt;
	const char *from, *subject;
};

/* allocate new message in msgs[] */
/* maintains msg_num global counter */
static struct idx_message *msgs_grow(void)
{
	struct idx_message *new_msgs;
	idx_msgnum_t new_num, new_alloc;
	size_t new_size;

	new_num = msg_num + 1;
	if (new_num <= 0)
		return NULL;

	if (new_num > msg_alloc) {
		new_alloc = msg_alloc + MSG_ALLOC_STEP;
		if (new_num > new_alloc)
			return NULL;
		new_size = (size_t)new_alloc * sizeof(struct idx_message);
		if (new_size / sizeof(struct idx_message) != new_alloc)
			return NULL;
		new_msgs = realloc(msgs, new_size);
		if (!new_msgs)
			return NULL;
		msgs = new_msgs;
		msg_alloc = new_alloc;
	}

	return &msgs[msg_num++];
}

/* convert parsed_message into idx_message and append it into msgs[] */
static int message_process(struct parsed_message *msg)
{
	struct idx_message *idx_msg;
	char *p;
	size_t left;

	idx_msg = msgs_grow();
	if (!idx_msg)
		return -1;

	memset(idx_msg, 0, sizeof(*idx_msg));

	idx_msg->offset = msg->data_offset;
	idx_msg->size = msg->data_size;

	if (msg->tm.tm_year >= (MIN_YEAR - 1900) &&
	    msg->tm.tm_year <= (MAX_YEAR - 1900)) {
		idx_msg->y = msg->tm.tm_year - (MIN_YEAR - 1900);
		idx_msg->m = msg->tm.tm_mon + 1;
		idx_msg->d = msg->tm.tm_mday;
	} else {
		idx_msg->y = 0;
		idx_msg->m = 1;
		idx_msg->d = 1;
	}

	if (msg->have_msgid) {
		memcpy(idx_msg->msgid_hash, msg->msgid_hash,
		    sizeof(idx_msg->msgid_hash));
		idx_msg->flags = IDX_F_HAVE_MSGID;
	}
	if (msg->have_irt) {
		memcpy(idx_msg->irt_hash, msg->irt_hash,
		    sizeof(idx_msg->irt_hash));
		idx_msg->flags |= IDX_F_HAVE_IRT;
	}

	p = idx_msg->strings;
	left = sizeof(idx_msg->strings);
	if (msg->from) {
		size_t n = strlen(msg->from) + 1;
		if (n > left) {
			if (n - left > 1)
				idx_msg->flags |= IDX_F_FROM_TRUNC;
			n = left;
		}
		memcpy(p, msg->from, n);
		p += n;
		left -= n;
	} else {
		*p++ = 0;
		left--;
	}
	if (msg->subject) {
		size_t n = strlen(msg->subject) + 1;
		if (n > left) {
			if (left < IDX_SUBJECT_MINGUALEN) {
				/* extend buffer at the cost of the "From" */
				size_t m = IDX_SUBJECT_MINGUALEN;
				if (m > n)
					m = n;
				p -= m - left;
				*(p - 1) = 0;
				left = m;
				idx_msg->flags |= IDX_F_FROM_TRUNC;
			}
			if (n - left > 1)
				idx_msg->flags |= IDX_F_SUBJECT_TRUNC;
			n = left;
		}
		if (n)
			memcpy(p, msg->subject, n);
	}

	return 0;
}

/* rebuild next/prev thread links */
static int msgs_link(void)
{
	idx_msgnum_t i;
	struct idx_message *m, *lit, *seen;
	unsigned int aday;
	struct mem_message *pool, **hash, *irt;
	unsigned int hv;
	unsigned int count;

	pool = calloc(msg_num, sizeof(*pool));
	if (!pool)
		return -1;
	hash = calloc(0x10000, sizeof(*hash));
	if (!hash) {
		free(pool);
		return -1;
	}

	for (i = 0, m = msgs; i < msg_num; i++, m++) {
		/* The following assignment eliminates link cycles that may
		 * cause an infinite loop in incremental mode. */
		m->t.nn = m->t.pn = 0;
		if (!(m->flags & IDX_F_HAVE_MSGID))
			continue;
		pool[i].msg = m;
		hv = m->msgid_hash[0] | ((unsigned int)m->msgid_hash[1] << 8);
		pool[i].next_hash = hash[hv];
		hash[hv] = &pool[i];
	}

	for (i = 0, m = msgs; i < msg_num; i++, m++) {
		if (!(m->flags & IDX_F_HAVE_IRT))
			continue;
		hv = m->irt_hash[0] | ((unsigned int)m->irt_hash[1] << 8);
		irt = hash[hv];
		while (irt) {
			if (!memcmp(m->irt_hash, irt->msg->msgid_hash, sizeof(idx_hash_t)) && m != irt->msg)
				break;
			irt = irt->next_hash;
		}
		if (!irt) continue;

/* The following loop could be avoided by maintaining a thread index,
 * including a "last in thread" pointer for each thread, and pointers
 * from each message into the thread index. */
		lit = seen = irt->msg;
		count = 0;
		while (lit->t.nn) {
			aday = YMD2ADAY(lit->t.ny, lit->t.nm, lit->t.nd);
			lit = &msgs[num_by_aday[aday] + lit->t.nn - 2];
			if (lit == seen)
				break;
			if (!((count + 1) & count))
				seen = lit;
			count++;
		}
		if (lit->t.nn)
			continue;
		aday = YMD2ADAY(lit->y, lit->m, lit->d);
		m->t.py = lit->y;
		m->t.pm = lit->m;
		m->t.pd = lit->d;
		m->t.pn = lit - msgs + 2 - num_by_aday[aday];
		aday = YMD2ADAY(m->y, m->m, m->d);
		lit->t.ny = m->y;
		lit->t.nm = m->m;
		lit->t.nd = m->d;
		lit->t.nn = i + 2 - num_by_aday[aday];
	}

	free(hash);
	free(pool);

	return 0;
}

static int cmp_msgs_by_day(const void *p1, const void *p2)
{
	const struct idx_message *m1 = p1, *m2 = p2;

	if (m1->y != m2->y)
		return (int)m1->y - (int)m2->y;
	if (m1->m != m2->m)
		return (int)m1->m - (int)m2->m;
	return (int)m1->d - (int)m2->d;
}

/* update messages-per-day array and rebuild thread links */
static int msgs_final(idx_msgnum_t start_from)
{
	idx_msgnum_t i;
	struct idx_message *m;
	unsigned int aday, prev_aday;

retry:
	prev_aday = 0;
	if (start_from) {
		for (i = 0; i < N_ADAY; i++) {
			if (num_by_aday[i] > 0)
				prev_aday = i;
		}
	} else {
		memset(num_by_aday, 0, sizeof(num_by_aday));
	}

	for (i = start_from, m = msgs + start_from; i < msg_num; i++, m++) {
		aday = YMD2ADAY(m->y, m->m, m->d);

		if (aday < prev_aday) {
			fprintf(stderr, "Warning: date went backwards: "
			    "%u -> %u (%04u/%02u/%02u), sorting... ",
			    prev_aday, aday, MIN_YEAR + m->y, m->m, m->d);
			qsort(msgs, msg_num, sizeof(*msgs), cmp_msgs_by_day);
			fprintf(stderr, "done\n");
			start_from = 0;
			goto retry;
		}
		prev_aday = aday;

		if (num_by_aday[aday] <= 0)
			num_by_aday[aday] = i + 1;
		if (num_by_aday[++aday] <= 0)
			num_by_aday[aday]--;
	}

	return msgs_link();
}

/*
 * Checks if the buffer pointed to by s1, of n1 chars, starts with the
 * string s2, of n2 chars.
 */
static inline int eq(const char *s1, size_t n1, const char *s2, size_t n2)
{
	if (n1 < n2)
		return 0;
	if (!memcmp(s1, s2, n2))
		return 1;
	return !strncasecmp(s1, s2, n2);
}

/* read existing index file into memory (which is num_by_aday[] and msgs[]) */
/* returns offset up to which the mailbox was indexed so far */
static off_t begin_inc_idx(int idx_fd, int fd)
{
	struct idx_message m;
	struct idx_message *mptr;
	off_t mailbox_size;
	off_t inc_ofs = 0;
	int error = 0;

	/* read messages-per-day array */
	if (read_loop(idx_fd, &num_by_aday, sizeof(num_by_aday)) != sizeof(num_by_aday))
		return 0;

	msg_num = 0;
	msg_alloc = 0;
	msgs = NULL;

	/*
	 * We cannot just get the last index entry to detect the offset up to
	 * which the mailbox was indexed so far: the order of index entries
	 * may have been changed by qsort() called from msgs_final().
	 *
	 * This will need to be re-worked to not read message structs one by
	 * one (inefficient).
	 */
	while (read_loop(idx_fd, &m, sizeof(m)) == sizeof(m)) {
		off_t new_inc_ofs = m.offset + m.size + 1;
		if (new_inc_ofs > inc_ofs)
			inc_ofs = new_inc_ofs;
		mptr = msgs_grow();
		if (!mptr) {
			error = 1;
			break;
		}
		memcpy(mptr, &m, sizeof(m));
	}
	if (!error) {
		if ((mailbox_size = lseek(fd, 0, SEEK_END)) < 0)
			return -1;
		if (mailbox_size < inc_ofs) {
/* XXX: This is also triggered when the mbox doesn't end with an empty line */
			fprintf(stderr, "Warning: mailbox size reduced, "
			    "performing full indexing\n");
			error = 1;
		}
	}
	if (error) {
		free(msgs);
		return 0;
	}

	return inc_ofs;
}

static void message_header_hash(const char *p, const char *q, idx_hash_t *hash)
{
	MD5_CTX ctx;
	unsigned char hash_full[16];
	MD5_Init(&ctx);
	MD5_Update(&ctx, p, q - p);
	MD5_Final(hash_full, &ctx);
	memcpy(hash, hash_full, sizeof(*hash));
}

/*
 * The mailbox parsing routine.
 * We implement a state machine at the line fragment level (that is, full or
 * partial lines).  This is faster than dealing with individual characters
 * (we leave that job for libc) and it doesn't require ever loading entire
 * lines into memory.
 */
static int mailbox_parse_fd(int fd)
{
	struct stat stat;			/* File information */
	struct parsed_message msg;		/* Message being parsed */
	struct buffer premime;			/* Buffered raw headers */
	struct mime_ctx mime;			/* MIME decoding context */
	char *file_buffer, *line_buffer;	/* Our internal buffers */
	off_t file_offset, line_offset;		/* Their offsets in the file */
	off_t offset;				/* A line fragment's offset */
	char *current, *next, *line;		/* Line pointers */
	int block, saved, extra, length;	/* Internal block sizes */
	int done, start, end;			/* Various boolean flags: */
	int blank, header, body;		/* the state information */
	off_t unindexed_size, inc_ofs;

	inc_ofs = lseek(fd, 0, SEEK_CUR);
	if (inc_ofs < 0 || fstat(fd, &stat))
		return 1;
	unindexed_size = stat.st_size - inc_ofs;
	if (!unindexed_size)
		return 0;
	if (unindexed_size < 0 || !S_ISREG(stat.st_mode) ||
	    stat.st_size > MAX_MAILBOX_BYTES || (stat.st_size >> (sizeof(off_t) * 8 - 1)))
		return 1;

	memset(&msg, 0, sizeof(msg));

	file_buffer = malloc(FILE_BUFFER_SIZE + LINE_BUFFER_SIZE);
	if (!file_buffer)
		return 1;
	line_buffer = &file_buffer[FILE_BUFFER_SIZE];

	if (buffer_init(&premime, 0)) {
		free(file_buffer);
		return 1;
	}
	if (mime_init(&mime, &premime)) {
		buffer_free(&premime);
		free(file_buffer);
		return 1;
	}

	file_offset = line_offset = offset = inc_ofs;	/* Start at inc_ofs, */
	current = file_buffer; block = 0; saved = 0;	/* and empty buffers */

	done = 0;	/* Haven't reached EOF or the original size yet */
	end = 1;	/* Assume we've just seen a LF: parse a new line */
	blank = 1;	/* Assume we've seen a blank line: look for "From " */
	header = 0;	/* Not in message headers, */
	body = 0;	/* and not in message body */

/*
 * The main loop.  Its first part extracts the line fragments, while the
 * second one manages the state flags and performs whatever is required
 * based on the state.  Unfortunately, splitting this into two functions
 * didn't seem to simplify the code.
 */
	do {
/*
 * Part 1.
 * The line fragment extraction.
 */

/* Look for the next LF in the file buffer */
		if ((next = memchr(current, '\n', block))) {
/* Found it: get the length of this piece, and check for buffered data */
			length = ++next - current;
			if (saved) {
/* Have this line's beginning in the line buffer: combine them */
				extra = LINE_BUFFER_SIZE - saved;
				if (extra > length)
					extra = length;
				memcpy(&line_buffer[saved], current, extra);
				current += extra; block -= extra;
				length = saved + extra;
				line = line_buffer;
				offset = line_offset;
				start = end; end = current == next;
				saved = 0;
			} else {
/* Nothing in the line buffer: just process what we've got now */
				line = current;
				offset = file_offset - block;
				start = end; end = 1;
				current = next; block -= length;
			}
		} else {
/* No more LFs in the file buffer */
			if (saved || block <= LINE_BUFFER_SIZE) {
/* Have this line's beginning in the line buffer: combine them */
/* Not enough data to process right now: buffer it */
				extra = LINE_BUFFER_SIZE - saved;
				if (extra > block)
					extra = block;
				if (!saved)
					line_offset = file_offset - block;
				memcpy(&line_buffer[saved], current, extra);
				current += extra; block -= extra;
				saved += extra;
				length = saved;
				line = line_buffer;
				offset = line_offset;
			} else {
/* Nothing in the line buffer and we've got enough data: just process it */
				length = block - 1;
				line = current;
				offset = file_offset - block;
				current += length;
				block = 1;
			}
			if (!block) {
/* We've emptied the file buffer: fetch some more data */
				current = file_buffer;
				block = read(fd, file_buffer, FILE_BUFFER_SIZE);
				if (block < 0)
					break;
				file_offset += block;
				if (block > 0 && saved < LINE_BUFFER_SIZE)
					continue;
				if (!saved) {
/* Nothing in the line buffer, and read(2) returned 0: we're done */
					offset = file_offset;
					done = 1;
					break;
				}
			}
			start = end; end = !block;
			saved = 0;
		}

/*
 * Part 2.
 * The following variables are set when we get here:
 * -- line	the line fragment, not NUL terminated;
 * -- length	its length;
 * -- offset	its offset in the file;
 * -- start	whether it's at the start of the line;
 * -- end	whether it's at the end of the line
 * (all four combinations of "start" and "end" are possible).
 */

/* Check for a new message if we've just seen a blank line */
		if (blank && start)
		if (line[0] == 'F' && length >= 5 &&
		    line[1] == 'r' && line[2] == 'o' && line[3] == 'm' &&
		    line[4] == ' ') {
/* Process the previous one first, if exists */
			if (offset > inc_ofs) {
/* If we aren't at the very beginning, there must have been a message */
				if (!msg.data_offset)
					break;
				msg.raw_size = offset - msg.raw_offset;
				msg.data_size = offset - body - msg.data_offset;
				log_percentage(offset, stat.st_size);
				if (message_process(&msg))
					break;
			}
			msg.tm.tm_year = 0;
			if (line[length - 1] == '\n') {
				const char *p = memchr(line + 5, ' ', length - 5);
				if (p) {
					p = strptime(p, " %a %b %d %T %Y", &msg.tm);
					if (!p || *p != '\n')
						msg.tm.tm_year = 0;
				}
			}
/* Now prepare for parsing the new one */
			msg.raw_offset = offset;
			msg.data_offset = 0;
			msg.have_msgid = 0;
			msg.have_irt = 0;
			msg.from = NULL;
			msg.subject = NULL;
			premime.ptr = premime.start;
			mime.dst.ptr = mime.dst.start;
			header = 1; body = 0;
			continue;
		}

/* Memorize file offset of the message data (the line next to "From ") */
		if (header && start && !msg.data_offset) {
			msg.data_offset = offset;
			msg.data_size = 0;
		}

/* If we see LF at start of line, then this is a blank line :-) */
		blank = start && line[0] == '\n';

		if (!header) {
/* If we're no longer in message headers and we see more data, then it's
 * the body. */
			if (msg.data_offset)
				body = 1;
/* The rest of actions in this loop are for header lines only */
			continue;
		}

/* Buffer the headers and the blank line for MIME decoding */
		buffer_append(&premime, line, length);

/* Blank line ends message headers */
		if (!blank)
			continue;
		header = 0;

/* Now decode MIME */
		premime.ptr = premime.start;
		while (premime.ptr < premime.end && *premime.ptr != '\n') {
			char *p = premime.ptr;
			size_t l = premime.end - p, m = 0;
			switch (*p) {
			case 'M':
			case 'm':
				m = 1;
				/* FALLTHRU */
			case 'I':
			case 'i':
				if (eq(p, l, "Message-ID:", 11) ||
				    eq(p, l, "In-Reply-To:", 12)) {
					const char *q;
					p = mime_decode_header(&mime);
					while (*p && *p != '<')
						p++;
					if (!*p)
						continue;
					q = ++p;
					while (*q && *q != '>')
						q++;
					if (!*q || q - p < 4)
						continue;
					if (m) {
						message_header_hash(p, q, &msg.msgid_hash);
						msg.have_msgid = 1;
					} else {
						message_header_hash(p, q, &msg.irt_hash);
						msg.have_irt = 1;
					}
					continue;
				}
				break;
			case 'R':
			case 'r':
				if (!msg.have_irt &&
				    eq(p, l, "References:", 11)) {
					char *q;
					p = mime_decode_header(&mime);
					while (*p && *p != '<')
						p++;
					if (!*p)
						continue;
					/* seek last reference */
					do {
						q = ++p;
						while (*p && *p != '<')
							p++;
					} while (*p);
					p = q;
					while (*q && *q != '>')
						q++;
					if (!*q || q - p < 4)
						continue;
					message_header_hash(p, q, &msg.irt_hash);
					msg.have_irt = 1;
					continue;
				}
				break;
			case 'F':
			case 'f':
				if (eq(p, l, "From:", 5)) {
					p = mime_decode_header(&mime) + 5;
					while (*p == ' ' || *p == '\t')
						p++;
					msg.from = p;
					continue;
				}
				break;
			case 'S':
			case 's':
				if (eq(p, l, "Subject:", 8)) {
					p = mime_decode_header(&mime) + 8;
					while (*p == ' ' || *p == '\t')
						p++;
					msg.subject = p;
					while ((p = strchr(p, '['))) {
						const char *q;
						if (strncasecmp(++p, list, strlen(list)))
							continue;
						q = p + strlen(list);
						if (*q != ']')
							continue;
						if (*++q == ' ')
							q++;
						memmove(--p, q, strlen(q) + 1);
					}
					continue;
				}
				break;
			}
			mime_skip_header(&mime);
		}
	} while (1);

	if (premime.error)
		done = 0;
	buffer_free(&premime);
	free(file_buffer);

	if (offset != stat.st_size || !msg.data_offset)
		done = 0;

	if (done) {
/* Process the last message */
		msg.raw_size = offset - msg.raw_offset;
		msg.data_size = offset - (blank & body) - msg.data_offset;
		if (message_process(&msg))
			done = 0;
	}

	if (mime.dst.error)
		done = 0;
	mime_free(&mime);

	return !done;
}

int mailbox_parse(const char *mailbox)
{
	const char *p;
	int fd, idx_fd;
	char *idx;
	off_t idx_size;
	size_t msgs_size;
	int error;
	idx_msgnum_t old_msg_num;
	off_t inc_ofs = 0;

	if ((p = strrchr(mailbox, '/')))
		list = p + 1;
	else
		list = mailbox;

	fd = open(mailbox, O_RDONLY);
	if (fd < 0)
		return 1;

	error = lock_fd(fd, 1);

	idx_fd = -1;
	idx = concat(mailbox, INDEX_FILENAME_SUFFIX, NULL);
	if (!idx) {
		close(fd);
		return 1;
	}

	/* try to read index file and calculate offset
	 * of the next unparsed message in mbox (inc_ofs) */
	if (!error && (idx_fd = open(idx, O_RDWR)) >= 0) {
		error = lock_fd(idx_fd, 1);
		if (!error && idx_check_header(idx_fd, &inc_ofs)) {
			logtty("Incompatible index (needs rebuild)\n");
			error = 1;
		}

		if (!error) {
			struct stat st;

			/* if mbox is unmodified, exit w/o error */
			if (!fstat(fd, &st) && inc_ofs == st.st_size) {
				logtty("mbox is unmodified (%llu)\n", (unsigned long long)inc_ofs);
				unlock_fd(idx_fd);
				close(idx_fd);
				unlock_fd(fd);
				close(fd);
				free(idx);
				return 0;
			}

			logtty("Resuming index file\n");
			inc_ofs = begin_inc_idx(idx_fd, fd);
			error = inc_ofs < 0;
		}
		error |= unlock_fd(idx_fd);
	}

	/* otherwise create new index */
	if (!error && idx_fd < 0)
		idx_fd = open(idx, O_CREAT | O_WRONLY, 0644);
	free(idx);

	error |= idx_fd < 0;

	if (inc_ofs <= 0) {
		inc_ofs = 0;
		msg_num = 0;
		msg_alloc = 0;
		msgs = NULL;
	}
	old_msg_num = msg_num;
	if (!error)
		error = lseek(fd, inc_ofs, SEEK_SET) != inc_ofs;

	/* load messages into idx_message msgs[] */
	if (!error) {
		logtty("Parsing mailbox from %llu...\n", (unsigned long long)inc_ofs);
		error = mailbox_parse_fd(fd);
		inc_ofs = lseek(fd, 0, SEEK_CUR);
		error |= unlock_fd(fd);
	}

	error |= close(fd);

	/* update index map and rebuild thread links */
	if (!error) {
		logtty("Linking threads...\n");
		error = msgs_final(old_msg_num) < 0;
	}

	/* index file is always fully rewritten */
	if (!error) {
		logtty("Processing finished, writing index...\n");
		error = lock_fd(idx_fd, 0);
	}

	if (!error)
		error = lseek(idx_fd, 0, SEEK_SET) != 0;

	if (!error) {
		logtty("Writing header...\n");
		error = idx_write_header(idx_fd, inc_ofs);
	}

	/* write messages-per-day array */
	if (!error) {
		logtty("Writing messages index...\n");
		error = write_loop(idx_fd, num_by_aday, sizeof(num_by_aday)) != sizeof(num_by_aday);
	}

	/* write messages metadata */
	if (!error) {
		logtty("Writing messages metadata...\n");
		msgs_size = msg_num * sizeof(struct idx_message);
		error = write_loop(idx_fd, msgs, msgs_size) != msgs_size;
	}

	free(msgs);

	if (!error) {
		idx_size = lseek(idx_fd, 0, SEEK_CUR);
		error = idx_size == -1;
	}

	if (!error) {
		error = ftruncate(idx_fd, idx_size) != 0;
		logtty("Done\n");
	}

	if (idx_fd >= 0) {
		error |= unlock_fd(idx_fd);
		error |= close(idx_fd);
	}

	return error;
}
