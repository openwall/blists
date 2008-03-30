/*
 * Mailbox access.
 */

#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include "md5/md5.h"

#include "params.h"
#include "index.h"
#include "misc.h"
#include "mailbox.h"

static idx_msgnum_t num_by_aday[N_ADAY + 1];
static idx_msgnum_t msg_num, msg_alloc;
static struct idx_message *msgs;

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
};

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

static int message_process(struct parsed_message *msg)
{
	struct idx_message *idx_msg;

	idx_msg = msgs_grow();
	if (!idx_msg) return -1;

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

	idx_msg->t.alast = -1;

	return 0;
}

static int msgs_link(void)
{
	idx_msgnum_t i;
	struct idx_message *m, *tl;
	unsigned int aday;
	struct mem_message *pool, **hash, *irt;
	unsigned int hv;

	pool = calloc(msg_num, sizeof(*pool));
	if (!pool) return -1;
	hash = calloc(0x10000, sizeof(*hash));
	if (!hash) {
		free(pool);
		return -1;
	}

	for (i = 0, m = msgs; i < msg_num; i++, m++) {
		if (!(m->flags & IDX_F_HAVE_MSGID)) continue;
		pool[i].msg = m;
		hv = m->msgid_hash[0] | ((unsigned int)m->msgid_hash[1] << 8);
		pool[i].next_hash = hash[hv];
		hash[hv] = &pool[i];
	}

	for (i = 0, m = msgs; i < msg_num; i++, m++) {
		if (!(m->flags & IDX_F_HAVE_IRT)) continue;
		hv = m->irt_hash[0] | ((unsigned int)m->irt_hash[1] << 8);
		irt = hash[hv];
		while (irt) {
			if (!memcmp(m->irt_hash, irt->msg->msgid_hash,
			    sizeof(idx_hash_t)))
				break;
			irt = irt->next_hash;
		}
		if (!irt) continue;

		if (irt->msg->t.alast < 0) {
			aday =
			    ((unsigned int)m->y * 12 +
			    ((unsigned int)m->m - 1)) * 31 +
			    ((unsigned int)m->d - 1);
			irt->msg->t.alast = m->t.alast = i;
			irt->msg->t.ny = m->y;
			irt->msg->t.nm = m->m;
			irt->msg->t.nd = m->d;
			irt->msg->t.nn = i + 2 - num_by_aday[aday];
			aday =
			    ((unsigned int)irt->msg->y * 12 +
			    ((unsigned int)irt->msg->m - 1)) * 31 +
			    ((unsigned int)irt->msg->d - 1);
			m->t.py = irt->msg->y;
			m->t.pm = irt->msg->m;
			m->t.pd = irt->msg->d;
			m->t.pn = irt - pool + 2 - num_by_aday[aday];
		} else {
			tl = &msgs[irt->msg->t.alast];
			aday =
			    ((unsigned int)tl->y * 12 +
			    ((unsigned int)tl->m - 1)) * 31 +
			    ((unsigned int)tl->d - 1);
			m->t.py = tl->y;
			m->t.pm = tl->m;
			m->t.pd = tl->d;
			m->t.pn = irt->msg->t.alast + 2 - num_by_aday[aday];
			aday =
			    ((unsigned int)m->y * 12 +
			    ((unsigned int)m->m - 1)) * 31 +
			    ((unsigned int)m->d - 1);
			tl->t.ny = m->y;
			tl->t.nm = m->m;
			tl->t.nd = m->d;
			tl->t.nn = i + 2 - num_by_aday[aday];
			tl->t.alast = i;
			irt->msg->t.alast = i;
			while (tl->t.pn) {
				aday =
				    ((unsigned int)tl->t.py * 12 +
				    ((unsigned int)tl->t.pm - 1)) * 31 +
				    ((unsigned int)tl->t.pd - 1);
				tl = &msgs[num_by_aday[aday] + tl->t.pn - 2];
				tl->t.alast = i;
			}
		}
	}

	free(hash);
	free(pool);

	return 0;
}

static int cmp_msgs_by_day(const void *p1, const void *p2)
{
	const struct idx_message *m1 = p1, *m2 = p2;

	if (m1->y != m2->y) return (int)m1->y - (int)m2->y;
	if (m1->m != m2->m) return (int)m1->m - (int)m2->m;
	return (int)m1->d - (int)m2->d;
}

static int msgs_final(void)
{
	idx_msgnum_t i;
	struct idx_message *m;
	unsigned int aday, prev_aday;

retry:
	memset(num_by_aday, 0, sizeof(num_by_aday));

	prev_aday = 0;
	for (i = 0, m = msgs; i < msg_num; i++, m++) {
		aday =
		    ((unsigned int)m->y * 12 +
		    ((unsigned int)m->m - 1)) * 31 +
		    ((unsigned int)m->d - 1);

		if (aday < prev_aday) {
			fprintf(stderr, "Warning: date went backwards: "
			    "%u -> %u (%04u/%02u/%02u), sorting... ",
			    prev_aday, aday, MIN_YEAR + m->y, m->m, m->d);
			qsort(msgs, msg_num, sizeof(*msgs), cmp_msgs_by_day);
			fprintf(stderr, "done.\n");
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
#ifdef __GNUC__
__inline__
#endif
static int eq(char *s1, int n1, char *s2, int n2)
{
	if (n1 < n2) return 0;
	if (!memcmp(s1, s2, n2)) return 1;
	return !strncasecmp(s1, s2, n2);
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
	MD5_CTX hash;				/* A Message-ID digest */
	char *file_buffer, *line_buffer;	/* Our internal buffers */
	off_t file_offset, line_offset;		/* Their offsets in the file */
	off_t offset;				/* A line fragment's offset */
	char *current, *next, *line;		/* Line pointers */
	int block, saved, extra, length;	/* Internal block sizes */
	int done, start, end;			/* Various boolean flags: */
	int blank, header, body;		/* the state information */

	if (fstat(fd, &stat)) return 1;

	if (!S_ISREG(stat.st_mode)) return 1;
	if (!stat.st_size) return 0;
	if (stat.st_size > MAX_MAILBOX_BYTES || stat.st_size > ~0UL) return 1;

	memset(&msg, 0, sizeof(msg));

	file_buffer = malloc(FILE_BUFFER_SIZE + LINE_BUFFER_SIZE);
	if (!file_buffer) return 1;
	line_buffer = &file_buffer[FILE_BUFFER_SIZE];

	file_offset = 0; line_offset = 0; offset = 0;	/* Start at 0, with */
	current = file_buffer; block = 0; saved = 0;	/* empty buffers */

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
				if (extra > length) extra = length;
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
				if (extra > block) extra = block;
				if (!saved) line_offset = file_offset - block;
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
				if (block < 0) break;
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
			if (offset) {
/* If we aren't at the very beginning, there must have been a message */
				if (!msg.data_offset) break;
				msg.raw_size = offset - msg.raw_offset;
				msg.data_size = offset - body - msg.data_offset;
				if (message_process(&msg)) break;
			}
			msg.tm.tm_year = 0;
			if (line[length - 1] == '\n') {
				char *p = strchr(line + 5, ' ');

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

/* Blank line ends message headers */
		if (blank) {
			header = 0;
			continue;
		}

		if (start && end)
		switch (line[0]) {
		case 'M':
		case 'm':
		case 'I':
		case 'i':
			if (eq(line, length, "Message-ID:", 11) ||
			    eq(line, length, "In-Reply-To:", 12)) {
				char *p = &line[11], *q, *e = line + length;
				while (p < e && *p != '<') p++;
				if (p >= e) break;
				q = ++p;
				while (q < e && *q != '>') q++;
				if (q >= e || q - p < 4) break;
				MD5_Init(&hash);
				MD5_Update(&hash, p, q - p);
				if (line[0] == 'M' || line[0] == 'm') {
					MD5_Final(msg.msgid_hash, &hash);
					msg.have_msgid = 1;
				} else {
					MD5_Final(msg.irt_hash, &hash);
					msg.have_irt = 1;
				}
			}
			break;
		}
	} while (1);

	free(file_buffer);

	if (done) {
/* Process the last message */
		if (offset != stat.st_size) return 1;
		if (!msg.data_offset) return 1;
		msg.raw_size = offset - msg.raw_offset;
		msg.data_size = offset - (blank & body) - msg.data_offset;
		if (message_process(&msg)) return 1;
	}

	return !done;
}

int mailbox_parse(char *mailbox)
{
	int fd, idx_fd;
	char *idx;
	off_t idx_size;
	size_t msgs_size;
	int error;

	fd = open(mailbox, O_RDONLY);
	if (fd < 0) return 1;

	idx_fd = -1;
	idx = concat(mailbox, INDEX_FILENAME_SUFFIX, NULL);
	if (idx) {
		idx_fd = open(idx, O_CREAT | O_WRONLY, 0644);
		free(idx);
	}
	error = idx_fd < 0;

	msg_num = 0;
	msg_alloc = 0;
	msgs = NULL;

	if (!error)
		error = lock_fd(fd, 1);

	if (!error) {
		error = mailbox_parse_fd(fd);
		if (unlock_fd(fd) && !error) error = 1;
	}

	if (close(fd) && !error) error = 1;

	if (!error)
		error = msgs_final();

	if (!error)
		error = lock_fd(idx_fd, 0);

	if (!error)
		error =
		    write_loop(idx_fd, (char *)num_by_aday, sizeof(num_by_aday))
		    != sizeof(num_by_aday);

	if (!error) {
		msgs_size = msg_num * sizeof(struct idx_message);
		error =
		    write_loop(idx_fd, (char *)msgs, msgs_size) != msgs_size;
	}

	free(msgs);

	if (!error) {
		idx_size = lseek(idx_fd, 0, SEEK_CUR);
		error = idx_size == -1;
	}

	if (!error)
		error = ftruncate(idx_fd, idx_size) != 0;

	if (idx_fd >= 0) {
		if (unlock_fd(idx_fd) && !error) error = 1;
		if (close(idx_fd) && !error) error = 1;
	}

	return error;
}
