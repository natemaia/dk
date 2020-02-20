#include "yaxwm.h"

/* function prototypes */
void cmdfocus(const Arg *arg);
void cmdmove(const Arg *arg);
void cmdmouse(const Arg *arg);
void cmdexec(const Arg *arg);
void cmdset(const Arg *arg);
void cmdws(const Arg *arg);
void cmdborder(const Arg *arg, char *opt);
void cmdcolour(const Arg *arg, char *opt);
void cmdgappx(const Arg *arg, char *opt);
void cmdlayout(const Arg *arg, char *opt);
void cmdnmaster(const Arg *arg, char *opt);
void cmdnstack(const Arg *arg, char *opt);
void cmdsplit(const Arg *arg, char *opt);
void cmdparse(char *buf);

/* used specifically by other functions */
int adjbdorgap(int i, char *opt, int changing, int other);
void adjmvfocus(const Arg *arg, void (*fn)(const Arg *));
char *parseopts(char **argv, char **opts, int *argi);
void mvresizemouse(int mv);

/* fifo parser keywords and functions,
 * functions must have a prototype like:  void func(const Arg *);
 * remaining arguments will be tokenized as an array of char *'s in arg->v
 * where the function is expected to know how to handle and use them */
static Keyword keywords[] = {
	{ "ws",      cmdws },      /* wsopts */
	{ "set",     cmdset },     /* settings[] below */
	{ "exec",    cmdexec },    /* none */
	{ "move",    cmdmove },    /* listopts */
	{ "focus",   cmdfocus },   /* listopts */
	{ "mouse",   cmdmouse },   /* mouseopts */

	/* these just ignore args and operate on the current
	 * window so we can use existing functions */
	{ "kill",   killclient },
	{ "swap",   swapclient },
	{ "float",  togglefloat },
};

static const char *minopts[]   = { "relative", NULL };
static const char *listopts[]  = { "next", "prev", NULL };
static const char *mouseopts[] = { "move", "resize", NULL };
static const char *stdopts[]   = { "reset", "relative", NULL };
static const char *wsopts[]    = { "view", "send", "follow", NULL };
static const char *lytopts[]   = { "tile", "monocle", "none", NULL };
static const char *colopts[]   = { "reset", "focus", "unfocus", NULL };

/* "set" keyword options, used by cmdset() to parse arguments
 * the final argument should be an array of char *'s and contain any
 * optional string arguments supported by the setting function */
static Setting settings[] = {
	{ "gap",     cmdgappx,   stdopts },
	{ "border",  cmdborder,  stdopts },
	{ "colour",  cmdcolour,  colopts },
	{ "split",   cmdsplit,   minopts },
	{ "stack",   cmdnstack,  minopts },
	{ "master",  cmdnmaster, minopts },
	{ "layout",  cmdlayout,  lytopts },
};


int adjbdorgap(int i, char *opt, int changing, int other)
{
	int r;
	/* if opt is NULL or empty we assume absolute sizes, however we still use
	 * a relative calculation so we can just call the set* function and save code repetition */
	if (opt && *opt) {
		if (!strcmp("reset", opt)) {
			return 0;
		} else if (!strcmp("relative", opt)) {
			if (!(r = i))
				return UNSET;
		} else
			return UNSET;
	} else if (!(r = MAX(MIN(i, (selws->mon->winarea_h / 6) - changing), 0) - other))
		return UNSET;
	return r;
}

void adjmvfocus(const Arg *arg, void (*fn)(const Arg *))
{
	Arg a;
	char *opt;

	if (!selws->sel || selws->sel->fullscreen || selws->sel->floating)
		return;
	else if (!(opt = parseopts((char **)arg->v, (char **)listopts, &a.i)) && a.i == UNSET)
		return;
	else if ((opt && !strcmp(opt, "next")) || (a.i > 0 && a.i != UNSET))
		a.i = opt ? +1 : a.i;
	else if ((opt && !strcmp(opt, "prev")) || a.i < 0)
		a.i = opt ? -1 : a.i;
	while (a.i) {
		fn(&a);
		a.i += a.i > 0 ? -1 : 1;
	}
}

void cmdborder(const Arg *arg, char *opt)
{
	Arg a;
	if ((a.i = adjbdorgap(arg->i, opt, selws->gappx, borders[Width])) != UNSET)
		setborderpx(&a);
}

void cmdcolour(const Arg *arg, char *opt)
{
	Client *c;
	Workspace *ws;
	int f = borders[Focus], u = borders[Unfocus];

	if (!opt || !*opt || arg->i > 0xffffff || arg->i < 0x000000) {
		return;
	} else if (!strcmp("reset", opt)) {
		borders[Focus] = defaultborder[Focus];
		borders[Unfocus] = defaultborder[Unfocus];
	} else if (!strcmp("focus", opt)) {
		borders[Focus] = arg->i;
		xcb_change_window_attributes(con, selws->sel->win,
				XCB_CW_BORDER_PIXEL, &borders[Focus]);
		return;
	} else if (!strcmp("unfocus", opt)) {
		borders[Unfocus] = arg->i;
	} else {
		return;
	}
	if (f != borders[Focus] || u != borders[Unfocus])
		FOR_CLIENTS(c, ws)
			xcb_change_window_attributes(con, c->win, XCB_CW_BORDER_PIXEL,
					&borders[c == c->ws->sel ? Focus : Unfocus]);
}

void cmdexec(const Arg *arg)
{
	DBG("user run command: %s", ((char **)arg->v)[0])
	if (fork())
		return;
	if (con)
		close(xcb_get_file_descriptor(con));
	setsid();
	execvp(((char **)arg->v)[0], (char **)arg->v);
	errx(0, "execvp: %s", ((char **)arg->v)[0]);
}

void cmdfocus(const Arg *arg)
{
	adjmvfocus(arg, changefocus);
}

void cmdgappx(const Arg *arg, char *opt)
{
	Arg a;
	if ((a.i = adjbdorgap(arg->i, opt, borders[Width], selws->gappx)) != UNSET)
		setgappx(&a);
}

void cmdlayout(const Arg *arg, char *opt)
{
	Arg a;
	(void)(arg);
	if (!strcmp(opt, "tile"))
		a.v = tile;
	else if (!strcmp(opt, "monocle"))
		a.v = monocle;
	else
		a.v = NULL;
	setlayout(&a);
}

void cmdmouse(const Arg *arg)
{
	int i;
	char *opt;

	if ((opt = parseopts((char **)arg->v, (char **)mouseopts, &i)))
		mvrmouse(!strcmp(opt, "move"));
}

void cmdmove(const Arg *arg)
{
	adjmvfocus(arg, movestack);
}

void cmdnmaster(const Arg *arg, char *opt)
{
	Arg a;
	a.i = opt ? arg->i : arg->i - (int)selws->nmaster;
	setnmaster(&a);
}

void cmdnstack(const Arg *arg, char *opt)
{
	Arg a;
	a.i = opt ? arg->i : arg->i - (int)selws->nstack;
	setnstack(&a);
}

void cmdsplit(const Arg *arg, char *opt)
{
	Arg a;

	if (opt)
		a.f = arg->f;
	else if (arg->f > 0.9 || arg->f < 0.1 || !(a.f = arg->f - selws->splitratio))
		return;
	setsplit(&a);
}

void cmdset(const Arg *arg)
{
	int n;
	Arg a;
	uint i;
	char *s, *o = NULL;
	char **args, **opts;

	s = ((char **)arg->v)[0];
	args = (char **)arg->v + 1;
	for (i = 0; i < LEN(settings); i++)
		if (!strcmp(settings[i].name, s)) {
			while (*args) {
				if (!strcmp(settings[i].name, "split") && strtof(*args, NULL))
					a.f = strtof(*args, NULL);
				else if (**args == '#' && !strcmp(settings[i].name, "colour"))
					a.i = strtol(++*args, NULL, 16);
				else if ((n = strtol(*args, NULL, 0)) || **args == '0')
					a.i = n;
				else for (opts = (char **)settings[i].opts; !o && opts && *opts; opts++)
					if (!strcmp(*opts, *args))
						o = *args;
				args++;
			}
			settings[i].func(&a, o);
			return;
		}
}

void cmdws(const Arg *arg)
{
	Arg a;
	char *opt;
	void (*fn)(const Arg *) = view;

	if (!selws->sel || selws->sel->fullscreen)
		return;
	opt = parseopts((char **)arg->v, (char **)wsopts, &a.i);
	if (a.i > (int)numws || a.i < 0)
		return;
	if (opt) {
		if (!strcmp(opt, "send"))
			fn = send;
		else if (!strcmp(opt, "follow"))
			fn = follow;
	}
	fn(&a);
}

void parsecommand(char *buf)
{
	Arg arg;
	uint i, n = 0;
	char *k, *args[10], *dbuf, *delim = " \t\n\r";

	dbuf = strdup(buf);
	if (!(k = strtok(dbuf, delim)))
		goto out;
	if ((i = !strcmp("reset", k)) || !strcmp("quit", k)) {
		arg.i = i;
		resetorquit(&arg);
		goto out;
	}
	for (i = 0; i < LEN(keywords); i++)
		if (!strcmp(keywords[i].name, k)) {
			while (n < sizeof(args) && (args[n++] = strtok(NULL, delim)))
				;
			if (*args) {
				arg.v = args;
				keywords[i].func(&arg);
			}
			break;
		}
out:
	free(dbuf);
}

char *parseopts(char **argv, char **opts, int *argi)
{
	int n;
	char *opt = NULL;

	*argi = UNSET;
	while (*argv) {
		if ((n = strtol(*argv, NULL, 0)) || **argv == '0')
			*argi = n;
		else for (; !opt && opts && *opts; opts++)
			if (!strcmp(*opts, *argv))
				opt = *argv;
		argv++;
	}
	return opt;
}
