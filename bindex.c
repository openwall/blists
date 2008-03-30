#include <stdio.h>

#include "mailbox.h"

int main(int argc, char **argv)
{
	if (argc != 2) {
		fputs("Usage: bindex MAILBOX\n", stderr);
		return 1;
	}

	if (mailbox_parse(argv[1])) {
		fprintf(stderr, "Failed to parse the mailbox: %s\n", argv[1]);
		return 1;
	}

	return 0;
}
