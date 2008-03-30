#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "html.h"

int main(void)
{
	char *list, *p, nul, slash;
	unsigned int y, m, d, n;

	p = getenv("SERVER_PROTOCOL");
	if (!p || strcmp(p, "INCLUDED")) return html_error(NULL);

	list = getenv("QUERY_STRING_UNESCAPED");
	if (!list) return html_error(NULL);

	for (p = list; *p; p++) {
		if (p - list > 99) goto bad_syntax;
		if (*p >= 'a' && *p <= 'z') continue;
		if (p != list && *p == '-') continue;
		if (*p == '/') break;
		goto bad_syntax;
	}
	*p++ = '\0';

	nul = '\0';
	if (sscanf(p, "%u/%u/%u/%u%c", &y, &m, &d, &n, &nul) >= 4 && !nul)
		return html_message(list, y, m, d, n);

	if (sscanf(p, "%u/%u%c%c", &y, &m, &slash, &nul) >= 3 &&
	    slash == '/' && !nul)
		return html_month_index(list, y, m);

	if (sscanf(p, "%u%c%c", &y, &slash, &nul) >= 2 && y &&
	    slash == '/' && !nul)
		return html_year_index(list, y);

	if (!p[0])
		return html_year_index(list, 0);

bad_syntax:
	return html_error("Invalid request syntax");
}
