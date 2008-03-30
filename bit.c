#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "html.h"

int main(void)
{
	char *list, *p, c;
	unsigned int y, m, d, n;

	p = getenv("SERVER_PROTOCOL");
	if (!p || strcmp(p, "INCLUDED")) return html_error(NULL);

	list = getenv("QUERY_STRING_UNESCAPED");
	if (!list) return html_error(NULL);

	for (p = list; *p; p++) {
		if (*p >= 'a' && *p <= 'z') continue;
		if (p != list && *p == '-') continue;
		if (*p == '/') break;
		return 1;
	}
	*p++ = '\0';

	c = '\0';
	if (sscanf(p, "%u/%u/%u/%u%c", &y, &m, &d, &n, &c) >= 4 && !c)
		return html_message(list, y, m, d, n);

	if (sscanf(p, "%u/%u/%c", &y, &m, &c) >= 2 && !c)
		return html_index(list, y, m);

	return html_error("Invalid request syntax");
}
