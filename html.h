/*
 * Copyright (c) 2006,2008,2017 Solar Designer <solar at openwall.com>
 * Copyright (c) 2011,2017 ABC <abc at openwall.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted.
 *
 * There's ABSOLUTELY NO WARRANTY, express or implied.
 */

#ifndef _BLISTS_HTML_H
#define _BLISTS_HTML_H

#define HTML_HEADER			1
#define HTML_BODY			2
#define HTML_CENSOR			4
#define HTML_ATTACHMENT			8

/* Header vs. body */
extern int html_flags;

/*
 * Outputs an HTML error message to stdout.  If msg is NULL, then the
 * error is assumed to be internal to the server and a fixed message is
 * output.
 */
extern int html_error_real(const char *file, unsigned int lineno, const char *msg);
#define html_error(x) html_error_real(__FILE__, __LINE__, x)

/*
 * Loads the specified message and outputs it to stdout with conversion
 * to HTML and hyperlinks added.
 */
extern int html_message(const char *list, unsigned int y, unsigned int m, unsigned int d, unsigned int n);

/*
 * Loads the specified attachment and outputs it to stdout verbatim.
 */
extern int html_attachment(const char *list, unsigned int y, unsigned int m, unsigned int d, unsigned int n, unsigned int a);

/*
 * Outputs the message index for the specified day to stdout.
 */
extern int html_day_index(const char *list, unsigned int y, unsigned int m, unsigned int d);

/*
 * Outputs the message index for the specified month to stdout.
 */
extern int html_month_index(const char *list, unsigned int y, unsigned int m);

/*
 * Outputs the summary with hyperlinks for the specified year to stdout.
 * If the year is specified as 0, the summary for all years is output.
 */
extern int html_year_index(const char *list, unsigned int y);

#endif
