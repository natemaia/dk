#include "parse.h"

int parsebool(char *arg)
{
	int i;
	char *end;

	if (((i = !strcmp("true", arg)) || !strcmp("false", arg))
			|| (((i = strtoul(arg, &end, 0)) > 0 || !strcmp("0", arg)) && *end == '\0'))
		return (i ? 1 : 0);
	return -1;
}

Client *parseclient(char *argv, int *ebadwin)
{
	char *end;
	Client *c = NULL;

	if (argv && *argv) {
		unsigned int i;
		if ((argv[0] == '#' || (argv[0] == '0' && argv[1] == 'x'))
				&& (i = strtoul(*argv == '#' ? argv + 1 : argv, &end, 16)) > 0 && *end == '\0')
		{
			if (!(c = wintoclient(i))) {
				fprintf(cmdresp, "!invalid window id: %s\n", argv);
				*ebadwin = -1;
			}
		}
	}
	return c;
}

char *parsetoken(char **src)
{
	size_t n = 0;
	int q, sq = 0;
	char *s, *t, *head, *tail;

	if (!(*src) || !(**src))
		return NULL;
	while (**src && (**src == ' ' || **src == '\t' || **src == '='))
		(*src)++;

	if ((q = **src == '"' || (sq = **src == '\''))) {
		head = *src + 1;
		if (!(tail = strchr(head, sq ? '\'' : '"')))
			return 0;
		if (!sq)
			while (*(tail - 1) == '\\')
				tail = strchr(tail + 1, '"');
	} else {
		head = *src;
		tail = strpbrk(*src, " =\n\t");
	}

	s = t = head;
	while (tail ? s < tail : *s) {
		if (q && !sq && *s == '\\' && *(s + 1) == '"') {
			s++;
		} else {
			n++;
			*t++ = *s++;
		}
	}
	*t = '\0';
	*src = tail ? ++tail : '\0';

	return head;
}

void parsecmd(char *buf)
{
	unsigned int i;
	int n = 0, match = 0, max = 32;
	char **argv, **new, *tok, *key;

	DBG("parsecmd: tokenizing buffer: %s", buf)
	if (!(key = parsetoken(&buf)))
		return;
	for (i = 0; i < LEN(keywords); i++) {
		if ((match = !strcmp(keywords[i].str, key))) {
			argv = ecalloc(max, sizeof(char *));
			while ((tok = parsetoken(&buf))) {
				if (n + 1 >= max) {
					max *= 2;
					if (!(new = realloc(argv, max * sizeof(char *))))
						err(1, "unable to reallocate space");
					argv = new;
				}
				argv[n++] = tok;
			}
			argv[n] = NULL;
#ifdef DEBUG
			DBG("parsecmd: keyword = %s", key)
			for (int j = 0; j < n; j++) {
				DBG("parsecmd: argv[%d] = %s", j, argv[j])
			}
#endif
			if (n) {
				cmdclient = selws->sel;
				((void (*)(char **))keywords[i].func)(argv);
			} else {
				fprintf(cmdresp, "!%s %s\n", key, enoargs);
			}
			free(argv);
			break;
		}
	}
	if (!match) {
		if (!strcmp("exit", key))
			running = 0;
		else if (!strcmp("reload", key))
			execcfg();
		else if (!strcmp("restart", key))
			running = 0, restart = 1;
		else
			fprintf(cmdresp, "!invalid or unknown command: %s\n", key);
	}
	fflush(cmdresp);
	fclose(cmdresp);
}

int parsecolour(char *arg, unsigned int *result)
{
	char *end;
	unsigned int argb, len;

	if ((len = strlen(arg)) >= 6 && len <= 10)
		return -1;
	len -= arg[0] == '#' ? 1 : (arg[0] == '0' && arg[1] == 'x') ? 2 : 0;
	if ((argb = strtoul(arg[0] == '#' ? arg + 1 : arg, &end, 16)) <= 0xffffffff && *end == '\0') {
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
		return 1;
	}
	return -1;
}

float parsefloat(char *arg, int *rel)
{
	float f;
	char *end;

	if ((f = strtof(arg, &end)) && *end == '\0' && f >= -0.95 && f <= 0.95) {
		if (rel)
			*rel = arg[0] == '-' || arg[0] == '+';
		return f;
	}
	return -1.0;
}

int parseint(char *arg, int *rel, int allowzero)
{
	int i;
	char *end;

	if (((i = strtol(arg, &end, 0)) || (allowzero && !strcmp("0", arg))) && *end == '\0') {
		if (rel)
			*rel = arg[0] == '-' || arg[0] == '+';
		return i;
	}
	return INT_MIN;
}

int parseintclamp(char *arg, int *rel, int min, int max)
{
	int i;

	if ((i = parseint(arg, rel, min <= 0 && max >= 0)) != INT_MIN && i >= min && i <= max)
		return i;
	return INT_MIN;
}

int parseopt(char *argv, char **optarr)
{
	char **s = optarr;

	for (int i = 0; s && *s; s++, i++)
		if (!strcmp(*s, argv))
			return i;
	return -1;
}

int parsegeom(char *arg, char type, int *i, int *rel, int *grav)
{
	int j;

	if (!grav && (j = parseint(arg, rel, type == 'x' || type == 'y' ? 1 : 0)) != INT_MIN) {
		*i = j;
	} else if (grav && !strcmp("center", arg)) {
		*grav = GRAV_CENTER;
	} else {
		switch (type) {
		case 'x':
			if (grav && !strcmp("left", arg)) *grav = GRAV_LEFT;
			else if (grav && !strcmp("right", arg)) *grav = GRAV_RIGHT;
			else if ((j = parseint(arg, rel, 1)) != INT_MIN) *i = j;
			else return 0;
			break;
		case 'y':
			if (grav && !strcmp("top", arg)) *grav = GRAV_TOP;
			else if (grav && !strcmp("bottom", arg)) *grav = GRAV_BOTTOM;
			else if ((j = parseint(arg, rel, 1)) != INT_MIN) *i = j;
			else return 0;
			break;
		case 'w': /* FALLTHROUGH */
		case 'h':
			if ((j = parseint(arg, rel, 0)) != INT_MIN) *i = j;
			else return 0;
			break;
		}
	}
	return 1;
}

Workspace *parsewsormon(char *arg, int mon)
{
	int i, n;
	Monitor *m;
	Workspace *cws = selws, *ws;

	if (mon) {
		for (m = nextmon(monitors); m; m = nextmon(m->next))
			if (!strcmp(m->name, arg))
				return m->ws;
	} else {
		FOR_EACH(ws, workspaces)
			if (!strcmp(ws->name, arg))
				return ws;
	}
	if (mon)
		for (n = 0, m = nextmon(monitors); m; m = nextmon(m->next), n++)
			;
	if ((i = parseintclamp(arg, NULL, 1, mon ? n : globalcfg[GLB_NUMWS])) == INT_MIN || i <= 0)
		return NULL;
	return mon ? ((m = nextmon(itomon(i - 1))) ? m->ws : cws) : itows(i - 1);
}
