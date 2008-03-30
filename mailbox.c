/*
 * Mailbox access.
 */

#define _XOPEN_SOURCE
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include "params.h"
#include "index.h"
#include "misc.h"

static int idx_fd;

static int msg_num;
static int n_by_aday[N_ADAY];

struct message {
	unsigned long raw_offset;	/* Raw, with the "From " line */
	unsigned long raw_size;
	unsigned long data_offset;	/* Just the message itself */
	unsigned long data_size;
	struct tm tm;
};

static int message_process(struct message *msg)
{
	struct idx_message idx_msg;
	unsigned int aday;

	idx_msg.offset = msg->data_offset;
	idx_msg.size = msg->data_size;

	if (msg->tm.tm_year >= (MIN_YEAR - 1900) &&
	    msg->tm.tm_year <= (MAX_YEAR - 1900)) {
		idx_msg.y = msg->tm.tm_year - (MIN_YEAR - 1900);
		idx_msg.m = msg->tm.tm_mon + 1;
		idx_msg.d = msg->tm.tm_mday;
	} else {
		idx_msg.y = 0;
		idx_msg.m = 1;
		idx_msg.d = 1;
	}

	aday = 0;
	if (msg->tm.tm_year) {
		aday = (msg->tm.tm_year - (MIN_YEAR - 1900)) * 366 +
		    msg->tm.tm_mon * 31 +
		    (msg->tm.tm_mday - 1);
		if (aday < 0 || aday >= N_ADAY) aday = 0;
	}

	msg_num++;
	if (!n_by_aday[aday])
		n_by_aday[aday] = msg_num;

	return
		write_loop(idx_fd, (char *)&idx_msg, sizeof(idx_msg))
		    != sizeof(idx_msg);
}

#if 0
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
#endif

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
	unsigned long mailbox_size;		/* Its original size */
	struct message msg;			/* Message being parsed */
	char *file_buffer, *line_buffer;	/* Our internal buffers */
	unsigned long file_offset, line_offset;	/* Their offsets in the file */
	unsigned long offset;			/* A line fragment's offset */
	char *current, *next, *line;		/* Line pointers */
	int block, saved, extra, length;	/* Internal block sizes */
	int done, start, end;			/* Various boolean flags: */
	int blank, header, body;		/* the state information */

	if (fstat(fd, &stat)) return 1;

	if (!S_ISREG(stat.st_mode)) return 1;
	if (stat.st_size > MAX_MAILBOX_BYTES || stat.st_size > ~0UL) return 1;
	mailbox_size = stat.st_size;
	if (!mailbox_size) return 0;

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

#if 0
		if (start)
		switch (line[0]) {
		case 'D':
		case 'd':
			eq(line, length, "Date:", 5);
			break;
		}
#endif
	} while (1);

	free(file_buffer);

	if (done) {
/* Process the last message */
		if (offset != mailbox_size) return 1;
		if (!msg.data_offset) return 1;
		msg.raw_size = offset - msg.raw_offset;
		msg.data_size = offset - (blank & body) - msg.data_offset;
		if (message_process(&msg)) return 1;
	}

	return !done;
}

int mailbox_parse(char *mailbox)
{
	int fd;
	char *idx;
	int error;

	fd = open(mailbox, O_RDONLY);
	if (fd < 0) return 1;

	idx = concat(mailbox, INDEX_FILENAME_SUFFIX, NULL);
	error = !idx;
	if (idx) {
		idx_fd = open(idx, O_CREAT | O_WRONLY | O_TRUNC, 0644);
		if (idx_fd < 0) error = 1;
		free(idx);
	}

	msg_num = 0;
	memset(n_by_aday, 0, sizeof(n_by_aday));

	if (!error)
		error =
		    write_loop(idx_fd, (char *)n_by_aday, sizeof(n_by_aday))
		    != sizeof(n_by_aday);

	if (!error)
		error = lock_fd(fd, 1);
	if (!error) {
		error = mailbox_parse_fd(fd);
		if (unlock_fd(fd) && !error) error = 1;
	}

	if (close(fd) && !error) error = 1;

	if (!error)
		error = lseek(idx_fd, 0, SEEK_SET) != 0;

	if (!error)
		error =
		    write_loop(idx_fd, (char *)n_by_aday, sizeof(n_by_aday))
		    != sizeof(n_by_aday);

	return error;
}
