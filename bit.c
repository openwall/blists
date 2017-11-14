/*
 * Copyright (c) 2006,2008,2017 Solar Designer <solar at openwall.com>
 * Copyright (c) 2011,2017 ABC <abc at openwall.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted.
 *
 * There's ABSOLUTELY NO WARRANTY, express or implied.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "html.h"

int main(int argc, char **argv)
{
	char *list, *p, nul, slash;
	unsigned int y, m, d, n, a;

	if (argc < 1 || argc > 3)
		return html_error(NULL);

	if (argc >= 2) {
		if (!strcmp(argv[1], "header"))
			html_flags = HTML_HEADER;
		else if (!strcmp(argv[1], "body"))
			html_flags = HTML_BODY;
		else if (!strcmp(argv[1], "header-censored"))
			html_flags = HTML_HEADER | HTML_CENSOR;
		else if (!strcmp(argv[1], "body-censored"))
			html_flags = HTML_BODY | HTML_CENSOR;
		else if (!strcmp(argv[1], "attachment"))
			html_flags = HTML_ATTACHMENT;
		else
			return html_error(NULL);
	}

	list = NULL;
	if (html_flags != HTML_ATTACHMENT) {
		p = getenv("SERVER_PROTOCOL");
		if (!p || strcmp(p, "INCLUDED"))
			return html_error(NULL);
		list = getenv("QUERY_STRING_UNESCAPED");
	} else {
		if (argc > 2)
			list = argv[2];
	}
	if (!list)
		return html_error(NULL);

	for (p = list; *p; p++) {
		if (p - list > 99)
			goto bad_syntax;
		if ((*p >= 'a' && *p <= 'z') ||
		    (*p >= '0' && *p <= '9') ||
		    (p != list && *p == '-'))
			continue;
		if (*p == '/')
			break;
		goto bad_syntax;
	}
	*p++ = '\0';

	nul = '\0';
	if (sscanf(p, "%u/%u/%u/%u/%u%c", &y, &m, &d, &n, &a, &nul) >= 5 && !nul)
		return html_attachment(list, y, m, d, n, a);

	if (sscanf(p, "%u/%u/%u/%u%c", &y, &m, &d, &n, &nul) >= 4 && !nul)
		return html_message(list, y, m, d, n);

	if (sscanf(p, "%u/%u/%u%c%c", &y, &m, &d, &slash, &nul) >= 4 && slash == '/' && !nul)
		return html_day_index(list, y, m, d);

	if (sscanf(p, "%u/%u%c%c", &y, &m, &slash, &nul) >= 3 && slash == '/' && !nul)
		return html_month_index(list, y, m);

	if (sscanf(p, "%u%c%c", &y, &slash, &nul) >= 2 && y && slash == '/' && !nul)
		return html_year_index(list, y);

	if (!p[0])
		return html_year_index(list, 0);

bad_syntax:
	return html_error("Invalid request syntax");
}
