/* yet another X window manager
* see license file for copyright and license details
* vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
*/

/* signal.h sigaction */
#define _XOPEN_SOURCE 700

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <regex.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <locale.h>

#include <sys/un.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <xcb/xcb.h>
#include <xcb/randr.h>
#include <xcb/xproto.h>
#include <xcb/xcb_util.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_cursor.h>
#include <xcb/xcb_keysyms.h>

#ifdef DEBUG
#define DBG(fmt, ...) print("yaxwm:%d:"fmt, __LINE__, ##__VA_ARGS__);
static void print(const char *fmt, ...);
#else
#define DBG(fmt, ...)
#endif

#ifndef VERSION
#define VERSION "0.1"
#endif

#define W(x)          ((x)->w + 2 * (x)->bw)
#define H(x)          ((x)->h + 2 * (x)->bw)
#define MAX(a, b)     ((a) > (b) ? (a) : (b))
#define MIN(a, b)     ((a) < (b) ? (a) : (b))
#define LEN(x)        (sizeof(x) / sizeof(x[0]))
#define CLNMOD(mod)   (mod & ~(numlockmask | XCB_MOD_MASK_LOCK))

#define BWMASK        (XCB_CONFIG_WINDOW_BORDER_WIDTH)
#define XYMASK        (XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y)
#define WHMASK        (XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT)
#define BUTTONMASK    (XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE)

#define FOR_EACH(v, list)   for ((v) = (list); (v); (v) = (v)->next)
#define FOR_STACK(v, list)  for ((v) = (list); (v); (v) = (v)->snext)
#define FOR_CLIENTS(c, ws)  FOR_EACH((ws), workspaces) FOR_EACH((c), (ws)->clients)

#define FIND_TAIL(v, list)\
	for ((v) = (list); (v) && (v)->next; (v) = (v)->next)

#define FIND_TILETAIL(v, list)\
	for ((v) = nextt((list)); (v) && nextt((v)->next); (v) = nextt((v)->next))

#define FIND_PREV(v, cur, list)\
	for ((v) = (list); (v) && (v)->next && (v)->next != (cur); (v) = (v)->next)

#define FIND_PREVTILED(v, cur, list)\
	for ((v) = nextt((list)); (v) && nextt((v)->next)\
			&& nextt((v)->next) != (cur); (v) = nextt((v)->next))

#define MOVE(win, x, y)\
	xcb_configure_window(con, (win), XYMASK, (uint []){(x), (y)})

#define PROP_APPEND(win, atom, type, membsize, nmemb, value)\
	xcb_change_property(con, XCB_PROP_MODE_APPEND, (win), (atom),\
			(type), (membsize), (nmemb), (value))

#define PROP_REPLACE(win, atom, type, membsize, nmemb, value)\
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, (win), (atom),\
			(type), (membsize), (nmemb), (value))

typedef unsigned int uint;
typedef unsigned char uchar;
typedef struct Panel Panel;
typedef struct Client Client;
typedef struct Layout Layout;
typedef struct Monitor Monitor;
typedef struct Keyword Keyword;
typedef struct Command Command;
typedef struct Callback Callback;
typedef struct Workspace Workspace;
typedef struct WindowRule WindowRule;
typedef struct WorkspaceRule WorkspaceRule;

enum Borders {
	Width, Smart, Focus, Unfocus
};

enum Gravity {
	Left, Right, Center, Top, Bottom,
};

struct Panel {
	int x, y, w, h;
	int strut_l, strut_r, strut_t, strut_b;
	Panel *next;
	Monitor *mon;
	xcb_window_t win;
};

struct Client {
	int x, y, w, h, bw;
	int old_x, old_y, old_w, old_h, old_bw;
	int max_w, max_h, min_w, min_h;
	int base_w, base_h, increment_w, increment_h;
	float min_aspect, max_aspect;
	int sticky, fixed, floating, fullscreen, urgent, nofocus, oldstate;
	Client *next, *snext;
	Workspace *ws;
	xcb_window_t win;
};

struct Monitor {
	char *name;
	xcb_randr_output_t id;
	int x, y, w, h;
	int wx, wy, ww, wh;
	Monitor *next;
	Workspace *ws;
};

struct Keyword {
	char *name;
	void (*func)(char **);
};

struct Workspace {
	int num;
	char *name;
	uint nmaster, nstack, gappx;
	float split;
	Layout *layout;
	Monitor *mon;
	Workspace *next;
	Client *sel, *stack, *clients, *hidden;
};

struct WindowRule {
	int ws, floating, sticky;
	char *class, *inst, *title, *mon;
	Callback *cb;
	regex_t classreg, instreg, titlereg;
	WindowRule *next;
};

struct WorkspaceRule {
	char *name;
	uint nmaster, nstack, gappx;
	float split;
	Layout *layout;
	WorkspaceRule *next;
};

struct Layout {
	char *name;
	void (*fn)(Workspace *);
};

struct Command {
	char *name;
	void (*fn)(int);
};

struct Callback {
	char *name;
	void (*fn)(Client *);
};

Callback *applywinrule(Client *c);
static int checkerror(int lvl, char *msg, xcb_generic_error_t *e);
static void cmdborder(char **argv);
static void cmdfloat(char **argv);
static void cmdfocus(char **argv);
static void cmdfollow(int num);
static void cmdgappx(char **argv);
static void cmdkill(char **argv);
static void cmdlayout(char **argv);
static void cmdmouse(char **argv);
static void cmdmvresize(char **argv);
static void cmdmvstack(char **argv);
static void cmdnmaster(char **argv);
static void cmdnstack(char **argv);
static void cmdparse(char *buf);
static void cmdrule(char **argv);
static void cmdsend(int num);
static void cmdset(char **argv);
static void cmdsplit(char **argv);
static void cmdstick(char **argv);
static void cmdswap(char **argv);
static void cmdwin(char **argv);
static void cmdwinrule(char **argv);
static void cmdwm(char **argv);
static void cmdws(char **argv);
static void cmdview(int num);
static void confine(Client *c);
static void detach(Client *c, int reattach);
static void *ecalloc(size_t elems, size_t size);
static void eventignore(uint8_t type);
static void eventloop(void);
static void execcfg(void);
static void fixupworkspaces(void);
static void focus(Client *c);
static void freeclient(Client *c, int destroyed);
static void freepanel(Panel *panel, int destroyed);
static void freewm(void);
static void freews(Workspace *ws);
static void grabbuttons(Client *c, int focused);
static void gravitate(Client *c, int vert, int horz, int matchgap);
static void initclient(xcb_window_t win, xcb_window_t trans, xcb_get_geometry_reply_t *g);
static void initpanel(xcb_window_t win, xcb_get_geometry_reply_t *g);
static void initscan(void);
static void initwinrule(char *class, char *inst, char *title, char *mon,
		int ws, int floating, int sticky, Callback *cb);
static int initwinruleregcomp(WindowRule *r, char *class, char *inst, char *title);
static void initwm(void);
static void initworkspaces(void);
static Workspace *initws(int num, WorkspaceRule *r);
static Workspace *itows(int num);
static void layoutws(Workspace *ws);
static void monocle(Workspace *ws);
static void mousemvr(int move);
static void movefocus(int direction);
static void movestack(int direction);
static Client *nextt(Client *c);
static int optparse(char **argv, char **opts, int *argi, float *argf, int hex);
static int querypointer(int *x, int *y);
static void resize(Client *c, int x, int y, int w, int h, int bw);
static void resizehint(Client *c, int x, int y, int w, int h, int bw, int usermotion);
static void restack(Workspace *ws);
static int rulecmp(WindowRule *r, char *title, char *class, char *inst);
static void sendconfigure(Client *c);
static int sendevent(Client *c, const char *ev, long mask);
static int sendwmproto(Client *c, int wmproto);
static void setclientws(Client *c, uint num);
static void setfullscreen(Client *c, int fullscreen);
static void setstackmode(xcb_window_t win, uint mode);
static void setsticky(Client *c, int sticky);
static void seturgency(Client *c, int urg);
static void setwinstate(xcb_window_t win, uint32_t state);
static void showhide(Client *c);
static void sighandle(int);
static void sizehints(Client *c);
static void takefocus(Client *c);
static void tile(Workspace *ws);
static void unfocus(Client *c, int focusroot);
static void ungrabpointer(void);
static void updatenumws(int needed);
static int updaterandr(void);
static void updatestruts(Panel *p, int apply);
static xcb_get_window_attributes_reply_t *winattr(xcb_window_t win);
static xcb_get_geometry_reply_t *wingeom(xcb_window_t win);
static void winhints(Client *c);
static xcb_atom_t winprop(xcb_window_t win, xcb_atom_t prop);
static int wintextprop(xcb_window_t w, xcb_atom_t atom, char *text, size_t size);
static Client *wintoclient(xcb_window_t win);
static Panel *wintopanel(xcb_window_t win);
static Workspace *wintows(xcb_window_t win);
static xcb_window_t wintrans(xcb_window_t win);
static void wintype(Client *c);

/* options available for various commands */
enum { stdreset, stdabsolute };
static char *minopts[] = { "absolute", NULL };
static char *stdopts[] = { [stdreset] = "reset", [stdabsolute] = "absolute", NULL };

/* command keywords and parser functions */
static Keyword keywords[] = {
	{ "set",  cmdset  },
	{ "win",  cmdwin  },
	{ "wm",   cmdwm   },
	{ "ws",   cmdws   },
	{ "rule", cmdrule },
};

/* "set" keyword options, used by cmdset() to parse arguments */
static Keyword setcmds[] = {
	{ "border",  cmdborder  },
	{ "gap",     cmdgappx   },
	{ "layout",  cmdlayout  },
	{ "master",  cmdnmaster },
	{ "mouse",   cmdmouse   },
	{ "split",   cmdsplit   },
	{ "stack",   cmdnstack  },
};

/* "win" keyword options, used by cmdwin() to parse arguments */
static Keyword wincmds[] = {
	{ "float",    cmdfloat    },
	{ "focus",    cmdfocus    },
	{ "kill",     cmdkill     },
	{ "mvstack",  cmdmvstack  },
	{ "mvresize", cmdmvresize },
	{ "stick",    cmdstick    },
	{ "swap",     cmdswap     },
};

/* "rule" keyword options, used by cmdrule() to parse arguments */
static Keyword rulecmds[] = {
	{ "win", cmdwinrule },
	/* { "ws",  cmdwsrule }, */
};

/* cursors used for normal operation, moving, and resizing */
enum Cursors {
	Normal,
	Move,
	Resize
};
static const char *cursors[] = {
	[Move] = "fleur",
	[Normal] = "arrow",
	[Resize] = "sizing"
};

/* supported WM_* atoms */
enum WMAtoms {
	Delete,
	Protocols,
	TakeFocus,
	Utf8Str,
	WMState
};
static const char *wmatoms[] = {
	[Protocols] = "WM_PROTOCOLS",
	[Delete] = "WM_DELETE_WINDOW",
	[WMState] = "WM_STATE",
	[TakeFocus] = "WM_TAKE_FOCUS",
	[Utf8Str] = "UTF8_STRING"
};

/* supported _NET_* atoms */
enum NetAtoms {
	ActiveWindow,
	Check,
	ClientList,
	CurDesktop,
	DesktopNames,
	Dialog,
	Dock,
	Fullscreen,
	Name,
	NumDesktops,
	State,
	Strut,
	StrutPartial,
	Supported,
	WindowType,
	WmDesktop
};
static const char *netatoms[] = {
	[ActiveWindow] = "_NET_ACTIVE_WINDOW",
	[Check] = "_NET_SUPPORTING_WM_CHECK",
	[ClientList] = "_NET_CLIENT_LIST",
	[CurDesktop] = "_NET_CURRENT_DESKTOP",
	[DesktopNames] = "_NET_DESKTOP_NAMES",
	[Dialog] = "_NET_WM_WINDOW_TYPE_DIALOG",
	[Dock] = "_NET_WM_WINDOW_TYPE_DOCK",
	[Fullscreen] = "_NET_WM_STATE_FULLSCREEN",
	[Name] = "_NET_WM_NAME",
	[NumDesktops] = "_NET_NUMBER_OF_DESKTOPS",
	[State] = "_NET_WM_STATE",
	[StrutPartial] = "_NET_WM_STRUT_PARTIAL",
	[Strut] = "_NET_WM_STRUT",
	[Supported] = "_NET_SUPPORTED",
	[WindowType] = "_NET_WM_WINDOW_TYPE",
	[WmDesktop] = "_NET_WM_DESKTOP"
};

#include "stringl.c"
#include "config.h"

extern char **environ;            /* environment variables */

static char *argv0;               /* program name */
static int sockfd;                /* socket file descriptor */
static char *sock;                /* socket path, loaded from YAXWM_SOCK environment variable */
static FILE *cmdresp;             /* file used for writing messages to after command */
static int numws = 0;             /* number of workspaces currently allocated */
static int scr_w, scr_h;          /* root window size */
static uint running = 1;          /* continue handling events */
static int randrbase = -1;        /* randr extension response */
static uint numlockmask = 0;      /* numlock modifier bit mask */
static int dborder[LEN(borders)]; /* default border values used for resetting */

static Panel *panels;         /* panel list head */
static Monitor *primary;      /* primary monitor */
static Monitor *monitors;     /* monitor list head */
static Workspace *selws;      /* active workspace */
static Workspace *lastws;     /* last active workspace */
static Workspace *workspaces; /* workspace list head */
static WindowRule *winrules;  /* window rule list head */

static xcb_screen_t *scr;                 /* the X screen */
static xcb_connection_t *con;             /* xcb connection to the X server */
static xcb_window_t root, wmcheck;        /* root window and _NET_SUPPORTING_WM_CHECK window */
static xcb_key_symbols_t *keysyms;        /* current keymap symbols */
static xcb_cursor_t cursor[LEN(cursors)]; /* cursors for moving, resizing, and normal */
static xcb_atom_t wmatom[LEN(wmatoms)];   /* _WM atoms */
static xcb_atom_t netatom[LEN(netatoms)]; /* _NET atoms */

int main(int argc, char *argv[])
{
	argv0 = argv[0];
	xcb_void_cookie_t ck;
	struct sigaction sa;
	int sigs[] = { SIGTERM, SIGINT, SIGHUP, SIGCHLD };
	uint mask = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT;

	if (argc > 1) {
		fprintf(stderr, !strcmp(argv[1], "-v") ? "%s "VERSION"\n" : "usage: %s [-v]\n", argv0);
		exit(1);
	}
	if (!setlocale(LC_CTYPE, ""))
		err(1, "no locale support");
	if (xcb_connection_has_error((con = xcb_connect(NULL, NULL))))
		err(1, "error connecting to X");
	atexit(freewm);

	if (!(scr = xcb_setup_roots_iterator(xcb_get_setup(con)).data))
		errx(1, "error getting default screen from X connection");
	root = scr->root;
	scr_w = scr->width_in_pixels;
	scr_h = scr->height_in_pixels;

	ck = xcb_change_window_attributes_checked(con, root, XCB_CW_EVENT_MASK, &mask);
	checkerror(1, "is another window manager already running?", xcb_request_check(con, ck));

	sa.sa_handler = sighandle;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	for (uint i = 0; i < LEN(sigs); i++)
		if (sigaction(sigs[i], &sa, NULL) < 0)
			err(1, "unable to setup handler for signal: %d", sigs[i]);

	initwm();
	initscan();
	eventloop();

	return 0;
}

int adjbdorgap(int i, int opt, int changing, int other)
{
	int r;

	if (opt != -1) {
		if (opt == stdreset) {
			return 0;
		} else if (opt == stdabsolute) {
			if (!(r = MAX(MIN(i, (selws->mon->wh / 6) - other), 0) - changing))
				return INT_MAX;
		} else
			return INT_MAX;
	} else if (!(r = i))
		return INT_MAX;
	return r;
}

void adjmstack(int i, int opt, int master)
{
	uint n = INT_MAX;

	if (i == INT_MAX)
		return;
	if (opt != -1)
		i -= master ? (int)selws->nmaster : (int)selws->nstack;
	if (master && (n = MAX(selws->nmaster + i, 0)) != selws->nmaster)
		selws->nmaster = n;
	else if (!master && (n = MAX(selws->nstack + i, 0)) != selws->nstack)
		selws->nstack = n;
	if (n != INT_MAX)
		layoutws(selws);
}

void adjmvfocus(char **argv, void (*fn)(int))
{
	int i, opt;
	enum { next, prev };
	char *opts[] = { "next", "prev", NULL };

	opt = optparse(argv, opts, &i, NULL, 0);
	if (opt < 0 && i == INT_MAX)
		return;
	i = opt == -1 ? i : opt == next ? 1 : -1;
	while (i) {
		fn(i);
		i += i > 0 ? -1 : 1;
	}
}

void applypanelstrut(Panel *p)
{
	DBG("applypanelstrut: %s window area before: %d,%d @ %dx%d", p->mon->name, p->mon->wx,
			p->mon->wy, p->mon->ww, p->mon->wh);
	if (p->mon->x + p->strut_l > p->mon->wx)
		p->mon->wx = p->strut_l;
	if (p->mon->y + p->strut_t > p->mon->wy)
		p->mon->wy = p->strut_t;
	if (p->mon->w - (p->strut_r + p->strut_l) < p->mon->ww)
		p->mon->ww = p->mon->w - (p->strut_r + p->strut_l);
	if (p->mon->h - (p->strut_b + p->strut_t) < p->mon->wh)
		p->mon->wh = p->mon->h - (p->strut_b + p->strut_t);
	DBG("applypanelstrut: %s window area after: %d,%d @ %dx%d", p->mon->name, p->mon->wx,
			p->mon->wy, p->mon->ww, p->mon->wh);
}

int applysizehints(Client *c, int *x, int *y, int *w, int *h, int usermotion)
{
	int baseismin;
	Monitor *m = c->ws->mon;

	/* set minimum possible */
	*w = MAX(1, *w);
	*h = MAX(1, *h);
	if (usermotion) { /* don't confine */
		if (*x > scr_w)
			*x = scr_w - W(c);
		if (*y > scr_h)
			*y = scr_h - H(c);
		if (*x + *w + 2 * c->bw < 0)
			*x = 0;
		if (*y + *h + 2 * c->bw < 0)
			*y = 0;
	} else { /* confine to monitor */
		*x = MAX(*x, m->wx);
		*y = MAX(*y, m->wy);
		if (*x + W(c) > m->wx + m->ww)
			*x = m->wx + m->ww - W(c);
		if (*y + H(c) > m->wy + m->wh)
			*y = m->wy + m->wh - H(c);
	}
	if (c->floating || !c->ws->layout->fn) {
		if (!(baseismin = c->base_w == c->min_w && c->base_h == c->min_h)) {
			/* temporarily remove base dimensions */
			*w -= c->base_w;
			*h -= c->base_h;
		}
		if (c->min_aspect > 0 && c->max_aspect > 0) { /* adjust for aspect limits */
			if (c->max_aspect < (float)*w / *h)
				*w = *h * c->max_aspect + 0.5;
			else if (c->min_aspect < (float)*h / *w)
				*h = *w * c->min_aspect + 0.5;
		}
		if (baseismin) { /* increment calculation requires this */
			*w -= c->base_w;
			*h -= c->base_h;
		}
		/* adjust for increment value */
		if (c->increment_w)
			*w -= *w % c->increment_w;
		if (c->increment_h)
			*h -= *h % c->increment_h;
		/* restore base dimensions */
		*w += c->base_w;
		*h += c->base_h;
		*w = MAX(*w, c->min_w);
		*h = MAX(*h, c->min_h);
		if (c->max_w)
			*w = MIN(*w, c->max_w);
		if (c->max_h)
			*h = MIN(*h, c->max_h);
	}
	return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

Callback *applywinrule(Client *c)
{ /* apply user specified rules to client, try using _NET atoms otherwise
   * returns a pointer to the callback function if any otherwise NULL */
	Monitor *m;
	WindowRule *r;
	Callback *cb = NULL;
	int ws, n, num = -1;
	char title[NAME_MAX];
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t pc;
	xcb_icccm_get_wm_class_reply_t prop;

	if (!wintextprop(c->win, netatom[Name], title, sizeof(title))
			&& !wintextprop(c->win, XCB_ATOM_WM_NAME, title, sizeof(title)))
		strlcpy(title, "broken", sizeof(title));
	DBG("applywinrule: window title: %s", title);

	pc = xcb_icccm_get_wm_class(con, c->win);
	c->floating = 0;
	if ((ws = winprop(c->win, netatom[WmDesktop])) < 0)
		ws = selws->num;
	if (xcb_icccm_get_wm_class_reply(con, pc, &prop, &e)) {
		DBG("applywinrule: window class: %s - instance: %s", prop.class_name, prop.instance_name)
		for (r = winrules; r; r = r->next) {
			if (!rulecmp(r, title, prop.class_name, prop.instance_name))
				continue;
			DBG("applywinrule: matched rule: class: %s - inst: %s - title: %s",
					r->class, r->inst, r->title)
			c->floating = r->floating;
			c->sticky = r->sticky;
			cb = r->cb;
			if (r->ws >= 0)
				ws = r->ws;
			else if (r->mon) {
				if (strtol(r->mon, NULL, 0) || !strcmp(r->mon, "0"))
					num = strtol(r->mon, NULL, 0);
				for (n = 0, m = monitors; m; m = m->next, n++) {
					if ((num >= 0 && num == n) || !strcmp(r->mon, m->name)) {
						ws = m->ws->num;
						break;
					}
				}
			}
		}
		xcb_icccm_get_wm_class_reply_wipe(&prop);
	} else {
		checkerror(0, "failed to get window class", e);
	}
	setclientws(c, ws);
	DBG("applywinrule: set client values - workspace: %d, monitor: %s, floating: %d, sticky: %d",
			c->ws->num, c->ws->mon->name, c->floating, c->sticky)
	return cb;
}

void assignworkspaces(void)
{ /* map workspaces to monitors, create more if needed */
	int i, j, n = 0;
	Monitor *m;
	Workspace *ws;

	FOR_EACH(m, monitors)
		n++;
	updatenumws(n);
	j = numws / MAX(1, n);
	ws = workspaces;

	FOR_EACH(m, monitors)
		for (i = 0; ws && i < j; i++, ws = ws->next) {
			ws->mon = m;
			if (!i || ws == selws || ws->mon->ws == ws)
				m->ws = ws;
		}
	if (j * n != numws) {
		for (m = monitors; ws; m = monitors)
			while (ws && m) {
				ws->mon = m;
				ws = ws->next;
				m = m->next;
			}
	}
}

void attach(Client *c, int tohead)
{ /* attach client to it's workspaces client list */
	Client *t = NULL;

	if (!c->ws)
		c->ws = selws;
	if (!tohead)
		FIND_TAIL(t, c->ws->clients);
	if (t) { /* attach to tail */
		c->next = t->next;
		t->next = c;
	} else { /* attach to head */
		c->next = c->ws->clients;
		c->ws->clients = c;
	}
}

void attachpanel(Panel *p)
{
	p->next = panels;
	panels = p;
}

void attachstack(Client *c)
{ /* attach client to it's workspaces focus stack list */
	c->snext = c->ws->stack;
	c->ws->stack = c;
}

void changews(Workspace *ws, int usermotion)
{ /* change the currently active workspace and warp the mouse if needed */
	int diffmon = selws ? selws->mon != ws->mon : 1;

	lastws = selws;
	selws = ws;
	selws->mon->ws = ws;
	PROP_REPLACE(root, netatom[CurDesktop], XCB_ATOM_CARDINAL, 32, 1, &ws->num);
	if (diffmon && !usermotion)
		xcb_warp_pointer(con, root, root, 0, 0, 0, 0,
				ws->mon->x + (ws->mon->w / 2), ws->mon->y + (ws->mon->h / 2));
}

int checkerror(int lvl, char *msg, xcb_generic_error_t *e)
{ /* if e is non-null print a warning with error code and name to stderr and free(3) e
   * when lvl is non-zero call exit(lvl) */
	if (!e)
		return 1;
	warnx("%s -- X11 error: %d: %s", msg, e->error_code, xcb_event_get_error_label(e->error_code));
	free(e);
	if (lvl)
		exit(lvl);
	return 0;
}

void cmdborder(char **argv)
{
	Client *c;
	Workspace *ws;
	int i, n, opt, f = borders[Focus], u = borders[Unfocus];

	enum { colreset, colfocus, colunfocus };
	enum { absolute, width, colour, color, smart };
	char *colopt[] = { "reset",    "focus", "unfocus", NULL };
	char *bdropt[] = { "absolute", "width", "colour", "color", "smart", NULL };

	if ((opt = optparse(argv, bdropt, &i, NULL, 0)) < 0)
		return;
	if (opt == smart) {
		if (i != INT_MAX)
			borders[Smart] = i;
	} else if (opt == colour || opt == color) {
		if ((opt = optparse(argv + 1, colopt, &i, NULL, 1)) < 0)
			return;
		if (opt == colreset) {
			borders[Focus] = dborder[Focus];
			borders[Unfocus] = dborder[Unfocus];
		} else if (i <= 0xffffff && i >= 0) {
			if (opt == colfocus) {
				borders[Focus] = i;
				if (selws->sel)
					xcb_change_window_attributes(con, selws->sel->win,
							XCB_CW_BORDER_PIXEL, &borders[Focus]);
				return;
			} else if (opt == colunfocus) {
				borders[Unfocus] = i;
			}
		}
		if (f != borders[Focus] || u != borders[Unfocus])
			FOR_CLIENTS(c, ws)
				xcb_change_window_attributes(con, c->win, XCB_CW_BORDER_PIXEL,
						&borders[c == c->ws->sel ? Focus : Unfocus]);
	} else if (opt == width) {
		opt = optparse(argv + 1, stdopts, &i, NULL, 0);
		if (opt < 0 && i == INT_MAX)
			return;
		if ((n = adjbdorgap(i, opt, borders[Width], selws->gappx)) != INT_MAX) {
			if (n == 0)
				i = dborder[Width];
			else /* limit border width to 1/6 screen height - gap size and > 0 */
				i = MAX(MIN((int)((selws->mon->wh / 6) - selws->gappx), borders[Width] + n), 1);
			if (i != borders[Width]) {
				/* update border width on clients that have borders matching the current global */
				FOR_CLIENTS(c, ws)
					if (c->bw && c->bw == borders[Width])
						c->bw = i;
				borders[Width] = i;
				layoutws(NULL);
			}
		}
	}
}

void cmdwinrule(char **argv)
{
	uint ui;
	Monitor *m;
	Workspace *ws;
	Callback *cb = NULL;
	int i, floating = 0, workspace = -1, sticky = 0;
	char *mon = NULL, *class = NULL, *inst = NULL, *title = NULL;

	while (*argv) {
		if (!class && !strcmp(*argv, "class")) {
			argv++;
			class = *argv;
		} else if (!inst && !strcmp(*argv, "instance")) {
			argv++;
			inst = *argv;
		} else if (!title && !strcmp(*argv, "title")) {
			argv++;
			title = *argv;
		} else if (class || inst || title) {
			if (!strcmp(*argv, "mon")) {
				argv++;
				if ((i = strtol(*argv, NULL, 0)) || !strcmp(*argv, "0"))
					mon = *argv;
				else FOR_EACH(m, monitors)
					if (!strcmp(m->name, *argv)) {
						mon = m->name;
						break;
					}
			} else if (!strcmp(*argv, "ws")) {
				argv++;
				i = strtol(*argv, NULL, 0);
				if ((i < numws && i > 0) || !strcmp(*argv, "0"))
					workspace = i;
				else FOR_EACH(ws, workspaces)
					if (!strcmp(ws->name, *argv)) {
						workspace = ws->num;
						break;
					}
			} else if (!strcmp(*argv, "callback")) {
				argv++;
				for (ui = 0; ui < LEN(callbacks); ui++)
					if (!strcmp(callbacks[ui].name, *argv)) {
						cb = &callbacks[ui];
						break;
					}
			} else if (!strcmp(*argv, "floating")) {
				floating = 1;
			} else if (!strcmp(*argv, "floating")) {
				sticky = 1;
			}
		}
		argv++;
	}
	if ((class || inst || title) && (mon || workspace != -1 || floating || sticky))
		initwinrule(class, inst, title, mon, workspace, floating, sticky, cb);
}

void cmdfloat(char **argv)
{
	Client *c;

	if (!(c = selws->sel) || c->fullscreen)
		return;
	if ((c->floating = !c->floating || c->fixed)) {
		c->w = c->old_w, c->h = c->old_h;
		c->x = c->old_x ? c->old_x : (c->ws->mon->wx + c->ws->mon->ww - W(c)) / 2;
		c->y = c->old_y ? c->old_y : (c->ws->mon->wy + c->ws->mon->wh - H(c)) / 2;
		resize(c, c->x, c->y, c->w, c->h, c->bw);
	} else {
		c->old_x = c->x, c->old_y = c->y, c->old_w = c->w, c->old_h = c->h;
	}
	layoutws(selws);
	(void)(argv);
}

void cmdfocus(char **argv)
{
	if (!selws->sel || selws->sel->fullscreen)
		return;
	adjmvfocus(argv, movefocus);
}

void cmdfollow(int num)
{ /* follow selected client to a workspace */
	Client *c;
	Workspace *ws;

	if (!selws->sel || num == selws->num || !(ws = itows(num)))
		return;
	if ((c = selws->sel)) {
		unfocus(c, 1);
		setclientws(c, num);
	}
	changews(ws, 0);
	focus(NULL);
	layoutws(NULL);
	restack(selws);
}

void cmdgappx(char **argv)
{
	uint ng;
	int i, n, opt;

	opt = optparse(argv, stdopts, &i, NULL, 0);

	if (opt < 0 && i == INT_MAX)
		return;
	if ((n = adjbdorgap(i, opt, selws->gappx, borders[Width])) == INT_MAX)
		return;
	if (n == 0)
		ng = workspacerules[selws->num].gappx;
	else /* limit gaps to 1/6 screen height - border size */
		ng = MAX(MIN((int)selws->gappx + n, (selws->mon->wh / 6) - borders[Width]), 0);
	if (ng != selws->gappx) {
		selws->gappx = ng;
		layoutws(selws);
	}
}

void cmdkill(char **argv)
{ /* close currently active client and free it */
	if (!selws->sel)
		return;
	if (!sendwmproto(selws->sel, Delete)) {
		xcb_grab_server(con);
		xcb_set_close_down_mode(con, XCB_CLOSE_DOWN_DESTROY_ALL);
		xcb_kill_client(con, selws->sel->win);
		xcb_flush(con);
		xcb_ungrab_server(con);
	}
	xcb_flush(con);
	(void)(argv);
}

void cmdlayout(char **argv)
{
	uint i;

	if (!argv || !*argv)
		return;
	while (*argv) {
		for (i = 0; i < LEN(layouts); i++)
			if (!strcmp(layouts[i].name, *argv)) {
				if (&layouts[i] != selws->layout) {
					selws->layout = &layouts[i];
					layoutws(selws);
				}
				return;
			}
		argv++;
	}
}

void cmdmouse(char **argv)
{
	if (!argv || !*argv)
		return;
	while (*argv) {
		if (!strcmp("mod", *argv)) {
			argv++;
			if (!strcmp("alt", *argv) || !strcmp("mod1", *argv))
				mousemod = XCB_MOD_MASK_1;
			else if (!strcmp("super", *argv) || !strcmp("mod4", *argv))
				mousemod = XCB_MOD_MASK_4;
			else if (!strcmp("ctrl", *argv) || !strcmp("control", *argv))
				mousemod = XCB_MOD_MASK_CONTROL;
		} else if (!strcmp("move", *argv)) {
			argv++;
			if (!strcmp("button1", *argv))
				mousemove = XCB_BUTTON_INDEX_1;
			else if (!strcmp("button2", *argv))
				mousemove = XCB_BUTTON_INDEX_2;
			else if (!strcmp("button3", *argv))
				mousemove = XCB_BUTTON_INDEX_3;
		} else if (!strcmp("resize", *argv)) {
			argv++;
			if (!strcmp("button1", *argv))
				mouseresize = XCB_BUTTON_INDEX_1;
			else if (!strcmp("button2", *argv))
				mouseresize = XCB_BUTTON_INDEX_2;
			else if (!strcmp("button3", *argv))
				mouseresize = XCB_BUTTON_INDEX_3;
		}
		argv++;
	}
	if (selws->sel)
		grabbuttons(selws->sel, 1);
}

void cmdmvresize(char **argv)
{
	Client *c;
	int i = 0, n, arg, absolute, x = 0, y = 0, w = 0, h = 0;

	if (!(c = selws->sel) || c->fullscreen)
		return;
	if (!selws->sel->floating) {
		adjmvfocus(argv, movestack);
		return;
	}
	if ((absolute = !strcmp(*argv, "absolute"))) {
		w = c->w, h = c->h, x = c->x, y = c->y;
		argv++;
	}

	while (*argv && i < 4) {
		n = strtol(*argv, NULL, 0);
		if ((arg = !strcmp("x", *argv)) || n || !strcmp(*argv, "0")) {
			argv++;
			x = arg ? strtol(*argv, NULL, 0) : n;
		} else if ((arg = !strcmp("y", *argv)) || n || !strcmp(*argv, "0")) {
			argv++;
			y = arg ? strtol(*argv, NULL, 0) : n;
		} else if ((arg = !strcmp("w", *argv)) || n) {
			argv++;
			w = arg ? strtol(*argv, NULL, 0) : n;
		} else if ((arg = !strcmp("h", *argv)) || n) {
			argv++;
			h = arg ? strtol(*argv, NULL, 0) : n;
		}
		argv++;
		i++;
	}
	if (absolute)
		resize(c, x, y, MAX(50, w), MAX(50, h), c->bw);
	else
		resize(c, c->x + x, c->y + y, MAX(50, c->w + w), MAX(50, c->h + h), c->bw);
	confine(c);
	eventignore(XCB_ENTER_NOTIFY);
}

void cmdmvstack(char **argv)
{
	if (!selws->sel || selws->sel->fullscreen || selws->sel->floating)
		return;
	adjmvfocus(argv, movestack);
}

void cmdnmaster(char **argv)
{
	int i, opt;

	opt = optparse(argv, minopts, &i, NULL, 0);
	adjmstack(i, opt, 1);
}

void cmdnstack(char **argv)
{
	int i, opt;

	opt = optparse(argv, minopts, &i, NULL, 0);
	adjmstack(i, opt, 0);
}

void cmdparse(char *buf)
{
	char *k, *args[10], *dbuf;
	uint i, n = 0, matched = 0;

	dbuf = strdup(buf);
	if ((k = strtok(dbuf, " \t\n\r"))) {
		for (i = 0; i < LEN(keywords); i++)
			if (!strcmp(keywords[i].name, k)) {
				matched = 1;
				while (n < sizeof(args) && (args[n++] = strtok(NULL, " =\"\t\n\r")))
					;
				if (*args)
					keywords[i].func((char **)args);
				else
					fprintf(cmdresp, "!keyword requires additional arguments: %s", k);
				break;
			}
		if (!matched)
			fprintf(cmdresp, "!unknown keyword: %s", k);
	}
	free(dbuf);
	fflush(cmdresp);
	fclose(cmdresp);
}

void cmdrule(char **argv)
{
	uint i;
	char *s, **r;

	if (!(s = argv[0]))
		return;
	r = argv + 1;
	for (i = 0; i < LEN(rulecmds); i++)
		if (!strcmp(rulecmds[i].name, s)) {
			rulecmds[i].func(r);
			return;
		}
}

void cmdsend(int num)
{
	Client *c;

	if (!(c = selws->sel) || num == selws->num || !itows(num))
		return;
	unfocus(c, 1);
	setclientws(c, num);
	focus(NULL);
	layoutws(NULL);
}

void cmdset(char **argv)
{
	uint i;
	char *s, **r;

	if (!(s = argv[0]))
		return;
	r = argv + 1;
	for (i = 0; i < LEN(setcmds); i++)
		if (!strcmp(setcmds[i].name, s)) {
			setcmds[i].func(r);
			return;
		}
}

void cmdsplit(char **argv)
{
	int opt;
	float f, nf;

	if (!selws->layout->fn)
		return;
	opt = optparse(argv, minopts, NULL, &f, 0);
	if (f == 0.0 || (opt != -1 && (f > 0.9 || f < 0.1 || !(f -= selws->split))))
		return;
	if ((nf = f < 1.0 ? f + selws->split : f - 1.0) < 0.1 || nf > 0.9)
		return;
	selws->split = nf;
	layoutws(selws);
}

void cmdstick(char **argv)
{
	Client *c;

	if (!(c = selws->sel) || c->fullscreen)
		return;
	setsticky(c, !c->sticky);
	(void)(argv);
}

void cmdswap(char **argv)
{
	Client *c;

	if (!(c = selws->sel) || c->floating || !selws->layout->fn)
		return;
	if (c == nextt(selws->clients) && !(c = nextt(c->next)))
		return;
	detach(c, 1);
	focus(NULL);
	layoutws(c->ws);
	(void)(argv);
}

void cmdwin(char **argv)
{
	uint i;
	char *s, **r;

	if (!argv || !argv[0])
		return;
	s = argv[0];
	r = argv + 1;
	for (i = 0; i < LEN(wincmds); i++)
		if (!strcmp(wincmds[i].name, s)) {
			wincmds[i].func(r);
			return;
		}
}

void cmdwm(char **argv)
{
	int opt;
	char *const arg[] = { argv0, NULL };
	enum { wmrld,  wmrst,  wmext };
	char *opts[] = {
		[wmrld] = "reload", [wmrst] = "restart", [wmext] = "exit", NULL
	};

	if ((opt = optparse(argv, opts, NULL, NULL, 0)) != -1) {
		if (opt == wmrld)
			execcfg();
		else if (opt == wmrst)
			execvp(arg[0], arg);
		else
			running = 0;
	}
}

void cmdws(char **argv)
{
	uint j;
	int i = INT_MAX, n;
	void (*fn)(int) = cmdview; /* assume view so `ws 1` is the same as `ws view 1` */

	if (!argv || !*argv)
		return;
	while (*argv) {
		if ((n = strtol(*argv, NULL, 0)) || !strcmp(*argv, "0"))
			i = n;
		else for (j = 0; j < LEN(wscommands); j++)
			if (wscommands[j].fn && !strcmp(wscommands[j].name, *argv))
				fn = wscommands[j].fn;
		argv++;
	}
	if (i < (int)numws && i >= 0)
		fn(i);
}

void cmdview(int num)
{
	Workspace *ws;

	if (num == selws->num || !(ws = itows(num)))
		return;
	changews(ws, 0);
	focus(NULL);
	layoutws(NULL);
	restack(selws);
}

void clientconfigurerequest(Client *c, xcb_configure_request_event_t *e)
{
	Monitor *m;

	if (e->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH)
		c->bw = e->border_width;
	else if (c->floating || !selws->layout->fn) {
		m = c->ws->mon;
		if (e->value_mask & XCB_CONFIG_WINDOW_X) {
			c->old_x = c->x;
			c->x = m->x + e->x;
		}
		if (e->value_mask & XCB_CONFIG_WINDOW_Y) {
			c->old_y = c->y;
			c->y = m->y + e->y;
		}
		if (e->value_mask & XCB_CONFIG_WINDOW_WIDTH) {
			c->old_w = c->w;
			c->w = e->width;
		}
		if (e->value_mask & XCB_CONFIG_WINDOW_HEIGHT) {
			c->old_h = c->h;
			c->h = e->height;
		}
		if (c->x + W(c) > m->x + m->w)
			c->x = (m->wx + m->ww - W(c)) / 2;
		if (c->y + H(c) > m->y + m->h)
			c->y = (m->wy + m->wh - H(c)) / 2;
		if ((e->value_mask & XYMASK) && !(e->value_mask & WHMASK))
			sendconfigure(c);
		if (c->ws == c->ws->mon->ws)
			resize(c, c->x, c->y, c->w, c->h, c->bw);
	} else {
		sendconfigure(c);
	}
}

Client *coordtoclient(int x, int y)
{
	Client *c;

	FOR_EACH(c, selws->clients)
		if (x > c->x && x < c->x + W(c) && y > c->y && y < c->y + H(c))
			break;
	return c;
}

Monitor *coordtomon(int x, int y)
{
	Monitor *m;

	FOR_EACH(m, monitors)
		if (x >= m->x && x < m->x + m->w && y >= m->y && y < m->y + m->h)
			return m;
	return selws->mon;
}

void confine(Client *c)
{
	Monitor *m = c->ws->mon;

	if (c->x + W(c) > m->wx + m->ww)
		c->x = c->ws->mon->wx + c->ws->mon->ww - W(c);
	if (c->y + H(c) > m->wy + m->wh)
		c->y = c->ws->mon->wy + c->ws->mon->wh - H(c);
	c->x = MAX(c->x, c->ws->mon->wx);
	c->y = MAX(c->y, c->ws->mon->wy);
}

void detach(Client *c, int reattach)
{ /* detach client from it's workspaces client list, can reattach to save calling attach() */
	Client **tc = &c->ws->clients;

	while (*tc && *tc != c)
		tc = &(*tc)->next;
	*tc = c->next;
	if (reattach)
		attach(c, 1);
}

void detachstack(Client *c)
{ /* detach client from it's workspaces focus stack list */
	Client **tc = &c->ws->stack;

	while (*tc && *tc != c)
		tc = &(*tc)->snext;
	*tc = c->snext;
	if (c == c->ws->sel)
		c->ws->sel = c->ws->stack;
}

void *ecalloc(size_t elems, size_t size)
{ /* calloc(3) elems elements of size size, exit with message on error */
	void *p;

	if (!(p = calloc(elems, size)))
		err(1, "unable to allocate space");
	return p;
}

void eventhandle(xcb_generic_event_t *ev)
{
	Client *c;
	Monitor *m;
	Workspace *ws;
	Panel *p = NULL;
	static xcb_timestamp_t lasttime = 0;

	switch (XCB_EVENT_RESPONSE_TYPE(ev)) {
		case XCB_FOCUS_IN:
		{
			xcb_focus_in_event_t *e = (xcb_focus_in_event_t *)ev;

			if (e->mode == XCB_NOTIFY_MODE_GRAB
					|| e->mode == XCB_NOTIFY_MODE_UNGRAB
					|| e->detail == XCB_NOTIFY_DETAIL_POINTER
					|| e->detail == XCB_NOTIFY_DETAIL_POINTER_ROOT
					|| e->detail == XCB_NOTIFY_DETAIL_NONE) {
				return;
			}
			if (selws->sel && e->event != selws->sel->win)
				takefocus(selws->sel);
			return;
		}
		case XCB_CONFIGURE_NOTIFY:
		{
			xcb_configure_notify_event_t *e = (xcb_configure_notify_event_t *)ev;

			if (e->window == root && (scr_h != e->height || scr_w != e->width)) {
				scr_w = e->width;
				scr_h = e->height;
				if (randrbase < 0) {
					monitors->w = monitors->ww = scr_w;
					monitors->h = monitors->wh = scr_h;
					fixupworkspaces();
				}
			}
			return;
		}
		case XCB_CONFIGURE_REQUEST:
		{
			xcb_configure_request_event_t *e = (xcb_configure_request_event_t *)ev;

			if ((c = wintoclient(e->window))) {
				DBG("eventhandle: configure request on managed window: 0x%x", e->window)
				clientconfigurerequest(c, e);
			} else {
				DBG("eventhandle: configure request on unmanaged window: 0x%x", e->window)
				xcb_params_configure_window_t wc;
				wc.x = e->x;
				wc.y = e->y;
				wc.width = e->width;
				wc.height = e->height;
				wc.sibling = e->sibling;
				wc.stack_mode = e->stack_mode;
				wc.border_width = e->border_width;
				xcb_configure_window(con, e->window, e->value_mask, &wc);
			}
			xcb_flush(con);
			return;
		}
		case XCB_DESTROY_NOTIFY:
		{
			xcb_destroy_notify_event_t *e = (xcb_destroy_notify_event_t *)ev;

			if ((c = wintoclient(e->window)))
				freeclient(c, 1);
			else if ((p = wintopanel(e->window)))
				freepanel(p, 1);
			return;
		}
		case XCB_ENTER_NOTIFY:
		{
			xcb_enter_notify_event_t *e = (xcb_enter_notify_event_t *)ev;

			if (e->mode != XCB_NOTIFY_MODE_NORMAL || e->detail == XCB_NOTIFY_DETAIL_INFERIOR)
				return;
			if ((ws = (c = wintoclient(e->event)) ? c->ws : wintows(e->event)) != selws) {
				unfocus(selws->sel, 1);
				changews(ws, 1);
			} else if (!focusmouse || !c || c == selws->sel)
				return;
			focus(c);
			return;
		}
		case XCB_BUTTON_PRESS:
		{
			xcb_button_press_event_t *e = (xcb_button_press_event_t *)ev;

			if (!(c = wintoclient(e->event)))
				return;
			focus(c);
			restack(c->ws);
			xcb_allow_events(con, XCB_ALLOW_REPLAY_POINTER, e->time);
			if (CLNMOD(e->state) == CLNMOD(mousemod)
					&& (e->detail == mousemove || e->detail == mouseresize))
				mousemvr(e->detail == mousemove);
			return;
		}
		case XCB_MOTION_NOTIFY:
		{
			xcb_motion_notify_event_t *e = (xcb_motion_notify_event_t *)ev;

			if (e->event != root)
				return;
			if ((e->time - lasttime) < (1000 / 60)) /* not too frequently */
				return;
			lasttime = e->time;
			if ((m = coordtomon(e->root_x, e->root_y)) != selws->mon) {
				unfocus(selws->sel, 1);
				changews(m->ws, 1);
				focus(NULL);
			}
			return;
		}
		case XCB_MAP_REQUEST:
		{
			xcb_get_geometry_reply_t *g;
			xcb_get_window_attributes_reply_t *wa;
			xcb_map_request_event_t *e = (xcb_map_request_event_t *)ev;

			if ((c = wintoclient(e->window)) || (p = wintopanel(e->window)))
				return;
			if (!(wa = winattr(e->window)) || !(g = wingeom(e->window)))
				return;
			if (winprop(e->window, netatom[WindowType]) == netatom[Dock])
				initpanel(e->window, g);
			else if (!wa->override_redirect)
				initclient(e->window, XCB_WINDOW_NONE, g);
			free(wa);
			free(g);
			return;
		}
		case XCB_UNMAP_NOTIFY:
		{
			xcb_unmap_notify_event_t *e = (xcb_unmap_notify_event_t *)ev;

			if (XCB_EVENT_SENT(ev))
				setwinstate(e->window, XCB_ICCCM_WM_STATE_WITHDRAWN);
			else if ((c = wintoclient(e->window)))
				freeclient(c, 0);
			else if ((p = wintopanel(e->window)))
				freepanel(p, 0);
			return;
		}
		case XCB_CLIENT_MESSAGE:
		{
			xcb_client_message_event_t *e = (xcb_client_message_event_t *)ev;
			xcb_atom_t fs = netatom[Fullscreen];
			uint32_t *d = e->data.data32;

			DBG("---- CLIENT MESSAGE ENTER ----")
			if (e->window == root && e->type == netatom[CurDesktop]) {
				DBG("%s client message on root window - data: %d", netatoms[CurDesktop], d[0])
				cmdview(d[0]);
			} else if ((c = wintoclient(e->window))) {
				if (e->type == netatom[WmDesktop] && d[0] < (uint)numws && d[0] != (uint)c->ws->num) {
					DBG("%s client message on window: 0x%08x - data: %d", netatoms[WmDesktop], c->win, d[0])
					if (c == selws->sel) {
						unfocus(c, 1);
						focus(NULL);
					}
					ws = c->ws;
					setclientws(c, d[0]);
					if (d[0] == (uint)itows(d[0])->mon->ws->num || ws == ws->mon->ws)
						layoutws(NULL);
				} else if (e->type == netatom[State] && (d[1] == fs || d[2] == fs)) {
					DBG("%s client message on window: 0x%08x - data: %d", netatoms[Fullscreen], c->win, d[0])
					setfullscreen(c, (d[0] == 1 || (d[0] == 2 && !c->fullscreen)));
				} else if (e->type == netatom[ActiveWindow] && d[0] < (uint)numws) {
					DBG("%s client message on window: 0x%08x", netatoms[ActiveWindow], c->win)
					unfocus(selws->sel, 1);
					cmdview(c->ws->num);
					focus(c);
					restack(selws);
				}
			}
			DBG("---- CLIENT MESSAGE LEAVE ----")
			return;
		}
		case XCB_PROPERTY_NOTIFY:
		{
			xcb_window_t trans;
			xcb_property_notify_event_t *e = (xcb_property_notify_event_t *)ev;

			if (e->atom == netatom[StrutPartial] && (p = wintopanel(e->window))) {
				updatestruts(p, 1);
				layoutws(NULL);
			} else if (e->state != XCB_PROPERTY_DELETE && (c = wintoclient(e->window))) {
				switch (e->atom) {
				case XCB_ATOM_WM_TRANSIENT_FOR:
					if (c->floating || (trans = wintrans(c->win)) == XCB_NONE)
						return;
					if ((c->floating = (wintoclient(trans) != NULL)))
						layoutws(c->ws);
					break;
				case XCB_ATOM_WM_NORMAL_HINTS:
					sizehints(c);
					break;
				case XCB_ATOM_WM_HINTS:
					winhints(c);
					break;
				}
				if (e->atom == netatom[WindowType])
					wintype(c);
			}
			return;
		}
		default:
		{
			if (randrbase != -1 && ev->response_type == randrbase + XCB_RANDR_SCREEN_CHANGE_NOTIFY) {
				if (updaterandr() > 0)
					fixupworkspaces();
				return;
			}
			xcb_request_error_t *e = (xcb_request_error_t *)ev;
			/* BadWindow
			 * or BadMatch & SetInputFocus/ConfigureWindow
			 * or BadAccess & GrabButton/GrabKey */
			if (ev->response_type || e->error_code == 3
					|| (e->error_code == 8  && (e->major_opcode == 42 || e->major_opcode == 12))
					|| (e->error_code == 10 && (e->major_opcode == 28 || e->major_opcode == 33)))
				return;
			/* TODO: some kind of error handling for those we don't want to ignore */
			warnx("failed request: %s - %s: 0x%08x", xcb_event_get_request_label(e->major_opcode),
					xcb_event_get_error_label(e->error_code), e->bad_value);
			return;
		}
	}
}

void eventignore(uint8_t type)
{ /* ignore the event type until the queue is cleared */
	xcb_generic_event_t *ev;

	xcb_flush(con);
	while (running && (ev = xcb_poll_for_event(con))) {
		if (XCB_EVENT_RESPONSE_TYPE(ev) != type)
			eventhandle(ev);
		free(ev);
	}
}

void eventloop(void)
{ /* wait for events or commands while the user hasn't requested quit */
	Client *c;
	ssize_t n;
	Workspace *ws;
	fd_set read_fds;
	char buf[PIPE_BUF];
	int confd, nfds, cmdfd;
	xcb_generic_event_t *ev;
	static struct sockaddr_un sockaddr;

	if (!(sock = getenv("YAXWM_SOCK"))) {
		sock = "/tmp/yaxwmsock";
		if (setenv("YAXWM_SOCK", sock, 0) < 0)
			err(1, "unable to export socket path to environment: %s", sock);
	}
	sockaddr.sun_family = AF_UNIX;
	strlcpy(sockaddr.sun_path, sock, sizeof(sockaddr.sun_path));
	if (sockaddr.sun_path[0] == '\0')
		err(1, "unable to write socket path: %s", sock);
	if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		err(1, "unable to create socket: %s", sock);
	unlink(sock);
	if (bind(sockfd, (struct sockaddr *)&sockaddr, sizeof(sockaddr)) < 0)
		err(1, "unable to bind socket: %s", sock);
	if (listen(sockfd, SOMAXCONN) < 0)
		err(1, "unable to listen to socket: %s", sock);

	execcfg();
	if (winrules)
		FOR_CLIENTS(c, ws)
			applywinrule(c);
	layoutws(NULL);
	focus(NULL);

	confd = xcb_get_file_descriptor(con);
	nfds = MAX(confd, sockfd);
	nfds++;
	while (running) {
		xcb_flush(con);
		FD_ZERO(&read_fds);
		FD_SET(sockfd, &read_fds);
		FD_SET(confd, &read_fds);
		if (select(nfds, &read_fds, NULL, NULL, NULL) > 0) {
			if (FD_ISSET(sockfd, &read_fds)) {
				cmdfd = accept(sockfd, NULL, 0);
				if (cmdfd > 0 && (n = recv(cmdfd, buf, sizeof(buf)-1, 0)) > 0
						&& *buf != '#' && *buf != '\n')
				{
					if (buf[n - 1] == '\n')
						n--;
					buf[n] = '\0';
					if ((cmdresp = fdopen(cmdfd, "w")) != NULL)
						cmdparse(buf);
					else {
						warn("unable to open the socket as file: %s", sock);
						close(cmdfd);
					}
				}
			}
			if (FD_ISSET(confd, &read_fds)) {
				while ((ev = xcb_poll_for_event(con))) {
					eventhandle(ev);
					free(ev);
				}
			}
		}
	}
}

void execcfg(void)
{
	char *cfg, *home;
	char path[PATH_MAX];

	if (!(cfg = getenv("YAXWM_CONF"))) {
		if (!(home = getenv("XDG_CONFIG_HOME")) && !(home = getenv("HOME")))
			return;
		strlcpy(path, home, sizeof(path));
		strlcat(path, "/.config/yaxwm/", sizeof(path));
		strlcat(path, "yaxwmrc", sizeof(path));
		cfg = path;
	}
	if (!fork()) {
		if (con)
			close(xcb_get_file_descriptor(con));
		setsid();
		execle(cfg, cfg, (char *)NULL, environ);
		warn("unable to execute config file");
	}
}

void fixupworkspaces(void)
{ /* after monitor(s) change we need to reassign workspaces and resize fullscreen clients */
	Client *c;
	Workspace *ws;

	assignworkspaces();
	FOR_CLIENTS(c, ws)
		if (c->fullscreen)
			resize(c, ws->mon->x, ws->mon->y, ws->mon->w, ws->mon->h, c->bw);
	if (panels)
		updatestruts(panels, 1);
	focus(NULL);
	layoutws(NULL);
	restack(selws);
}

void focus(Client *c)
{ /* focus client (making it the head of the focus stack)
   * when client is NULL focus the current workspace stack head */
	if (!selws)
		return;
	if (!c || c->ws != c->ws->mon->ws)
		c = selws->stack;
	if (selws->sel && selws->sel != c)
		unfocus(selws->sel, 0);
	if (c) {
		if (c->urgent)
			seturgency(c, 0);
		detachstack(c);
		attachstack(c);
		grabbuttons(c, 1);
		xcb_change_window_attributes(con, c->win, XCB_CW_BORDER_PIXEL, &borders[Focus]);
		takefocus(c);
	} else {
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
		xcb_delete_property(con, root, netatom[ActiveWindow]);
	}
	selws->sel = c;
}

void freeclient(Client *c, int destroyed)
{ /* detach client and free it, if !destroyed we update the state to withdrawn */
	if (!c)
		return;
	Workspace *ws, *cws = c->ws;

	detach(c, 0);
	detachstack(c);
	if (!destroyed) {
		xcb_grab_server(con);
		xcb_configure_window(con, c->win, BWMASK, &c->old_bw);
		xcb_ungrab_button(con, XCB_BUTTON_INDEX_ANY, c->win, XCB_MOD_MASK_ANY);
		setwinstate(c->win, XCB_ICCCM_WM_STATE_WITHDRAWN);
		xcb_flush(con);
		xcb_ungrab_server(con);
	}
	free(c);
	xcb_delete_property(con, root, netatom[ClientList]);
	FOR_CLIENTS(c, ws)
		PROP_APPEND(root, netatom[ClientList], XCB_ATOM_WINDOW, 32, 1, &c->win);
	layoutws(cws);
	focus(NULL);
}

void freewinrule(WindowRule *r)
{ /* detach client and free it, if !destroyed we update the state to withdrawn */
	WindowRule **cr = &winrules;

	while (*cr && *cr != r)
		cr = &(*cr)->next;
	*cr = r->next;
	if (r->class)
		regfree(&(r->classreg));
	if (r->inst)
		regfree(&(r->instreg));
	if (r->title)
		regfree(&(r->titlereg));
	free(r->mon);
	free(r->inst);
	free(r->title);
	free(r->class);
	free(r);
}

void freemon(Monitor *m)
{ /* detach and free a monitor and it's name */
	Monitor *mon;

	if (m == monitors)
		monitors = monitors->next;
	else {
		FIND_PREV(mon, m, monitors);
		if (mon)
			mon->next = m->next;
	}
	free(m->name);
	free(m);
}

void freepanel(Panel *p, int destroyed)
{
	Panel **pp = &panels;

	while (*pp && *pp != p)
		pp = &(*pp)->next;
	*pp = p->next;
	if (!destroyed) {
		xcb_grab_server(con);
		setwinstate(p->win, XCB_ICCCM_WM_STATE_WITHDRAWN);
		xcb_flush(con);
		xcb_ungrab_server(con);
	}
	updatestruts(p, 0);
	free(p);
	layoutws(NULL);
}

void freewm(void)
{ /* exit yaxwm, free everything and cleanup X */
	uint i;
	Workspace *ws;

	FOR_EACH(ws, workspaces)
		while (ws->stack)
			freeclient(ws->stack, 0);
	xcb_key_symbols_free(keysyms);
	while (panels)
		freepanel(panels, 0);
	while (monitors)
		freemon(monitors);
	while (workspaces)
		freews(workspaces);
	while (winrules)
		freewinrule(winrules);
	for (i = 0; i < LEN(cursors); i++)
		xcb_free_cursor(con, cursor[i]);
	xcb_destroy_window(con, wmcheck);
	xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT,
			XCB_INPUT_FOCUS_POINTER_ROOT, XCB_CURRENT_TIME);
	xcb_flush(con);
	xcb_delete_property(con, root, netatom[ActiveWindow]);
	xcb_disconnect(con);
	close(sockfd);
	unlink(sock);
}

void freews(Workspace *ws)
{ /* detach and free workspace */
	Workspace *sel;

	if (ws == workspaces)
		workspaces = workspaces->next;
	else {
		FIND_PREV(sel, ws, workspaces);
		if (sel)
			sel->next = ws->next;
	}
	free(ws);
}

void grabbuttons(Client *c, int focused)
{
	xcb_generic_error_t *e;
	xcb_keysym_t nlock = 0xff7f;
	xcb_keycode_t *kc, *t = NULL;
	xcb_get_modifier_mapping_reply_t *m = NULL;
	uint mods[] = { 0, XCB_MOD_MASK_LOCK, 0, XCB_MOD_MASK_LOCK };

	numlockmask = 0;
	if ((m = xcb_get_modifier_mapping_reply(con, xcb_get_modifier_mapping(con), &e))) {
		if ((t = xcb_key_symbols_get_keycode(keysyms, nlock))
				&& (kc = xcb_get_modifier_mapping_keycodes(m)))
			for (uint i = 0; i < 8; i++)
				for (uint j = 0; j < m->keycodes_per_modifier; j++)
					if (kc[i * m->keycodes_per_modifier + j] == *t)
						numlockmask = (1 << i);
	} else {
		checkerror(0, "unable to get modifier mapping for numlock", e);
	}
	free(t);
	free(m);

	/* apply the mask to search elements */
	mods[2] |= numlockmask, mods[3] |= numlockmask;
	xcb_ungrab_button(con, XCB_BUTTON_INDEX_ANY, c->win, XCB_BUTTON_MASK_ANY);
	if (!focused)
		xcb_grab_button(con, 0, c->win, BUTTONMASK, XCB_GRAB_MODE_SYNC,
				XCB_GRAB_MODE_SYNC, XCB_NONE, XCB_NONE, XCB_BUTTON_INDEX_ANY, XCB_BUTTON_MASK_ANY);
	for (uint i = 0; i < LEN(mods); i++) {
			xcb_grab_button(con, 0, c->win, BUTTONMASK, XCB_GRAB_MODE_ASYNC,
					XCB_GRAB_MODE_SYNC, XCB_NONE, XCB_NONE, mousemove, mousemod | mods[i]);
			xcb_grab_button(con, 0, c->win, BUTTONMASK, XCB_GRAB_MODE_ASYNC,
					XCB_GRAB_MODE_SYNC, XCB_NONE, XCB_NONE, mouseresize, mousemod | mods[i]);
	}
}

int grabpointer(xcb_cursor_t cursor)
{ /* grab the mouse pointer on the root window with cursor passed */
	int r = 0;
	xcb_generic_error_t *e;
	xcb_grab_pointer_cookie_t pc;
	xcb_grab_pointer_reply_t *ptr = NULL;

	pc = xcb_grab_pointer(con, 0, root, XCB_EVENT_MASK_BUTTON_RELEASE
			| XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_POINTER_MOTION_HINT,
			XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, root, cursor, XCB_CURRENT_TIME);
	if ((ptr = xcb_grab_pointer_reply(con, pc, &e)))
		r = ptr->status == XCB_GRAB_STATUS_SUCCESS;
	else
		checkerror(0, "unable to grab pointer", e);
	free(ptr);
	return r;
}

void gravitate(Client *c, int vert, int horz, int matchgap)
{
	int x, y, gap;

	if (!c || !c->ws || !c->floating)
		return;
	x = c->x, y = c->y;
	gap = matchgap ? c->ws->gappx : 0;
	switch (horz) {
	case Left: x = c->ws->mon->wx + gap; break;
	case Right: x = c->ws->mon->wx + c->ws->mon->ww - W(c) - gap; break;
	case Center: x = (c->ws->mon->wx + c->ws->mon->ww - W(c)) / 2; break;
	}
	switch (vert) {
	case Top: y = c->ws->mon->wy + gap; break;
	case Bottom: y = c->ws->mon->wy + c->ws->mon->wh - H(c) - gap; break;
	case Center: y = (c->ws->mon->wy + c->ws->mon->wh - H(c)) / 2; break;
	}
	resizehint(c, x, y, c->w, c->h, c->bw, 0);
}

void initatoms(xcb_atom_t *atoms, const char **names, int num)
{ /* intern atoms in bulk */
	int i;
	xcb_generic_error_t *e;
	xcb_intern_atom_reply_t *r;
	xcb_intern_atom_cookie_t c[num];

	for (i = 0; i < num; ++i)
		c[i] = xcb_intern_atom(con, 0, strlen(names[i]), names[i]);
	for (i = 0; i < num; ++i) {
		if ((r = xcb_intern_atom_reply(con, c[i], &e))) {
			atoms[i] = r->atom;
			free(r);
		} else {
			checkerror(0, "unable to initialize atom", e);
		}
	}
}

void initclient(xcb_window_t win, xcb_window_t trans, xcb_get_geometry_reply_t *g)
{ /* allocate and setup new client from window */
	Client *c, *t;
	Callback *cb = NULL;
	xcb_window_t none = XCB_WINDOW_NONE;

	c = ecalloc(1, sizeof(Client));
	c->win = win;
	c->x = c->old_x = g->x, c->y = c->old_y = g->y;
	c->w = c->old_w = g->width, c->h = c->old_h = g->height;
	c->old_bw = g->border_width;
	if ((trans != none || (trans = wintrans(c->win)) != none) && (t = wintoclient(trans)))
		setclientws(c, t->ws->num);
	else
		cb = applywinrule(c);
	confine(c);
	c->bw = borders[Width];
	xcb_configure_window(con, c->win, BWMASK, &c->bw);
	sendconfigure(c);
	wintype(c);
	sizehints(c);
	winhints(c);
	xcb_change_window_attributes(con, c->win, XCB_CW_EVENT_MASK | XCB_CW_BORDER_PIXEL,
			(uint []){ borders[Unfocus], XCB_EVENT_MASK_ENTER_WINDOW
			| XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_PROPERTY_CHANGE
			| XCB_EVENT_MASK_STRUCTURE_NOTIFY });
	grabbuttons(c, 0);
	if (c->floating || (c->floating = c->oldstate = trans != none || c->fixed)) {
		if (c->x + c->y == 0)
			gravitate(c, Center, Center, 0);
		setstackmode(c->win, XCB_STACK_MODE_ABOVE);
	}
	PROP_APPEND(root, netatom[ClientList], XCB_ATOM_WINDOW, 32, 1, &c->win);
	MOVE(c->win, c->x + 2 * scr_w, c->y); /* some windows require this */
	setwinstate(c->win, XCB_ICCCM_WM_STATE_NORMAL);
	if (c->ws == c->ws->mon->ws || c->sticky) {
		if (c->ws == selws)
			unfocus(selws->sel, 0);
		c->ws->sel = c;
		layoutws(c->ws);
	} else { /* hide windows on non-visible workspaces */
		MOVE(c->win, H(c) * -2, c->y);
	}
	xcb_map_window(con, c->win);
	focus(NULL);
	if (cb)
		cb->fn(c);
	DBG("initclient: client mapped - workspace %d: %d,%d @ %dx%d - floating: %d - nofocus: %d",
			c->ws->num, c->x, c->y, c->w, c->h, c->floating, c->nofocus)
}

void initwinrule(char *class, char *inst, char *title, char *mon,
		int ws, int floating, int sticky, Callback *cb)
{
	size_t len;
	WindowRule *r;

	r = ecalloc(1, sizeof(WindowRule));
	r->class = NULL;
	r->inst = NULL;
	r->title = NULL;
	r->mon = NULL;
	r->ws = ws;
	r->floating = floating;
	r->sticky = sticky;
	r->cb = cb;
	if (mon) {
		len = strlen(mon) + 1;
		r->mon = ecalloc(1, len);
		strlcpy(r->mon, mon, len);
	}
	if (initwinruleregcomp(r, class, inst, title)) {
		r->next = winrules;
		winrules = r;
	} else {
		free(r->mon);
		free(r);
	}
	DBG("initwinrule: SUCCESS rule: class: %s - inst: %s - title: %s - mon: %s - ws: %d - floating: %d - sticky: %d",
			r->class, r->inst, r->title, r->mon, r->ws, r->floating, r->sticky)
	return;

}

int initwinruleregcomp(WindowRule *r, char *class, char *inst, char *title)
{
	int i;
	size_t len;
	char buf[NAME_MAX], *e;

	if (class) {
		len = strlen(class) + 1;
		r->class = ecalloc(1, len);
		strlcpy(r->class, class, len);
	}
	if (inst) {
		len = strlen(inst) + 1;
		r->inst = ecalloc(1, len);
		strlcpy(r->inst, inst, len);
	}
	if (title) {
		len = strlen(title) + 1;
		r->title = ecalloc(1, len);
		strlcpy(r->title, title, len);
	}
	if (class && (i = regcomp(&(r->classreg), r->class, REG_NOSUB | REG_EXTENDED | REG_ICASE))) {
		regerror(i, &(r->classreg), buf, sizeof(buf));
		e = "class";
		goto error;
	} else if (inst && (i = regcomp(&(r->instreg), r->inst, REG_NOSUB | REG_EXTENDED | REG_ICASE))) {
		regerror(i, &(r->instreg), buf, sizeof(buf));
		e = "instance";
		goto error;
	} else if (title && (i = regcomp(&(r->titlereg), r->title, REG_NOSUB | REG_EXTENDED | REG_ICASE))) {
		regerror(i, &(r->titlereg), buf, sizeof(buf));
		e = "title";
		goto error;
	}
	return 1;

error:
	fprintf(cmdresp, "!unable to compile %s regex %s: %s - %s", e, r->title, buf, strerror(errno));
	if (r->class) {
		regfree(&(r->classreg));
		free(r->class);
	}
	if (r->inst) {
		regfree(&(r->instreg));
		free(r->inst);
	}
	if (r->title) {
		regfree(&(r->titlereg));
		free(r->title);
	}
	return 0;
}

void initpanel(xcb_window_t win, xcb_get_geometry_reply_t *g)
{
	int *s;
	Panel *p;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t rc;
	xcb_get_property_reply_t *r = NULL;
	uint m = XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY;

	p = ecalloc(1, sizeof(Panel));
	p->win = win;
	p->x = g->x, p->y = g->y, p->w = g->width, p->h = g->height;
	p->mon = coordtomon(p->x, p->y);
	rc = xcb_get_property(con, 0, p->win, netatom[StrutPartial], XCB_ATOM_CARDINAL, 0, 4);
	if (!(r = xcb_get_property_reply(con, rc, &e)) || r->type == XCB_NONE) {
		checkerror(0, "unable to get _NET_WM_STRUT_PARTIAL from window", e);
		rc = xcb_get_property(con, 0, p->win, netatom[Strut], XCB_ATOM_CARDINAL, 0, 4);
		if (!(r = xcb_get_property_reply(con, rc, &e)))
			checkerror(0, "unable to get _NET_WM_STRUT or _NET_WM_STRUT_PARTIAL from window", e);
	}
	if (r && r->value_len && (s = xcb_get_property_value(r))) {
		DBG("initpanel: struts: %d, %d, %d, %d", s[0], s[1], s[2], s[3])
		p->strut_l = s[0], p->strut_r = s[1], p->strut_t = s[2], p->strut_b = s[3];
		updatestruts(p, 1);
	}
	free(r);
	attachpanel(p);
	xcb_change_window_attributes(con, p->win, XCB_CW_EVENT_MASK, &m);
	setwinstate(p->win, XCB_ICCCM_WM_STATE_NORMAL);
	xcb_map_window(con, p->win);
	layoutws(NULL);
	DBG("initpanel: panel mapped - mon: %s - geom: %d,%d @ %dx%d", p->mon->name, p->x, p->y, p->w, p->h)
}

Monitor *initmon(char *name, xcb_randr_output_t id, int x, int y, int w, int h)
{ /* allocate a monitor from randr output */
	Monitor *m;
	uint len = strlen(name) + 1;

	DBG("initmon: initializing new monitor: %s - %d,%d - %dx%d", name, x, y, w, h)
	m = ecalloc(1, sizeof(Monitor));
	m->x = m->wx = x;
	m->y = m->wy = y;
	m->w = m->ww = w;
	m->h = m->wh = h;
	m->id = id;
	m->name = ecalloc(1, len);
	if (len > 1)
		strlcpy(m->name, name, len);
	return m;
}

int initrandr(void)
{
	int extbase;
	const xcb_query_extension_reply_t *ext;

	ext = xcb_get_extension_data(con, &xcb_randr_id);
	if (!ext->present)
		return -1;
	updaterandr();
	extbase = ext->first_event;
	xcb_randr_select_input(con, root, XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE
			|XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE|XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE
			|XCB_RANDR_NOTIFY_MASK_OUTPUT_PROPERTY);
	return extbase;
}

void initscan(void)
{ /* walk root window tree and init existing windows */
	uint i;
	xcb_atom_t *s;
	xcb_window_t *w, *t;
	xcb_generic_error_t *e;
	xcb_query_tree_reply_t *rt;
	xcb_get_geometry_reply_t **g;
	xcb_get_window_attributes_reply_t **wa;
	uint8_t v = XCB_MAP_STATE_VIEWABLE, ic = XCB_ICCCM_WM_STATE_ICONIC;

	if ((rt = xcb_query_tree_reply(con, xcb_query_tree(con, root), &e))) {
		w = xcb_query_tree_children(rt);
		s = ecalloc(rt->children_len, sizeof(xcb_atom_t));
		t = ecalloc(rt->children_len, sizeof(xcb_window_t));
		g = ecalloc(rt->children_len, sizeof(xcb_get_geometry_reply_t *));
		wa = ecalloc(rt->children_len, sizeof(xcb_get_window_attributes_reply_t *));
		for (i = 0; i < rt->children_len; i++) { /* non-transients */
			g[i] = NULL;
			t[i] = s[i] = XCB_WINDOW_NONE;
			if (!(wa[i] = winattr(w[i])) || !(g[i] = wingeom(w[i]))) {
				w[i] = XCB_WINDOW_NONE;
			} else if (!(wa[i]->map_state == v || winprop(w[i], wmatom[WMState]) == ic)) {
				w[i] = 0;
			} else if (!wa[i]->override_redirect && !(t[i] = wintrans(w[i]))) {
				if (winprop(w[i], netatom[WindowType]) == netatom[Dock])
					initpanel(w[i], g[i]);
				else
					initclient(w[i], t[i], g[i]);
				w[i] = 0;
			}
		}
		for (i = 0; i < rt->children_len; i++) { /* transients */
			if (w[i] && t[i] && !wa[i]->override_redirect)
				initclient(w[i], t[i], g[i]);
			free(wa[i]);
			free(g[i]);
		}
		free(rt);
		free(s);
		free(t);
		free(wa);
		free(g);
	} else {
		checkerror(1, "unable to query tree from root window", e);
	}
}

void initwm(void)
{ /* setup internals, binds, atoms, and root window event mask */
	uint i, j;
	int r, cws;
	Workspace *ws;
	size_t len = 1;
	xcb_void_cookie_t c;
	xcb_cursor_context_t *ctx;

	if ((randrbase = initrandr()) < 0 || !monitors)
		monitors = initmon("default", 0, 0, 0, scr_w, scr_h);
	if (!primary)
		primary = monitors;
	initworkspaces();
	assignworkspaces();

	for (i = 0; i < LEN(dborder); i++)
		dborder[i] = borders[i];

	if (xcb_cursor_context_new(con, scr, &ctx) < 0)
		err(1, "unable to create cursor context");
	for (i = 0; i < LEN(cursors); i++)
		cursor[i] = xcb_cursor_load_cursor(ctx, cursors[i]);
	xcb_cursor_context_free(ctx);

	initatoms(wmatom, wmatoms, LEN(wmatoms));
	initatoms(netatom, netatoms, LEN(netatoms));
	wmcheck = xcb_generate_id(con);
	xcb_create_window(con, XCB_COPY_FROM_PARENT, wmcheck, root, -1, -1, 1, 1, 0,
			XCB_WINDOW_CLASS_INPUT_ONLY, scr->root_visual, 0, NULL);
	PROP_REPLACE(wmcheck, netatom[Check], XCB_ATOM_WINDOW, 32, 1, &wmcheck);
	PROP_REPLACE(wmcheck, netatom[Name], wmatom[Utf8Str], 8, 5, "yaxwm");
	PROP_REPLACE(root, netatom[Check], XCB_ATOM_WINDOW, 32, 1, &wmcheck);
	updatenumws(numws);
	PROP_REPLACE(root, netatom[Supported], XCB_ATOM_ATOM, 32, LEN(netatom), netatom);
	xcb_delete_property(con, root, netatom[ClientList]);
	cws = (r = winprop(root, netatom[CurDesktop])) >= 0 ? r : 0;
	changews((ws = itows(cws)) ? ws : workspaces, 1);
	FOR_EACH(ws, workspaces)
		len += strlen(ws->name) + 1;
	char names[len];
	len = 0;
	FOR_EACH(ws, workspaces)
		for (j = 0; (names[len++] = ws->name[j]); j++);
	PROP_REPLACE(root, netatom[DesktopNames], wmatom[Utf8Str], 8, --len, names);
	c = xcb_change_window_attributes_checked(con, root, XCB_CW_EVENT_MASK | XCB_CW_CURSOR,
			(uint []){ XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT
			| XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_BUTTON_PRESS
			| XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_ENTER_WINDOW
			| XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_STRUCTURE_NOTIFY
			| XCB_EVENT_MASK_PROPERTY_CHANGE,
			cursor[Normal] });
	checkerror(1, "unable to change root window event mask and cursor", xcb_request_check(con, c));
	if (!(keysyms = xcb_key_symbols_alloc(con)))
		err(1, "unable to get keysyms from X connection");
}

void initworkspaces(void)
{ /* setup default workspaces from user specified workspace rules */
	Workspace *ws;

	for (numws = 0; numws < (int)LEN(workspacerules); numws++) {
		FIND_TAIL(ws, workspaces);
		if (ws)
			ws->next = initws(numws, &workspacerules[numws]);
		else
			workspaces = initws(numws, &workspacerules[numws]);
	}
}

Workspace *initws(int num, WorkspaceRule *r)
{
	Workspace *ws;

	DBG("initws: initializing new workspace: '%s': %d", r->name, num)
	ws = ecalloc(1, sizeof(Workspace));
	ws->num = num;
	ws->name = r->name;
	ws->nmaster = r->nmaster;
	ws->nstack = r->nstack;
	ws->gappx = r->gappx;
	ws->split = r->split;
	ws->layout = r->layout;
	return ws;
}

char *itoa(int n, char *s)
{ /* convert n to chars in s */
	char c;
	int j, i = 0, sign = n;

	if (sign < 0)
		n = -n;
	do { /* convert digits to chars in reverse */
		s[i++] = n % 10 + '0';
	} while ((n /= 10) > 0);
	if (sign < 0)
		s[i++] = '-';
	s[i] = '\0';
	for (j = i - 1, i = 0; i < j; i++, j--) { /* un-reverse s */
		c = s[i];
		s[i] = s[j];
		s[j] = c;
	}
	return s;
}

Workspace *itows(int num)
{ /* return workspace matching num, otherwise NULL */
	Workspace *ws;

	for (ws = workspaces; ws && (int)ws->num != num; ws = ws->next)
		;
	return ws;
}

void layoutws(Workspace *ws)
{ /* show currently visible clients and restack workspaces */
	if (ws)
		showhide(ws->stack);
	else FOR_EACH(ws, workspaces)
		showhide(ws->stack);
	if (ws) {
		if (ws->layout->fn)
			ws->layout->fn(ws);
		restack(ws);
	} else FOR_EACH(ws, workspaces)
		if (ws == ws->mon->ws && ws->layout->fn)
			ws->layout->fn(ws);
}

void monocle(Workspace *ws)
{
	Client *c;
	Monitor *m = ws->mon;

	for (c = nextt(ws->clients); c; c = nextt(c->next))
		resize(c, m->wx, m->wy, m->ww, m->wh,
				borders[Smart] ? 0 : borders[Width]);
}

void movefocus(int direction)
{ /* focus the next or previous client on the active workspace */
	Client *c, *sel = selws->sel;

	if (!sel || sel->fullscreen)
		return;
	if (direction > 0)
		c = sel->next ? sel->next : selws->clients;
	else
		FIND_PREV(c, sel, selws->clients);
	if (c) {
		focus(c);
		restack(c->ws);
	}
}

void movestack(int direction)
{
	int i = 0;
	Client *c;

	if (!selws->sel || selws->sel->floating || !nextt(selws->clients->next))
		return;
	if (direction > 0) { /* swap current and the next or move to the front when at the end */
		detach(selws->sel, (c = nextt(selws->sel->next)) ? 0 : 1);
		if (c) { /* attach within the list */
			selws->sel->next = c->next;
			c->next = selws->sel;
		}
	} else { /* swap the current and the previous or move to the end when at the front */
		if (selws->sel == nextt(selws->clients)) { /* attach to end */
			detach(selws->sel, 0);
			attach(selws->sel, 0);
		} else {
			FIND_PREVTILED(c, selws->sel, selws->clients);
			detach(selws->sel, (i = (c == nextt(selws->clients)) ? 1 : 0));
			if (!i) { /* attach within the list */
				selws->sel->next = c;
				FIND_PREV(c, selws->sel->next, selws->clients); /* find the real (non-tiled) previous to c */
				c->next = selws->sel;
			}
		}
	}
	layoutws(selws);
	focus(selws->sel);
}

void mousemvr(int move)
{
	Client *c;
	Monitor *m;
	xcb_timestamp_t last = 0;
	xcb_motion_notify_event_t *e;
	xcb_generic_event_t *ev = NULL;
	int mx, my, ox, oy, ow, oh, nw, nh, nx, ny, x, y, released = 0;

	if (!(c = selws->sel) || c->fullscreen || !querypointer(&mx, &my))
		return;
	ox = nx = c->x, oy = ny = c->y, ow = nw = c->w, oh = nh = c->h;
	if (c != c->ws->sel)
		focus(c);
	restack(c->ws);
	if (!grabpointer(cursor[move ? Move : Resize]))
		return;
	while (running && !released) {
		if (!(ev = xcb_poll_for_event(con)))
			while (!(ev = xcb_wait_for_event(con)))
				xcb_flush(con);
		switch (XCB_EVENT_RESPONSE_TYPE(ev)) {
		case XCB_MOTION_NOTIFY:
			e = (xcb_motion_notify_event_t *)ev;
			/* we shouldn't need to query the pointer and just use the event root_x, root_y
			 * but for whatever reason there is some buffering happening and this forces
			 * a flush, using xcb_flush doesn't not seem to work in this case */
			if (!querypointer(&x, &y) || (e->time - last) < (1000 / 60))
				break;
			last = e->time;
			if (move)
				nx = ox + (x - mx), ny = oy + (y - my);
			else
				nw = ow + (x - mx), nh = oh + (y - my);
			if ((nw != c->w || nh != c->h || nx != c->x || ny != c->y) && !c->floating && selws->layout->fn) {
				c->old_x = c->x, c->old_y = c->y, c->old_h = c->h, c->old_w = c->w;
				cmdfloat(NULL);
				layoutws(c->ws);
			}
			if (!c->ws->layout->fn || c->floating) {
				if (move && (m = coordtomon(x, y)) != c->ws->mon) {
					setclientws(c, m->ws->num);
					changews(m->ws, 1);
					focus(c);
				}
				resizehint(c, nx, ny, nw, nh, c->bw, 1);
			}
			break;
		case XCB_BUTTON_RELEASE:
			released = 1;
			break;
		default:
			eventhandle(ev);
			break;
		}
		free(ev);
	}
	ungrabpointer();
	if (!move)
		eventignore(XCB_ENTER_NOTIFY);
}

Client *nextt(Client *c)
{ /* return c if it's not floating, or walk the list until we find one that isn't */
	while (c && c->floating)
		c = c->next;
	return c;
}

int optparse(char **argv, char **opts, int *argi, float *argf, int hex)
{ /* parses argv for arguments we're interested in */
	float f;
	char **s = opts;
	int i, ret = -1;

	if (!argv || !*argv)
		return ret;
	if (argi)
		*argi = INT_MAX;
	if (argf)
		*argf = 0.0;
	while (*argv) {
		DBG("argv: %s", *argv)
		if (argi && ((hex && **argv == '#' && strlen(*argv) == 7)
					|| (i = strtol(*argv, NULL, 0)) || !strcmp(*argv, "0"))) {

			*argi = hex && **argv == '#' ? strtol(*argv + 1, NULL, 16) : i;
			DBG("*argi: %d", *argi)
		} else if (argf && (f = strtof(*argv, NULL)))
			*argf = f;
		else if (opts)
			for (s = opts, i = 0; ret < 0 && s && *s; s++, i++)
				if (!strcmp(*s, *argv)) {
					if ((argi && *argi != INT_MAX) || (argf && *argf != 0.0))
						return i; /* we don't need to parse the rest */
					ret = i;
				}
		argv++;
	}
	return ret;
}

Monitor *outputtomon(xcb_randr_output_t id)
{
	Monitor *m;

	FOR_EACH(m, monitors)
		if (m->id == id)
			break;
	return m;
}

#ifdef DEBUG
void print(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
}
#endif

int querypointer(int *x, int *y)
{
	xcb_generic_error_t *e;
	xcb_query_pointer_reply_t *p;

	if ((p = xcb_query_pointer_reply(con, xcb_query_pointer(con, root), &e))) {
		*x = p->root_x, *y = p->root_y;
		free(p);
		return 1;
	} else {
		checkerror(0, "unable to query pointer", e);
	}
	return 0;
}

Monitor *randrclone(xcb_randr_output_t id, int x, int y)
{
	Monitor *m;

	FOR_EACH(m, monitors)
		if (id != m->id && m->x == x && m->y == y)
			break;
	return m;
}

void resize(Client *c, int x, int y, int w, int h, int bw)
{
	uint v[] = { x, y, w, h, bw };

	c->old_x = c->x, c->old_y = c->y;
	c->old_w = c->w, c->old_h = c->h;
	c->x = x, c->y = y, c->w = w, c->h = h;
	xcb_configure_window(con, c->win, XYMASK | WHMASK | BWMASK, v);
	sendconfigure(c);
}

void resizehint(Client *c, int x, int y, int w, int h, int bw, int usermotion)
{
	if (applysizehints(c, &x, &y, &w, &h, usermotion))
		resize(c, x, y, w, h, bw);
}

void restack(Workspace *ws)
{
	Client *c;

	if (!ws)
		ws = selws;
	if (!ws || !(c = ws->sel))
		return;
	if (c->floating || !ws->layout->fn)
		setstackmode(c->win, XCB_STACK_MODE_ABOVE);
	if (ws->layout->fn) {
		FOR_STACK(c, ws->stack)
			if (!c->floating && c->ws == c->ws->mon->ws)
				setstackmode(c->win, XCB_STACK_MODE_BELOW);
	}
	eventignore(XCB_ENTER_NOTIFY);
}

int rulecmp(WindowRule *r, char *title, char *class, char *inst)
{
	if (!r)
		return 0;
	if (r->title && r->class && r->inst)
		return (!regexec(&(r->titlereg), title, 0, NULL, 0)
				&& !regexec(&(r->classreg), class, 0, NULL, 0)
				&& !regexec(&(r->instreg), inst, 0, NULL, 0));
	if (r->class && r->inst)
		return (!regexec(&(r->classreg), class, 0, NULL, 0)
				&& !regexec(&(r->instreg), inst, 0, NULL, 0));
	if (r->title && r->class)
		return (!regexec(&(r->titlereg), title, 0, NULL, 0)
				&& !regexec(&(r->classreg), class, 0, NULL, 0));
	if (r->title && r->inst)
		return (!regexec(&(r->titlereg), title, 0, NULL, 0)
				&& !regexec(&(r->instreg), inst, 0, NULL, 0));
	if (r->class)
		return !regexec(&(r->classreg), class, 0, NULL, 0);
	if (r->inst)
		return !regexec(&(r->instreg), inst, 0, NULL, 0);
	if (r->title)
		return !regexec(&(r->titlereg), title, 0, NULL, 0);
	return 0;
}

void sendconfigure(Client *c)
{ /* send client a configure notify event */
	xcb_configure_notify_event_t ce;

	ce.event = c->win;
	ce.window = c->win;
	ce.response_type = XCB_CONFIGURE_NOTIFY;
	ce.x = c->x;
	ce.y = c->y;
	ce.width = c->w;
	ce.height = c->h;
	ce.border_width = c->bw;
	ce.above_sibling = XCB_NONE;
	ce.override_redirect = 0;
	sendevent(c, (char *)&ce, XCB_EVENT_MASK_STRUCTURE_NOTIFY);
}

int sendevent(Client *c, const char *ev, long mask)
{
	xcb_void_cookie_t vc;

	vc = xcb_send_event_checked(con, 0, c->win, mask, ev);
	return checkerror(0, "unable to send configure notify event to client window",
			xcb_request_check(con, vc));
}

int sendwmproto(Client *c, int wmproto)
{
	int n, exists = 0;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t rpc;
	xcb_client_message_event_t cme;
	xcb_icccm_get_wm_protocols_reply_t proto;

	rpc = xcb_icccm_get_wm_protocols(con, c->win, wmatom[Protocols]);
	if (xcb_icccm_get_wm_protocols_reply(con, rpc, &proto, &e)) {
		n = proto.atoms_len;
		while (!exists && n--)
			exists = proto.atoms[n] == wmatom[wmproto];
		xcb_icccm_get_wm_protocols_reply_wipe(&proto);
	} else {
		checkerror(0, "unable to get requested wm protocol", e);
	}
	if (exists) {
		cme.response_type = XCB_CLIENT_MESSAGE;
		cme.window = c->win;
		cme.type = wmatom[Protocols];
		cme.format = 32;
		cme.data.data32[0] = wmatom[wmproto];
		cme.data.data32[1] = XCB_TIME_CURRENT_TIME;
		sendevent(c, (char *)&cme, XCB_EVENT_MASK_NO_EVENT);
	}
	return exists;
}

void setclientws(Client *c, uint num)
{
	DBG("setclientws: setting client atom -- _NET_WM_DESKTOP: %d", num)
	if (c->ws) {
		detach(c, 0);
		detachstack(c);
	}
	c->ws = itows(num);
	PROP_REPLACE(c->win, netatom[WmDesktop], XCB_ATOM_CARDINAL, 32, 1, &num);
	attach(c, 0);
	attachstack(c);
}

void setfullscreen(Client *c, int fullscreen)
{
	Monitor *m;

	if (!c->ws || !(m = c->ws->mon))
		m = selws->mon;
	if (fullscreen && !c->fullscreen) {
		PROP_REPLACE(c->win, netatom[State], XCB_ATOM_ATOM, 32, 1, &netatom[Fullscreen]);
		c->oldstate = c->floating;
		c->old_bw = c->bw;
		c->fullscreen = 1;
		c->floating = 1;
		c->bw = 0;
		resize(c, m->x, m->y, m->w, m->h, c->bw);
		setstackmode(c->win, XCB_STACK_MODE_ABOVE);
		xcb_flush(con);
	} else if (!fullscreen && c->fullscreen) {
		PROP_REPLACE(c->win, netatom[State], XCB_ATOM_ATOM, 32, 0, (uchar *)0);
		c->floating = c->oldstate;
		c->fullscreen = 0;
		c->bw = c->old_bw;
		c->x = c->old_x;
		c->y = c->old_y;
		c->w = c->old_w;
		c->h = c->old_h;
		resize(c, c->x, c->y, c->w, c->h, c->bw);
		layoutws(c->ws);
	}
}

void setsticky(Client *c, int sticky)
{
	if (sticky && !c->sticky)
		c->sticky = 1;
	else if (!sticky && c->sticky)
		c->sticky = 0;
}

void setstackmode(xcb_window_t win, uint mode)
{
	xcb_configure_window(con, win, XCB_CONFIG_WINDOW_STACK_MODE, &mode);
}

void setwinstate(xcb_window_t win, uint32_t state)
{
	uint32_t s[] = { state, XCB_ATOM_NONE };
	PROP_REPLACE(win, wmatom[WMState], wmatom[WMState], 32, 2, s);
}

void seturgency(Client *c, int urg)
{
	xcb_generic_error_t *e;
	xcb_icccm_wm_hints_t wmh;
	xcb_get_property_cookie_t pc;

	pc = xcb_icccm_get_wm_hints(con, c->win);
	c->urgent = urg;
	DBG("seturgency: setting urgency hint for window: 0x%08x -- value: %d", c->win, urg)
	if (xcb_icccm_get_wm_hints_reply(con, pc, &wmh, &e)) {
		if (urg)
			wmh.flags |= XCB_ICCCM_WM_HINT_X_URGENCY;
		else
			wmh.flags &= ~XCB_ICCCM_WM_HINT_X_URGENCY;
		xcb_icccm_set_wm_hints(con, c->win, &wmh);
	} else {
		checkerror(0, "unable to get wm window hints", e);
	}
}

void showhide(Client *c)
{
	Client *sel;

	if (!c)
		return;
	if (c->ws == c->ws->mon->ws) { /* show clients top down */
		DBG("showhide: showing window: 0x%08x - workspace: %d - active workspace: %d", c->win, c->ws->num, selws->num)
		MOVE(c->win, c->x, c->y);
		if ((!c->ws->layout->fn || c->floating) && !c->fullscreen)
			resize(c, c->x, c->y, c->w, c->h, c->bw);
		showhide(c->snext);
	} else { /* hide clients bottom up */
		showhide(c->snext);
		DBG("showhide: hiding window: 0x%08x - workspace: %d - active workspace: %d", c->win, c->ws->num, selws->num)
		if (!c->sticky)
			MOVE(c->win, W(c) * -2, c->y);
		else if (c->ws != selws && selws && c->ws->mon == selws->mon) {
			sel = lastws->sel == c ? c : selws->sel;
			setclientws(c, selws->num); /* keep sticky windows on the current workspace on one monitor */
			focus(sel);
		}
	}
}

void sighandle(int sig)
{
	switch (sig) {
	case SIGINT: /* fallthrough */
	case SIGTERM: /* fallthrough */
	case SIGHUP: /* fallthrough */
		running = 0;
		break;
	case SIGCHLD:
		signal(sig, sighandle);
		while (waitpid(-1, NULL, WNOHANG) > 0)
			;
		break;
	}
}

void sizehints(Client *c)
{
	xcb_size_hints_t s;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t pc;

	pc = xcb_icccm_get_wm_normal_hints(con, c->win);
	DBG("sizehints: getting size hints for window: 0x%08x", c->win)
	c->max_aspect = c->min_aspect = 0.0;
	c->increment_w = c->increment_h = 0;
	c->min_w = c->min_h = c->max_w = c->max_h = c->base_w = c->base_h = 0;
	if (xcb_icccm_get_wm_normal_hints_reply(con, pc, &s, &e)) {
		if (s.flags & XCB_ICCCM_SIZE_HINT_US_POSITION)
			c->x = s.x, c->y = s.y;
		if (s.flags & XCB_ICCCM_SIZE_HINT_US_SIZE)
			c->w = s.width, c->h = s.height;
		if (s.flags & XCB_ICCCM_SIZE_HINT_P_ASPECT) {
			c->min_aspect = (float)s.min_aspect_den / s.min_aspect_num;
			c->max_aspect = (float)s.max_aspect_num / s.max_aspect_den;
		}
		if (s.flags & XCB_ICCCM_SIZE_HINT_P_MAX_SIZE)
			c->max_w = s.max_width, c->max_h = s.max_height;
		if (s.flags & XCB_ICCCM_SIZE_HINT_P_RESIZE_INC)
			c->increment_w = s.width_inc, c->increment_h = s.height_inc;
		if (s.flags & XCB_ICCCM_SIZE_HINT_BASE_SIZE)
			c->base_w = s.base_width, c->base_h = s.base_height;
		else if (s.flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE)
			c->base_w = s.min_width, c->base_h = s.min_height;
		if (s.flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE)
			c->min_w = s.min_width, c->min_h = s.min_height;
		else if (s.flags & XCB_ICCCM_SIZE_HINT_BASE_SIZE)
			c->min_w = s.base_width, c->min_h = s.base_height;
	} else {
		checkerror(0, "unable to get wm normal hints", e);
	}
	c->fixed = (c->max_w && c->max_h && c->max_w == c->min_w && c->max_h == c->min_h);
}

void takefocus(Client *c)
{
	if (!c->nofocus) {
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, c->win, XCB_CURRENT_TIME);
		PROP_REPLACE(root, netatom[ActiveWindow], XCB_ATOM_WINDOW, 32, 1, &c->win);
	}
	sendwmproto(c, TakeFocus);
}

void tile(Workspace *ws)
{
	Client *c;
	Monitor *m = ws->mon;
	uint i, n, my, ssy, sy, nr;
	uint mw = 0, ss = 0, sw = 0, ns = 1, bw = 0;

	for (n = 0, c = nextt(ws->clients); c; c = nextt(c->next), n++)
		;
	if (!n)
		return;
	if (n > 1 || !borders[Smart])
		bw = borders[Width];
	if (n <= ws->nmaster)
		mw = m->ww, ss = 1;
	else if (ws->nmaster)
		ns = 2, mw = m->ww * ws->split;
	if (ws->nstack && n - ws->nmaster > ws->nstack)
		ss = 1, sw = (m->ww - mw) / 2;
	for (i = 0, my = sy = ssy = ws->gappx, c = nextt(ws->clients); c; c = nextt(c->next), ++i) {
		if (i < ws->nmaster) {
			nr = MIN(n, ws->nmaster) - i;
			resize(c, m->wx + ws->gappx, m->wy + my,
					mw - ws->gappx * (5 - ns) / 2 - (2 * bw),
					((m->wh - my) / MAX(1, nr)) - ws->gappx - (2 * bw), bw);
			my += c->h + (2 * bw) + ws->gappx;
		} else if (i - ws->nmaster < ws->nstack) {
			nr = MIN(n - ws->nmaster, ws->nstack) - (i - ws->nmaster);
			resize(c, m->wx + mw + (ws->gappx / ns), m->wy + sy,
					(m->ww - mw - sw - ws->gappx * (5 - ns - ss) / 2) - (2 * bw),
					(m->wh - sy) / MAX(1, nr) - ws->gappx - (2 * bw), bw);
			sy += c->h + (2 * bw) + ws->gappx;
		} else {
			resize(c, m->wx + mw + sw + (ws->gappx / ns), m->wy + ssy,
					(m->ww - mw - sw - ws->gappx * (5 - ns) / 2) - (2 * bw),
					(m->wh - ssy) / MAX(1, n - i) - ws->gappx - (2 * bw), c->bw);
			ssy += c->h + (2 * bw) + ws->gappx;
		}
	}
}

void unfocus(Client *c, int focusroot)
{
	if (!c)
		return;
	grabbuttons(c, 0);
	xcb_change_window_attributes(con, c->win, XCB_CW_BORDER_PIXEL, &borders[Unfocus]);
	if (focusroot) {
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
		xcb_delete_property(con, root, netatom[ActiveWindow]);
	}
}

void ungrabpointer(void)
{
	xcb_void_cookie_t c;

	c = xcb_ungrab_pointer_checked(con, XCB_CURRENT_TIME);
	checkerror(1, "failed to ungrab pointer", xcb_request_check(con, c));
}

void updatenumws(int needed)
{
	char name[4]; /* we're never gonna have more than 999 workspaces */
	Workspace *ws;
	WorkspaceRule r;

	if (needed > 999)
		errx(1, "attempting to allocate too many workspaces");
	else if (needed > numws) {
		while (needed > numws) {
			FIND_TAIL(ws, workspaces);
			r.name = itoa(numws, name);
			r.nmaster = ws->nmaster;
			r.nstack = ws->nstack;
			r.gappx = ws->gappx;
			r.split = ws->split;
			r.layout = ws->layout;
			ws->next = initws(numws, &r);
			numws++;
		}
	}
	PROP_REPLACE(root, netatom[NumDesktops], XCB_ATOM_CARDINAL, 32, 1, &numws);
}

int updateoutputs(xcb_randr_output_t *outs, int len, xcb_timestamp_t timestamp)
{
	uint n;
	Monitor *m;
	char name[64];
	int i, changed = 0;
	xcb_generic_error_t *e;
	xcb_randr_get_crtc_info_cookie_t ck;
	xcb_randr_get_output_info_reply_t *o;
	xcb_randr_get_crtc_info_reply_t *c;
	xcb_randr_get_output_info_cookie_t oc[len];
	xcb_randr_get_output_primary_reply_t *po = NULL;

	for (i = 0; i < len; i++)
		oc[i] = xcb_randr_get_output_info(con, outs[i], timestamp);
	for (i = 0; i < len; i++) {
		if (!(o = xcb_randr_get_output_info_reply(con, oc[i], &e))) {
			checkerror(0, "unable to get monitor info", e);
			continue;
		}
		if (o->crtc != XCB_NONE && o->connection != XCB_RANDR_CONNECTION_DISCONNECTED) {
			ck = xcb_randr_get_crtc_info(con, o->crtc, timestamp);
			if (!(c = xcb_randr_get_crtc_info_reply(con, ck, &e))) {
				checkerror(0, "crtc info for randr output was NULL", e);
				free(o);
				continue;
			}
			n = xcb_randr_get_output_info_name_length(o) + 1;
			strlcpy(name, (char *)xcb_randr_get_output_info_name(o), MIN(sizeof(name), n));
			DBG("updateoutputs: crtc: %s -- location: %d,%d -- size: %dx%d -- status: %d",
					name, c->x, c->y, c->width, c->height, c->status)
			if ((m = randrclone(outs[i], c->x, c->y))) {
				DBG("updateoutputs: monitor %s, id %d is a clone of %s, id %d, skipping",
						name, outs[i], m->name, m->id)
			} else if ((m = outputtomon(outs[i]))) {
				changed = (c->x != m->x || c->y != m->y || c->width != m->w || c->height != m->h);
				m->x = m->wx = c->x;
				m->y = m->wy = c->y;
				m->w = m->ww = c->width;
				m->h = m->wh = c->height;
				DBG("updateoutputs: new size and location for monitor: %s -- %d,%d @ %dx%d -- %s",
						m->name, m->x, m->y, m->w, m->h, changed ? "updated" : "unchanged")
			} else {
				FIND_TAIL(m, monitors);
				if (m)
					m->next = initmon(name, outs[i], c->x, c->y, c->width, c->height);
				else
					monitors = initmon(name, outs[i], c->x, c->y, c->width, c->height);
				changed = 1;
			}
			free(c);
		} else if ((m = outputtomon(outs[i])) && o->connection == XCB_RANDR_CONNECTION_DISCONNECTED) {
			DBG("updateoutputs: output is inactive or disconnected: %s -- freeing", m->name)
			freemon(m);
			changed = 1;
		}
		free(o);
	}

	po = xcb_randr_get_output_primary_reply(con, xcb_randr_get_output_primary(con, root), NULL);
	if (po) {
		primary = outputtomon(po->output);
		DBG("updateoutputs: setting primary monitor: %s - centering cursor", primary->name)
		xcb_warp_pointer(con, root, root, 0, 0, 0, 0,
				primary->x + (primary->w / 2), primary->y + (primary->h / 2));
	}
	free(po);
	return changed;
}

int updaterandr(void)
{
	int len, changed;
	xcb_timestamp_t timestamp;
	xcb_generic_error_t *e;
	xcb_randr_output_t *outputs;
	xcb_randr_get_screen_resources_current_reply_t *r;
	xcb_randr_get_screen_resources_current_cookie_t rc;

	DBG("updaterandr: querying current randr outputs")
	rc = xcb_randr_get_screen_resources_current(con, root);
	if (!(r = xcb_randr_get_screen_resources_current_reply(con, rc, &e))) {
		checkerror(0, "unable to get screen resources", e);
		return -1;
	}
	timestamp = r->config_timestamp;
	len = xcb_randr_get_screen_resources_current_outputs_length(r);
	outputs = xcb_randr_get_screen_resources_current_outputs(r);
	changed = updateoutputs(outputs, len, timestamp);
	free(r);
	return changed;
}

void updatestruts(Panel *p, int apply)
{
	Panel *n;
	Monitor *m;

	FOR_EACH(m, monitors)
		m->wx = m->x, m->wy = m->y, m->ww = m->w, m->wh = m->h;
	if (!p)
		return;
	if (apply && !panels)
		applypanelstrut(p);
	FOR_EACH(n, panels)
		if ((apply || n != p) && (n->strut_l || n->strut_r || n->strut_t || n->strut_b))
			applypanelstrut(p);
}

xcb_get_window_attributes_reply_t *winattr(xcb_window_t win)
{
	xcb_generic_error_t *e;
	xcb_get_window_attributes_cookie_t c;
	xcb_get_window_attributes_reply_t *wa;

	c = xcb_get_window_attributes(con, win);
	DBG("winattr: getting window attributes from window: 0x%08x", win)
	if (!(wa = xcb_get_window_attributes_reply(con, c, &e)))
		checkerror(0, "unable to get window attributes", e);
	return wa;
}

xcb_get_geometry_reply_t *wingeom(xcb_window_t win)
{
	xcb_generic_error_t *e;
	xcb_get_geometry_cookie_t gc;
	xcb_get_geometry_reply_t *g = NULL;

	gc = xcb_get_geometry(con, win);
	DBG("wingeom: getting window geometry from window: 0x%08x", win)
	if (!(g = xcb_get_geometry_reply(con, gc, &e)))
		checkerror(0, "failed to get window geometry reply", e);

	return g;
}

void winhints(Client *c)
{
	xcb_generic_error_t *e;
	xcb_icccm_wm_hints_t wmh;
	xcb_get_property_cookie_t pc;

	pc = xcb_icccm_get_wm_hints(con, c->win);
	if (xcb_icccm_get_wm_hints_reply(con, pc, &wmh, &e)) {
		if (c == selws->sel && wmh.flags & XCB_ICCCM_WM_HINT_X_URGENCY) {
			wmh.flags &= ~XCB_ICCCM_WM_HINT_X_URGENCY;
			xcb_icccm_set_wm_hints(con, c->win, &wmh);
		} else {
			c->urgent = (wmh.flags & XCB_ICCCM_WM_HINT_X_URGENCY) ? 1 : 0;
		}
		c->nofocus = (wmh.flags & XCB_ICCCM_WM_HINT_INPUT) ? !wmh.input : 0;
	} else {
		checkerror(0, "unable to get wm window hints", e);
	}
}

xcb_atom_t winprop(xcb_window_t win, xcb_atom_t prop)
{
	xcb_atom_t ret;
	xcb_generic_error_t *e;
	xcb_get_property_reply_t *r = NULL;
	xcb_get_property_cookie_t c;

	c = xcb_get_property(con, 0, win, prop, XCB_ATOM_ANY, 0, 1);
	ret = -1;
	if ((r = xcb_get_property_reply(con, c, &e)) && xcb_get_property_value_length(r))
		ret = *(xcb_atom_t *)xcb_get_property_value(r);
	else
		checkerror(0, "unable to get window property", e);
	free(r);
	return ret;
}

int wintextprop(xcb_window_t w, xcb_atom_t atom, char *text, size_t size)
{
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t c;
	xcb_icccm_get_text_property_reply_t r;

	c = xcb_icccm_get_text_property(con, w, atom);
	if (!xcb_icccm_get_text_property_reply(con, c, &r, &e)) {
		checkerror(0, "failed to get text property", e);
		return 0;
	}
	/* FIXME: encoding */

	if(!r.name || !r.name_len)
		return 0;
	strlcpy(text, r.name, size);
	xcb_icccm_get_text_property_reply_wipe(&r);
	return 1;
}

Client *wintoclient(xcb_window_t win)
{
	Client *c;
	Workspace *ws;

	if (win == root)
		return NULL;
	FOR_CLIENTS(c, ws)
		if (c->win == win)
			return c;
	return NULL;
}

Panel *wintopanel(xcb_window_t win)
{
	Panel *p;

	if (win == root)
		return NULL;
	FOR_EACH(p, panels)
		if (p->win == win)
			return p;
	return p;
}

Workspace *wintows(xcb_window_t win)
{
	int x, y;
	Client *c;
	Workspace *ws;

	if (win == root && querypointer(&x, &y))
		return coordtomon(x, y)->ws;
	FOR_CLIENTS(c, ws)
		if (c->win == win)
			return ws;
	return selws;
}

xcb_window_t wintrans(xcb_window_t win)
{
	xcb_window_t trans;
	xcb_get_property_cookie_t pc;
	xcb_generic_error_t *e = NULL;

	pc = xcb_icccm_get_wm_transient_for(con, win);
	trans = XCB_WINDOW_NONE;
	if (!xcb_icccm_get_wm_transient_for_reply(con, pc, &trans, &e) && e) {
		warnx("unable to get wm transient for hint - X11 error: %d: %s",
				e->error_code, xcb_event_get_error_label(e->error_code));
		free(e);
	}
	return trans;
}

void wintype(Client *c)
{
	xcb_atom_t t;

	if (winprop(c->win, netatom[State]) == netatom[Fullscreen])
		setfullscreen(c, 1);
	else if ((t = winprop(c->win, netatom[WindowType])) == netatom[Dialog])
		c->floating = 1;
}
