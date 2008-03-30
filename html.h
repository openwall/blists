#ifndef _BLISTS_HTML_H
#define _BLISTS_HTML_H

/*
 * Outputs an HTML error message to stdout.  If msg is NULL, then the
 * error is assumed to be internal to the server and a fixed message is
 * output.
 */
extern int html_error(char *msg);

/*
 * Loads the specified message and outputs it to stdout with conversion
 * to HTML and hyperlinks added.
 */
extern int html_message(char *list,
	unsigned int y, unsigned int m, unsigned int d, unsigned int n);

#endif
