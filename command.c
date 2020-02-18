#include "yaxwm.h"

/* function prototypes */
void cmdfocus(const Arg *arg);
void cmdmove(const Arg *arg);
void cmdrun(const Arg *arg);
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
char *parseintopts(char **argv, char **opts, int *iarg);
int adjbdorgap(int i, char *opt, int changing, int other);

/* fifo parser keywords and functions,
 * functions must have a prototype like:  void func(const Arg *);
 * remaining arguments will be tokenized as an array of char *'s in arg->v
 * where the function is expected to know how to handle and use them */
static Keyword keywords[] = {
	{ "exec",   cmdrun },   /* none */
	{ "move",   cmdmove },  /* listopts */
	{ "focus",  cmdfocus }, /* listopts */
	{ "ws",     cmdws },    /* wsopts */
	{ "set",    cmdset },   /* settings[] below */

	/* these just ignore args and operate on the current
	 * window so we can use existing functions */
	{ "kill",   killclient },
	{ "swap",   swapclient },
	{ "float",  togglefloat },
};

static const char *minopts[]  = { "relative", NULL };
static const char *stdopts[]  = { "reset",   "relative", NULL };
static const char *lytopts[]  = { "tile",    "monocle", "none", NULL };
static const char *wsopts[]   = { "view",    "send",    "follow",  NULL };
static const char *colopts[]  = { "reset",   "focus",   "unfocus", NULL };
static const char *listopts[] = { "next",    "prev",    "first",  "last", NULL };

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

void cmdfocus(const Arg *arg)
{
	int i = 0, iarg;
	char *opt;
	Client *c = NULL, *tmp = NULL;

	if (!selws->sel || selws->sel->fullscreen)
		return;
	if (!(opt = parseintopts((char **)arg->v, (char **)listopts, &iarg)) && iarg == UNSET)
		return;
	if ((opt && !strcmp(opt, "first")) || iarg == 0) {
		c = selws->clients;
	} else if (opt && !strcmp(opt, "last")) {
		FIND_STAIL(c, selws->stack);
	} else if (opt && (!strcmp(opt, "next") || (!strcmp(opt, "relative") && iarg > 0))) {
		FIND_SNEXT(c, selws->sel, selws->stack);
		if (!strcmp(opt, "relative")) { /* relative forwards */
			iarg--;
			while (iarg > 0 && (tmp = c)) {
				FIND_SNEXT(c, tmp, selws->stack);
				iarg--;
			}
		}
	} else if (opt && (!strcmp(opt, "prev") || (!strcmp(opt, "relative") && iarg < 0))) {
		FIND_SPREV(c, selws->sel, selws->stack);
		if (!strcmp(opt, "relative")) { /* relative backwards */
			iarg++;
			while (iarg < 0 && (tmp = c)) {
				FIND_SPREV(c, tmp, selws->stack);
				iarg++;
			}
		}
	} else FOR_EACH(c, selws->clients) { /* absolute, try to find client at index arg->i */
		if (i == iarg)
			break;
		i++;
	}

	if (c) {
		focus(c);
		restack(c->ws);
	}
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

void cmdmove(const Arg *arg)
{
	Arg a;
	char *opt;
	int i = 0, iarg;

	if (!selws->sel || selws->sel->fullscreen || selws->sel->floating)
		return;
	if (!(opt = parseintopts((char **)arg->v, (char **)listopts, &iarg)) && iarg == UNSET)
		return;
	if ((opt && !strcmp(opt, "first")) || iarg == 0) {
		detach(selws->sel, 1);
	} else if (opt && !strcmp(opt, "last")) {
		detach(selws->sel, 0);
		attach(selws->sel, 0);
	} else if (opt && (!strcmp(opt, "next") || (!strcmp(opt, "relative") && iarg > 0))) {
		a.i = +1;
		movestack(&a);
		if (!strcmp(opt, "relative")) /* relative forwards */
			for (i = iarg - 1; i > 0; i--)
				movestack(&a);
	} else if (opt && (!strcmp(opt, "prev") || (!strcmp(opt, "relative") && iarg < 0))) {
		a.i = -1;
		movestack(&a);
		if (!strcmp(opt, "relative")) /* relative backwards */
			for (i = iarg + 1; i < 0; i++)
				movestack(&a);
	} else if (iarg > 0) {
		a.i = +1;
		detach(selws->sel, 1); /* attach to head to begin */
		for (i = iarg; i > 0; i--)
			movestack(&a);
	}
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

void cmdrun(const Arg *arg)
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
	opt = parseintopts((char **)arg->v, (char **)wsopts, &a.i);
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

char *parseintopts(char **argv, char **opts, int *iarg)
{
	int n;
	char *opt = NULL;

	*iarg = UNSET;
	while (*argv) {
		if ((n = strtol(*argv, NULL, 0)) || **argv == '0')
			*iarg = n;
		else for (; !opt && opts && *opts; opts++)
			if (!strcmp(*opts, *argv))
				opt = *argv;
		argv++;
	}
	return opt;
}

