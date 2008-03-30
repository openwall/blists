#ifndef _BLISTS_HTML_H
#define _BLISTS_HTML_H

#define HTML_HEADER			1
#define HTML_BODY			2
#define HTML_CENSOR			4

/* Header vs. body */
extern int html_flags;

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

/*
 * Outputs the message index for the specified month to stdout.
 */
extern int html_month_index(char *list, unsigned int y, unsigned int m);

/*
 * Outputs the summary with hyperlinks for the specified year to stdout.
 * If the year is specified as 0, the summary for all years is output.
 */
extern int html_year_index(char *list, unsigned int y);

#endif
