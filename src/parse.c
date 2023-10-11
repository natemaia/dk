/* dk window manager
 *
 * see license file for copyright and license details
 * vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <regex.h>
#include <err.h>
#include <locale.h>

#include <xcb/randr.h>
#include <xcb/xcb_keysyms.h>

#include "dk.h"
#include "parse.h"
#include "strl.h"
#include "util.h"
#include "cmd.h"
#include "layout.h"

int parsebool(char *arg)
{
	int i;
	char *end;

	if (arg && (((i = !strcmp("true", arg)) || !strcmp("false", arg)) ||
				(((i = strtoul(arg, &end, 0)) > 0 || !strcmp("0", arg)) &&
				 *end == '\0')))
		return (i ? 1 : 0);
	return -1;
}

Client *parseclient(char *arg, int *ebadwin)
{
	char *end;
	unsigned int i;
	Client *c = NULL;

	if (arg && arg[0] && (arg[0] == '#' || (arg[0] == '0' && arg[1] == 'x')) &&
		(i = strtoul(*arg == '#' ? arg + 1 : arg, &end, 16)) > 0 &&
		*end == '\0')
		*ebadwin = (c = wintoclient(i)) ? 0 : -1;
	return c;
}

static char *parsetoken(char **src)
{
	/* returns a pointer to a null terminated char * inside src
	 * *unsafe* modifies src replace separator with null terminator
	 * handles both strong and weak quoted strings
	 */
	size_t n = 0;
	int qoute, strongquote = 0;
	char *s, *t, *head, *tail;

	if (!(*src) || !(**src))
		return NULL;
	while (**src && (**src == ' ' || **src == '\t' || **src == '='))
		(*src)++;

	if ((qoute = **src == '"' || (strongquote = **src == '\''))) {
		head = *src + 1;
		if (!(tail = strchr(head, strongquote ? '\'' : '"')))
			return 0;
		if (!strongquote)
			while (*(tail - 1) == '\\')
				tail = strchr(tail + 1, '"');
	} else {
		head = *src;
		tail = strpbrk(*src, " =\n\t");
	}

	s = t = head;
	while (tail ? s < tail : *s) {
		if (qoute && !strongquote && *s == '\\' && *(s + 1) == '"')
			s++;
		else
			n++, *t++ = *s++;
	}
	*t = '\0';
	*src = tail ? ++tail : '\0';
	return head;
}

void parsecmd(char *buf)
{
	char **argv, **save, *tok;
	int n = 0, match = 0, max = 32;
	status_usingcmdresp = 0;

	save = argv = ecalloc(max, sizeof(char *));
	while ((tok = parsetoken(&buf))) {
		if (n + 1 >= max)
			save = argv = erealloc(argv, (max *= 2) * sizeof(char *));
		argv[n++] = tok;
	}
	argv[n] = NULL;

	if (n) {
		int j = n;
		unsigned int i;
		while (j > 0 && *argv) {
			for (i = 0, match = 0; keywords[i].str; i++) {
				if ((match = !strcmp(keywords[i].str, *argv))) {
					cmdc = selws->sel;
					if ((n = keywords[i].func(argv + 1)) == -1)
						goto end;
					argv += ++n, j -= n;
					break;
				}
			}
			if (!match && j-- <= 0)
				break;
		}
	}
	if (!match && *argv)
		respond(cmdresp, "!invalid or unknown command: %s", *argv);
end:
	if (cmdresp && !status_usingcmdresp) {
		fflush(cmdresp);
		fclose(cmdresp);
	}
	free(save);
}

int parsecolour(char *arg, unsigned int *result)
{
	char *end;
	unsigned int argb, len, orig = *result;

	if ((len = strlen(arg)) < 6 || len > 10)
		return -1;
	len -= arg[0] == '#' ? 1 : (arg[0] == '0' && arg[1] == 'x') ? 2 : 0;
	if ((argb = strtoul(arg[0] == '#' ? arg + 1 : arg, &end, 16)) <=
			0xffffffff &&
		*end == '\0') {
		unsigned short a, r, g, b;
		if (len == 6) {
			*result = (argb | 0xff000000);
		} else if ((a = ((argb & 0xff000000) >> 24)) && a != 0xff) {
			r = ((argb & 0x00ff0000 >> 16) * a) / 255;
			g = ((argb & 0x0000ff00 >> 8) * a) / 255;
			b = ((argb & 0x000000ff) * a) / 255;
			*result = (a << 24 | r << 16 | g << 8 | b);
		} else {
			*result = argb;
		}
		needsrefresh = needsrefresh || orig != *result;
		return 1;
	}
	return -1;
}

float parsefloat(char *arg, int *rel)
{
	float f;
	char *end;

	if (arg && (f = strtof(arg, &end)) && *end == '\0' && f != NAN) {
		if (rel)
			*rel = arg[0] == '-' || arg[0] == '+';
		return f;
	}
	return NAN;
}

int parseint(char *arg, int *rel, int allowzero)
{
	int i;
	char *end;

	if (arg &&
		((i = strtol(arg, &end, 0)) || (allowzero && !strcmp("0", arg))) &&
		*end == '\0') {
		if (rel)
			*rel = arg[0] == '-' || arg[0] == '+';
		return i;
	}
	return INT_MIN;
}

int parseintclamp(char *arg, int *rel, int min, int max)
{
	int i;

	if ((i = parseint(arg, rel, min <= 0 && max >= 0)) != INT_MIN && i >= min &&
		i <= max)
		return i;
	return INT_MIN;
}

int parseopt(char *arg, const char **optarr, int len_optarr)
{
	if (arg)
		for (int i = 0; i < len_optarr; optarr++, i++)
			if (!strcmp(*optarr, arg))
				return i;
	return -1;
}

int parsegeom(char *arg, char type, int *i, int *rel, int *grav)
{
	int j;

	if (!arg)
		return 0;
	if ((j = parseint(arg, rel, type == 'x' || type == 'y' ? 1 : 0)) !=
		INT_MIN) {
		*i = j;
	} else if (grav) {
		if (!strcmp("center", arg))
			*grav = GRAV_CENTER;
		else
			switch (type) {
			case 'x':
				if (!strcmp("left", arg))
					*grav = GRAV_LEFT;
				else if (!strcmp("right", arg))
					*grav = GRAV_RIGHT;
				else
					return 0;
				break;
			case 'y':
				if (!strcmp("top", arg))
					*grav = GRAV_TOP;
				else if (!strcmp("bottom", arg))
					*grav = GRAV_BOTTOM;
				else
					return 0;
				break;
			case 'w': /* FALLTHROUGH */
			case 'h': return 0;
			}
	}
	return 1;
}

Workspace *parsewsormon(char *arg, int mon)
{
	int i, n = 0;
	Monitor *m;
	Workspace *cws = selws, *ws;

	if (!arg)
		return NULL;
	if (mon) {
		for (m = nextmon(monitors); m; m = nextmon(m->next))
			if (!strcmp(m->name, arg))
				return m->ws;
	} else {
		FOR_EACH (ws, workspaces)
			if (!strcmp(ws->name, arg))
				return ws;
	}
	if (mon)
		for (m = nextmon(monitors); m; m = nextmon(m->next), n++)
			;
	if ((i = parseintclamp(arg, NULL, 1,
						   mon ? n : globalcfg[GLB_WS_NUM].val)) == INT_MIN ||
		i <= 0)
		return NULL;
	return mon ? ((m = nextmon(itomon(i - 1))) ? m->ws : cws) : itows(i - 1);
}
