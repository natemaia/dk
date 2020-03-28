/* yet another X window manager
* see license file for copyright and license details
* vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
*/

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
#include <xcb/xcb_event.h>
#include <xcb/xcb_util.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_cursor.h>
#include <xcb/xcb_keysyms.h>

#ifdef DEBUG
#define DBG(fmt, ...); print("yaxwm:%d:"fmt, __LINE__, ##__VA_ARGS__);
static void print(const char *fmt, ...);
#else
#define DBG(fmt, ...);
#endif

#ifndef VERSION
#define VERSION "0.3"
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

enum Cursors {
	Normal,
	Move,
	Resize
};

struct Panel {
	int x, y, w, h;
	int strut_l, strut_r, strut_t, strut_b;
	Panel *next;
	Monitor *mon;
	xcb_window_t win;
};

struct Client {
	int x, y, w, h, bw, hoff;
	int old_x, old_y, old_w, old_h, old_bw;
	int max_w, max_h, min_w, min_h;
	int base_w, base_h, increment_w, increment_h;
	float min_aspect, max_aspect;
	int sticky, fixed, floating, fullscreen, ffs, urgent, nofocus, oldstate;
	Client *trans, *next, *snext;
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

struct Layout {
	char *name;
	int (*fn)(Workspace *);
};

struct Keyword {
	char *name;
	void (*func)(char **);
};

struct Command {
	char *name;
	void (*fn)(int);
};

struct Callback {
	char *name;
	void (*fn)(Client *);
};

struct Workspace {
	int num;
	char *name;
	uint nmaster, nstack, gappx;
	uint padr, padl, padt, padb;
	float split;
	float ssplit;
	Layout *layout;
	Monitor *mon;
	Workspace *next;
	Client *sel, *stack, *clients, *hidden;
};

struct WindowRule {
	int x, y, w, h;
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
	float ssplit;
	uint padr, padl, padt, padb;
	Layout *layout;
	WorkspaceRule *next;
};

static Callback *applyrule(Client *c);
static int checkerror(int lvl, char *msg, xcb_generic_error_t *e);
static void cmdborder(char **argv);
static void cmdcycle(char **argv);
static void cmdffs(char **argv);
static void cmdfloat(char **argv);
static void cmdfocus(char **argv);
static void cmdfollow(int num);
static void cmdgappx(char **argv);
static void cmdkill(char **argv);
static void cmdlayout(char **argv);
static void cmdmouse(char **argv);
static void cmdmvresize(char **argv);
static void cmdnmaster(char **argv);
static void cmdnstack(char **argv);
static void cmdpad(char **argv);
static void cmdparse(char *buf);
static void cmdrule(char **argv);
static void cmdsend(int num);
static void cmdset(char **argv);
static void cmdsplit(char **argv);
static void cmdstick(char **argv);
static void cmdswap(char **argv);
static void cmdview(int num);
static void cmdwin(char **argv);
static void cmdwm(char **argv);
static void cmdws(char **argv);
static void confinetomon(Client *c);
static void detach(Client *c, int reattach);
static void *ecalloc(size_t elems, size_t size);
static void eventignore(uint8_t type);
static void eventloop(void);
static void execcfg(void);
static void fixupworkspaces(void);
static void focus(Client *c);
static void freeclient(Client *c, int destroyed);
static void freepanel(Panel *panel, int destroyed);
static void freerule(WindowRule *r);
static void freewm(void);
static void freews(Workspace *ws);
static void grabbuttons(Client *c, int focused);
static void gravitate(Client *c, int vert, int horz, int matchgap);
static void initclient(xcb_window_t win, xcb_window_t trans, xcb_get_geometry_reply_t *g, xcb_atom_t type);
static void initpanel(xcb_window_t win, xcb_get_geometry_reply_t *g);
static int initrulereg(WindowRule *r, WindowRule *wr);
static void initrule(WindowRule *r);
static void initscan(void);
static void initwm(void);
static Workspace *initws(int num, WorkspaceRule *r);
static char *itoa(int n, char *s);
static Workspace *itows(int num);
static int layoutws(Workspace *ws);
static int mono(Workspace *ws);
static void mousemvr(int move);
static void movefocus(int direction);
static void movestack(int direction);
static Client *nextt(Client *c);
static int optparse(char **argv, char **opts, int *argi, float *argf, int hex);
static void printerror(xcb_generic_error_t *e);
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
static void sizehints(Client *c, int uss);
static void takefocus(Client *c);
static int tile(Workspace *ws);
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

enum { stdreset, stdabsolute };
static char *minopts[] = { "absolute", NULL };
static char *stdopts[] = { [stdreset] = "reset", [stdabsolute] = "absolute", NULL };

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

enum NetAtoms {
	ActiveWindow,
	Check,
	ClientList,
	CurDesktop,
	Desktop,
	DesktopNames,
	Dialog,
	Dock,
	Fullscreen,
	Name,
	NumDesktops,
	Splash,
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
	[Desktop] = "_NET_WM_WINDOW_TYPE_DESKTOP",
	[Fullscreen] = "_NET_WM_STATE_FULLSCREEN",
	[Name] = "_NET_WM_NAME",
	[NumDesktops] = "_NET_NUMBER_OF_DESKTOPS",
	[Splash] = "_NET_WM_WINDOW_TYPE_SPLASH",
	[State] = "_NET_WM_STATE",
	[StrutPartial] = "_NET_WM_STRUT_PARTIAL",
	[Strut] = "_NET_WM_STRUT",
	[Supported] = "_NET_SUPPORTED",
	[WindowType] = "_NET_WM_WINDOW_TYPE",
	[WmDesktop] = "_NET_WM_DESKTOP"
};

static int border[] = {
	[Width] = 1,          /* border width in pixels */
	[Smart] = 1,          /* disable borders in mono layout or with only one tiled window */
	[Focus] = 0x6699cc,   /* focused window border colours, hex 0x000000-0xffffff */
	[Unfocus] = 0x000000, /* unfocused window border colours, hex 0x000000-0xffffff */
};

static const char *cursors[] = {
	[Move] = "fleur",
	[Normal] = "arrow",
	[Resize] = "sizing"
};

#include "stringl.c"
#include "config.h"

extern char **environ;            /* environment variables */
static char *argv0;               /* program name */
static int sockfd = -1;           /* socket file descriptor */
static char *sock;                /* socket path, loaded from YAXWM_SOCK environment variable */
static FILE *cmdresp;             /* file used for writing messages to after command */
static int numws = 0;             /* number of workspaces currently allocated */
static int scr_w, scr_h;          /* root window size */
static uint running = 1;          /* continue handling events */
static uint restart = 0;          /* restart wm before quitting */
static int focusmouse = 1;        /* enable focus follows mouse */
static int randrbase = -1;        /* randr extension response */
static uint numlockmask = 0;      /* numlock modifier bit mask */
static int dborder[LEN(border)];  /* default border values used for resetting */
static int wrpending = 1;         /* whether there is a new window rule pending */

static xcb_mod_mask_t mousemod = XCB_MOD_MASK_4;
static xcb_button_t   mousemove = XCB_BUTTON_INDEX_1;
static xcb_button_t   mouseresize = XCB_BUTTON_INDEX_3;

static Panel *panels;         /* panel list head */
static Monitor *primary;      /* primary monitor */
static Monitor *monitors;     /* monitor list head */
static Workspace *selws;      /* active workspace */
static Workspace *lastws;     /* last active workspace */
static Workspace *workspaces; /* workspace list head */
static WindowRule *rules;  /* window rule list head */

static xcb_screen_t *scr;                 /* the X screen */
static xcb_connection_t *con;             /* xcb connection to the X server */
static xcb_window_t root, wmcheck;        /* root window and _NET_SUPPORTING_WM_CHECK window */
static xcb_key_symbols_t *keysyms;        /* current keymap symbols */
static xcb_cursor_t cursor[LEN(cursors)]; /* cursors for moving, resizing, and normal */
static xcb_atom_t wmatom[LEN(wmatoms)];   /* _WM atoms */
static xcb_atom_t netatom[LEN(netatoms)]; /* _NET atoms */


int main(int argc, char *argv[])
{
	int arg;
	char *end;
	argv0 = argv[0];
	xcb_void_cookie_t ck;
	struct sigaction sa;
	int sigs[] = { SIGTERM, SIGINT, SIGHUP, SIGCHLD };
	uint mask = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT;

	if (argc > 1) {
		if (!strcmp(argv[1], "-s") && argv[2]) {
			sockfd = strtol(argv[2], &end, 0);
			if (*end != '\0')
				sockfd = -1;
		} else if ((arg = !strcmp(argv[1], "-v")) || !strcmp(argv[1], "-h")) {
			fprintf(stderr, arg ? "%s "VERSION"\n" : "usage: %s [-hv] [-s SOCKET_FD]\n", argv0);
			exit(0);
		} else {
			fprintf(stderr, "usage: %s [-hv] [-s SOCKET_FD]\n", argv0);
			exit(1);
		}
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
	checkerror(1, "is another window manager running?", xcb_request_check(con, ck));

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

int adjbordergap(int i, int opt, int changing, int other)
{
	DBG("adjbordergap: entering");
	if (opt != stdabsolute)
		return i;
	return MAX(MIN(i, (int)(selws->mon->wh - selws->padb - selws->padt / 6) - other), 0) - changing;
}

void adjnmasterstack(int i, int opt, int master)
{
	uint n = INT_MAX;

	DBG("adjnmasterstack: entering");
	if (i == INT_MAX)
		return;
	if (opt != -1)
		i -= master ? (int)selws->nmaster : (int)selws->nstack;
	if (master && (n = MAX(selws->nmaster + i, 0)) != selws->nmaster)
		selws->nmaster = n;
	else if (!master && (n = MAX(selws->nstack + i, 0)) != selws->nstack)
		selws->nstack = n;
	if (n != INT_MAX && selws->clients)
		layoutws(selws);
}

void applypanelstrut(Panel *p)
{
	DBG("applypanelstrut: entering -- %s window area before: %d,%d @ %dx%d",
			p->mon->name, p->mon->wx, p->mon->wy, p->mon->ww, p->mon->wh);
	if (p->mon->x + p->strut_l > p->mon->wx)
		p->mon->wx = p->strut_l;
	if (p->mon->y + p->strut_t > p->mon->wy)
		p->mon->wy = p->strut_t;
	if (p->mon->w - (p->strut_r + p->strut_l) < p->mon->ww)
		p->mon->ww = p->mon->w - (p->strut_r + p->strut_l);
	if (p->mon->h - (p->strut_b + p->strut_t) < p->mon->wh)
		p->mon->wh = p->mon->h - (p->strut_b + p->strut_t);
	DBG("applypanelstrut: %s window area after: %d,%d @ %dx%d",
			p->mon->name, p->mon->wx, p->mon->wy, p->mon->ww, p->mon->wh);
}

int applysizehints(Client *c, int *x, int *y, int *w, int *h, int usermotion)
{
	int baseismin;
	Monitor *m = c->ws->mon;

	DBG("applysizehints: entering");
	*w = MAX(1, *w);
	*h = MAX(1, *h);
	if (usermotion) {
		if (*x > scr_w)
			*x = scr_w - W(c);
		if (*y > scr_h)
			*y = scr_h - H(c);
		if (*x + *w + 2 * c->bw < 0)
			*x = 0;
		if (*y + *h + 2 * c->bw < 0)
			*y = 0;
	} else {
		*x = MAX(*x, m->wx);
		*y = MAX(*y, m->wy);
		if (*x + W(c) > m->wx + m->ww)
			*x = m->wx + m->ww - W(c);
		if (*y + H(c) > m->wy + m->wh)
			*y = m->wy + m->wh - H(c);
	}
	if (c->floating || !c->ws->layout->fn) {
		if (!(baseismin = c->base_w == c->min_w && c->base_h == c->min_h)) {
			*w -= c->base_w;
			*h -= c->base_h;
		}
		if (c->min_aspect > 0 && c->max_aspect > 0) {
			if (c->max_aspect < (float)*w / *h)
				*w = *h * c->max_aspect + 0.5;
			else if (c->min_aspect < (float)*h / *w)
				*h = *w * c->min_aspect + 0.5;
		}
		if (baseismin) {
			*w -= c->base_w;
			*h -= c->base_h;
		}
		if (c->increment_w)
			*w -= *w % c->increment_w;
		if (c->increment_h)
			*h -= *h % c->increment_h;
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

Callback *applyrule(Client *c)
{
	Monitor *m;
	WindowRule *r;
	Callback *cb = NULL;
	int n, num = -1, ws = selws->num;
	char title[NAME_MAX];
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t pc;
	xcb_icccm_get_wm_class_reply_t prop;

	DBG("applyrule: entering");
	if (!wintextprop(c->win, netatom[Name], title, sizeof(title))
			&& !wintextprop(c->win, XCB_ATOM_WM_NAME, title, sizeof(title)))
		title[0] = '\0';
	pc = xcb_icccm_get_wm_class(con, c->win);
	c->floating = 0;
	if (!c->trans && (ws = winprop(c->win, netatom[WmDesktop])) < 0)
		ws = selws->num;
	if (xcb_icccm_get_wm_class_reply(con, pc, &prop, &e)) {
		DBG("applyrule: window class: %s - instance: %s - title: %s",
				prop.class_name, prop.instance_name, title);
		for (r = rules; r; r = r->next) {
			if (!rulecmp(r, title, prop.class_name, prop.instance_name))
				continue;
			DBG("applyrule: matched -- class: %s, inst: %s, title: %s",
					r->class, r->inst, r->title);
			c->floating = r->floating;
			c->sticky = r->sticky;
			cb = r->cb;
			if (r->x != -1)
				c->x = r->x;
			if (r->y != -1)
				c->y = r->y;
			if (r->w != -1)
				c->w = r->w;
			if (r->h != -1)
				c->h = r->h;
			if (!c->trans) {
				if (r->ws >= 0)
					ws = r->ws;
				else if (r->mon) {
					if ((num = strtol(r->mon, NULL, 0)) || !strcmp(r->mon, "0"))
						for (n = 0, m = monitors; m; m = m->next, n++)
							if ((num >= 0 && num == n) || !strcmp(r->mon, m->name)) {
								ws = m->ws->num;
								break;
							}
				}
			}
			break;
		}
		xcb_icccm_get_wm_class_reply_wipe(&prop);
	} else {
		checkerror(0, "failed to get window class", e);
	}
	setclientws(c, c->trans ? c->trans->ws->num : ws);
	DBG("applyrule: done -- workspace: %d, monitor: %s, "
			"floating: %d, sticky: %d, x: %d, y: %d, w: %d, h: %d",
			c->ws->num, c->ws->mon->name, c->floating, c->sticky, c->x, c->y, c->w, c->h);

	return cb;
}

void assignworkspaces(void)
{
	int i, j, n = 0;
	Monitor *m;
	Workspace *ws;

	DBG("assignworkspaces: entering");
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
{
	Client *t = NULL;

	DBG("attach: entering");
	if (!c->ws)
		c->ws = selws;
	if (!tohead)
		FIND_TAIL(t, c->ws->clients);
	if (t) {
		c->next = t->next;
		t->next = c;
	} else {
		c->next = c->ws->clients;
		c->ws->clients = c;
	}
}

void attachpanel(Panel *p)
{
	DBG("attachpanel: entering");
	p->next = panels;
	panels = p;
}

void attachstack(Client *c)
{
	DBG("attachstack: entering");
	c->snext = c->ws->stack;
	c->ws->stack = c;
}

void changews(Workspace *ws, int usermotion)
{
	int diffmon = selws ? selws->mon != ws->mon : 1;

	DBG("changews: entering");
	lastws = selws;
	selws = ws;
	selws->mon->ws = ws;
	PROP_REPLACE(root, netatom[CurDesktop], XCB_ATOM_CARDINAL, 32, 1, &ws->num);
	if (diffmon && !usermotion)
		xcb_warp_pointer(con, root, root, 0, 0, 0, 0,
				ws->mon->x + (ws->mon->w / 2), ws->mon->y + (ws->mon->h / 2));
}

int checkerror(int lvl, char *msg, xcb_generic_error_t *e)
{
	if (!e)
		return 1;
	fprintf(stderr, "yaxwm: %s", msg);
	printerror(e);
	free(e);
	if (lvl)
		exit(lvl);
	return 0;
}

void cmdborder(char **argv)
{
	Client *c;
	Workspace *ws;
	int i, n, opt, f = border[Focus], u = border[Unfocus];

	DBG("cmdborder: entering");
	enum { colreset, colfocus, colunfocus };
	char *colopt[] = { "reset",    "focus", "unfocus", NULL };
	enum { width, colour, color, smart };
	char *bdropt[] = { "width", "colour", "color", "smart", NULL };

	if ((opt = optparse(argv, bdropt, &i, NULL, 0)) < 0)
		return;
	if (opt == smart) {
		if (i != INT_MAX)
			border[Smart] = i;
	} else if (opt == colour || opt == color) {
		if ((opt = optparse(argv + 1, colopt, &i, NULL, 1)) < 0)
			return;
		if (opt == colreset) {
			border[Focus] = dborder[Focus];
			border[Unfocus] = dborder[Unfocus];
		} else if (i <= 0xffffff && i >= 0) {
			if (opt == colfocus) {
				border[Focus] = i;
				if (selws->sel)
					xcb_change_window_attributes(con, selws->sel->win,
							XCB_CW_BORDER_PIXEL, &border[Focus]);
				return;
			} else if (opt == colunfocus) {
				border[Unfocus] = i;
			}
		}
		if (f != border[Focus] || u != border[Unfocus])
			FOR_CLIENTS(c, ws)
				xcb_change_window_attributes(con, c->win, XCB_CW_BORDER_PIXEL,
						&border[c == c->ws->sel ? Focus : Unfocus]);
	} else if (opt == width) {
		opt = optparse(argv + 1, stdopts, &i, NULL, 0);
		if (opt < 0 && i == INT_MAX)
			return;
		else if (opt == stdreset)
			i = dborder[Width];
		else if ((n = adjbordergap(i, opt, border[Width], selws->gappx)) != INT_MAX)
			i = MAX(MIN((int)((selws->mon->wh / 6) - selws->gappx), border[Width] + n), 0);
		if (i != border[Width]) {
			FOR_CLIENTS(c, ws)
				if (c->bw && c->bw == border[Width])
					c->bw = i;
			border[Width] = i;
			layoutws(NULL);
		}
	}
}

void cmdcycle(char **argv)
{
	Client *c;

	if (!(c = selws->sel) || c->floating || (c->fullscreen && !c->ffs))
		return;
	if (!c->ws->layout->fn || (c == nextt(selws->clients) && !nextt(c->next)))
		return;
	if (!(c = nextt(selws->sel->next)))
		c = nextt(selws->clients);
	movestack(-1);
	focus(c);
	(void)(argv);
}

void cmdffs(char **argv)
{
	Client *c;
	Monitor *m;

	if (!(c = selws->sel) || !(m = c->ws->mon))
		return;
	if ((c->ffs = !c->ffs) && c->fullscreen) {
		c->bw = c->old_bw;
		layoutws(c->ws);
	} else if (c->fullscreen) {
		c->bw = 0;
		resize(c, m->x, m->y, m->w, m->h, c->bw);
		layoutws(c->ws);
	}
	(void)(argv);
}

void cmdfloat(char **argv)
{
	Client *c;

	DBG("cmdfloat: entering");
	if (!(c = selws->sel) || (c->fullscreen && !c->ffs))
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
	int i, opt;
	enum { next, prev };
	char *opts[] = { "next", "prev", NULL };

	DBG("cmdfocus: entering");
	if (!selws->sel || (selws->sel->fullscreen && !selws->sel->ffs))
		return;
	opt = optparse(argv, opts, &i, NULL, 0);
	if (opt < 0 && i == INT_MAX) {
		fprintf(cmdresp, "!invalid argument for focus");
		return;
	}
	i = opt == -1 ? i : opt == next ? 1 : -1;
	while (i) {
		movefocus(i);
		i += i > 0 ? -1 : 1;
	}
}

void cmdfollow(int num)
{
	Client *c;
	Workspace *ws;

	DBG("cmdfollow: entering");
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
	int i, n, opt;
	uint ng = selws->gappx;

	DBG("cmdgappx: entering");
	opt = optparse(argv, stdopts, &i, NULL, 0);

	if (opt < 0 && i == INT_MAX)
		return;
	else if (opt == stdreset)
		ng = workspacerules[selws->num].gappx;
	else if ((n = adjbordergap(i, opt, selws->gappx, border[Width])) != INT_MAX)
		ng = MAX(MIN((int)selws->gappx + n, (selws->mon->wh / 6) - border[Width]), 0);
	if (ng != selws->gappx) {
		selws->gappx = ng;
		layoutws(selws);
	}
}

void cmdkill(char **argv)
{
	DBG("cmdkill: entering");
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

	DBG("cmdlayout: entering");
	if (!argv || !*argv)
		return;
	while (*argv) {
		for (i = 0; i < LEN(layouts); i++)
			if (!strcmp(layouts[i].name, *argv)) {
				if (&layouts[i] != selws->layout) {
					selws->layout = &layouts[i];
					if (selws->sel)
						layoutws(selws);
				}
				return;
			}
		argv++;
	}
}

void cmdmouse(char **argv)
{
	DBG("cmdmouse: entering");
	if (!argv || !*argv)
		return;
	while (*argv) {
		if (!strcmp("focus", *argv)) {
			argv++;
			if (!strcmp("1", *argv) || !strcmp("on", *argv) || !strcmp("enable", *argv))
				focusmouse = 1;
			else if (!strcmp("0", *argv) || !strcmp("off", *argv) || !strcmp("disable", *argv))
				focusmouse = 0;
		} else if (!strcmp("mod", *argv)) {
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
	uint j;
	float f, *sf;
	Client *c, *t;
	int i = 0, n, arg, absolute, ohoff, x = 0, y = 0, w = 0, h = 0;

	DBG("cmdmvresize: entering");
	if (!(c = selws->sel) || (c->fullscreen && !c->ffs)
			|| (!c->floating && c->ws->layout->fn != tile))
		return;
	if ((absolute = !strcmp(*argv, "absolute"))) {
		w = c->w, h = c->h, x = c->x, y = c->y;
		argv++;
	}

	if (c->floating) {
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
			} else
				fprintf(cmdresp, "!invalid argument for move/resize on floating window: %s", *argv);
			argv++;
			i++;
		}

		if (absolute)
			resize(c, x, y, MIN(c->ws->mon->ww, MAX(100, w)),
					MIN(c->ws->mon->wh, MAX(100, h)), c->bw);
		else
			resize(c, c->x + x, c->y + y, MIN(c->ws->mon->ww, MAX(100, c->w + w)),
					MIN(c->ws->mon->wh, MAX(100, c->h + h)), c->bw);
		confinetomon(c);
	} else {
		if (!strcmp(*argv, "y")) {
			DBG("cmdmvresize: active window is tiled -- adjusting stack location");
			argv++;
			if ((y = strtol(*argv, NULL, 0)))
				movestack(y > 0 ? 1 : -1);
			else
				fprintf(cmdresp, "!invalid argument for stack location adjustment: %s", *argv);
		} else if (!strcmp(*argv, "w")) {
			DBG("cmdmvresize: active window is tiled -- adjusting width ratio");
			argv++;
			if ((w = strtol(*argv, NULL, 0))) {
				for (j = 0, t = nextt(selws->clients); t; t = nextt(t->next), j++)
					if (t == c) {
						sf = (selws->nmaster && j < selws->nmaster + selws->nstack) ? &c->ws->split
							: &c->ws->ssplit;
						if (absolute)
							f = w / c->ws->mon->ww;
						else
							f = ((c->ws->mon->ww * *sf) + w) / c->ws->mon->ww;
						if (f < 0.1 || f > 0.9) {
							fprintf(cmdresp, "!width adjustment for window exceeded limit: %f",
									c->ws->mon->ww * f);
							return;
						}
						*sf = f;
						layoutws(selws);
						break;
					}
			} else
				fprintf(cmdresp, "!invalid argument for width adjustment: %s", *argv);
		} else if (!strcmp(*argv, "h")) {
			DBG("cmdmvresize: active window is tiled -- adjusting height offset");
			argv++;
			if ((h = strtol(*argv, NULL, 0)) || !strcmp(*argv, "0")) {
				ohoff = c->hoff;
				c->hoff = absolute ? h : c->hoff + h;
				DBG("cmdmvresize: new height offset: %d", c->hoff);
				if (layoutws(selws) == -1) {
					fprintf(cmdresp, "!height adjustment for window exceeded limit: %d", c->hoff);
					c->hoff = ohoff;
				}
			} else
				fprintf(cmdresp, "!invalid argument for height offset adjustment: %s", *argv);
		} else {
			fprintf(cmdresp, "!invalid argument for move/resize on tiled window: %s", *argv);
			return;
		}
	}

	eventignore(XCB_ENTER_NOTIFY);
}

void cmdnmaster(char **argv)
{
	int i, opt;

	DBG("cmdnmaster: entering");
	opt = optparse(argv, minopts, &i, NULL, 0);
	adjnmasterstack(i, opt, 1);
}

void cmdnstack(char **argv)
{
	int i, opt;

	DBG("cmdnstack: entering");
	opt = optparse(argv, minopts, &i, NULL, 0);
	adjnmasterstack(i, opt, 0);
}

void cmdpad(char **argv)
{
	int i = 0, n, arg;
	int pad[4] = { -1, -1, -1, -1 };

	if (!strcmp("print", *argv)) {
		fprintf(cmdresp, "l %d\nr %d\nt %d\nb %d",
				selws->padl, selws->padr, selws->padt, selws->padb);
		return;
	}
	while (*argv && i < 4) {
		n = strtol(*argv, NULL, 0);
		if ((arg = !strcmp("l", *argv)) || n || !strcmp("0", *argv)) {
			argv++;
			pad[0] = arg ? strtol(*argv, NULL, 0) : n;
		} else if ((arg = !strcmp("r", *argv)) || n || !strcmp("0", *argv)) {
			argv++;
			pad[1] = arg ? strtol(*argv, NULL, 0) : n;
		} else if ((arg = !strcmp("t", *argv)) || n || !strcmp("0", *argv)) {
			argv++;
			pad[2] = arg ? strtol(*argv, NULL, 0) : n;
		} else if ((arg = !strcmp("b", *argv)) || n || !strcmp("0", *argv)) {
			argv++;
			pad[3] = arg ? strtol(*argv, NULL, 0) : n;
		} else
			fprintf(cmdresp, "!invalid argument for workspace padding: %s", *argv);
		argv++;
		i++;
	}
	selws->padl = pad[0] >= 0 ? (uint)pad[0] : selws->padl;
	selws->padr = pad[1] >= 0 ? (uint)pad[1] : selws->padr;
	selws->padt = pad[2] >= 0 ? (uint)pad[2] : selws->padt;
	selws->padb = pad[3] >= 0 ? (uint)pad[3] : selws->padb;
	layoutws(selws);
}

void cmdparse(char *buf)
{
	uint i, n = 0, matched = 0;
	char *s, *argv[15], dbuf[BUFSIZ], k[BUFSIZ], tok[BUFSIZ], args[15][BUFSIZ];

	DBG("cmdparse: entering");
	strlcpy(dbuf, buf, sizeof(dbuf));
	s = dbuf;
	if (strqetok(&s, k, sizeof(k))) {
		for (i = 0; i < LEN(keywords); i++)
			if ((matched = !strcmp(keywords[i].name, k))) {
				while (n + 1 < LEN(args) && s && *s && strqetok(&s, tok, sizeof(tok))) {
					strlcpy(args[n], tok, sizeof(args[n]));
					argv[n] = args[n];
					DBG("cmdparse: %s keyword: argv[%d] = %s", k, n, argv[n]);
					n++;
				}
				argv[n] = NULL;
				if (*argv)
					keywords[i].func(argv);
				else
					fprintf(cmdresp, "!keyword requires additional arguments: %s", k);
				break;
			}
		if (!matched)
			fprintf(cmdresp, "!invalid or unknown command keyword: %s", k);
	}
	fflush(cmdresp);
	fclose(cmdresp);
}

void cmdrule(char **argv)
{
	uint ui;
	int i, rem;
	Workspace *ws;
	WindowRule *wr, r;

	DBG("cmdrule: entering");
	r.cb = NULL;
	r.sticky = 0, r.floating = 0;
	r.x = -1, r.y = -1, r.w = -1, r.h = -1, r.ws = -1;
	r.class = NULL, r.inst = NULL, r.title = NULL, r.mon = NULL;

	if ((rem = !strcmp("remove", *argv))) {
		argv++;
		if (!strcmp("all", *argv)) {
			while (rules)
				freerule(rules);
			return;
		}
	}
	while (*argv) {
		if (!r.class && !strcmp(*argv, "class")) {
			argv++;
			r.class = *argv;
		} else if (!r.inst && !strcmp(*argv, "instance")) {
			argv++;
			r.inst = *argv;
		} else if (!r.title && !strcmp(*argv, "title")) {
			argv++;
			r.title = *argv;
		} else if (!strcmp(*argv, "mon")) {
			argv++;
			if (*argv)
				r.mon = *argv;
		} else if (!strcmp(*argv, "ws")) {
			argv++;
			i = strtol(*argv, NULL, 0);
			if ((i < numws && i > 0) || !strcmp(*argv, "0"))
				r.ws = i;
			else FOR_EACH(ws, workspaces)
				if (!strcmp(ws->name, *argv)) {
					r.ws = ws->num;
					break;
				}
		} else if (!strcmp(*argv, "callback")) {
			argv++;
			for (ui = 0; ui < LEN(callbacks); ui++)
				if (!strcmp(callbacks[ui].name, *argv)) {
					r.cb = &callbacks[ui];
					break;
				}
		} else if (!strcmp(*argv, "x")) {
			argv++;
			if ((i = strtol(*argv, NULL, 0)) || !strcmp(*argv, "0"))
				r.x = i;
		} else if (!strcmp(*argv, "y")) {
			argv++;
			if ((i = strtol(*argv, NULL, 0)) || !strcmp(*argv, "0"))
				r.y = i;
		} else if (!strcmp(*argv, "w")) {
			argv++;
			if ((i = strtol(*argv, NULL, 0)) >= 50)
				r.w = i;
		} else if (!strcmp(*argv, "h")) {
			argv++;
			if ((i = strtol(*argv, NULL, 0)) >= 50)
				r.h = i;
		} else if (!strcmp(*argv, "floating")) {
			r.floating = 1;
		} else if (!strcmp(*argv, "sticky")) {
			r.sticky = 1;
		} else {
			fprintf(cmdresp, "!invalid argument for rule command: %s", *argv);
		}
		argv++;
	}

	if ((r.class || r.inst || r.title) && (r.cb || r.mon || r.ws != -1 || r.floating
				|| r.sticky || r.x != -1 || r.y != -1 || r.w != -1 || r.h != -1))
	{
		FOR_EACH(wr, rules) {
			if ((r.class && (r.class != wr->class || strcmp(r.class, wr->class)))
					|| (r.inst && (r.inst != wr->inst || strcmp(r.inst, wr->inst)))
					|| (r.title && (r.title != wr->title || strcmp(r.title, wr->title))))
				continue;
			if (rem && r.mon == wr->mon && r.ws == wr->ws && r.floating == wr->floating
					&& r.sticky == wr->sticky && r.x == wr->x && r.y == wr->y
					&& r.w == wr->w && r.h == wr->h && r.cb == wr->cb)
				break;
			DBG("cmdrule: updating existing rule with the same match patterns");
			wr->ws = r.ws;
			wr->mon = r.mon;
			wr->sticky = r.sticky;
			wr->floating = r.floating;
			wr->x = r.x, wr->y = r.y, wr->w = r.w, wr->h = r.h;
			return;
		}
		if (rem) {
			if (wr)
				freerule(wr);
			else
				fprintf(cmdresp, "!no matching rule allocated");
		} else
			initrule(&r);
	}
}

void cmdsend(int num)
{
	Client *c;

	DBG("cmdsend: entering");
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

	DBG("cmdset: entering");
	if (!(s = argv[0]))
		return;
	r = argv + 1;
	for (i = 0; i < LEN(setcmds); i++)
		if (!strcmp(setcmds[i].name, s)) {
			setcmds[i].func(r);
			return;
		}
	fprintf(cmdresp, "!invalid set keyword: %s", s);
}

void cmdsplit(char **argv)
{
	int opt;
	float f, nf, *sf;

	DBG("cmdsplit: entering");
	if (!selws->layout->fn)
		return;
	if (!strcmp("stack", *argv)) {
		sf = &selws->ssplit;
		argv++;
	} else {
		sf = &selws->split;
	}
	opt = optparse(argv, minopts, NULL, &f, 0);
	if (f == 0.0 || (opt != -1 && (f > 0.9 || f < 0.1 || !(f -= *sf))))
		return;
	if ((nf = f < 1.0 ? f + *sf : f - 1.0) < 0.1 || nf > 0.9)
		return;
	*sf = nf;
	if (selws->sel)
		layoutws(selws);
}

void cmdstick(char **argv)
{
	Client *c;

	DBG("cmdstick: entering");
	if (!(c = selws->sel) || (c->fullscreen && !c->ffs))
		return;
	setsticky(c, !c->sticky);
	(void)(argv);
}

void cmdswap(char **argv)
{
	Client *c;

	DBG("cmdswap: entering");
	if (!(c = selws->sel) || (c->fullscreen && !c->ffs) || c->floating || !selws->layout->fn)
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

	DBG("cmdwin: entering");
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
	enum { wmrld,  wmrst,  wmext };
	char *opts[] = {
		[wmrld] = "reload", [wmrst] = "restart", [wmext] = "exit", NULL
	};

	DBG("cmdwm: entering");
	if ((opt = optparse(argv, opts, NULL, NULL, 0)) != -1) {
		if (opt == wmrld)
			execcfg();
		else if (opt == wmrst || opt == wmext) {
			running = 0;
			restart = opt == wmrst;
		}
	}
}

void cmdws(char **argv)
{
	uint j;
	Workspace *ws;
	int i = -1, n;
	void (*fn)(int) = cmdview;

	DBG("cmdws: entering");
	if (!argv || !*argv) {
		fprintf(cmdresp, "!ws command requires additional arguments but none were given");
		return;
	}
	if (!strcmp("print", *argv)) {
		FOR_EACH(ws, workspaces)
			fprintf(cmdresp, "%d%s%s", ws->num + 1, ws == selws ? " *" : "", ws->next ? "\n" : "");	
		return;
	}
	while (*argv) {
		if ((n = strtol(*argv, NULL, 0))) {
			if (n >= 1 && n <= numws)
				i = n - 1;
			else {
				fprintf(cmdresp, "!workspace index out of range: %d", n);
				return;
			}
		} else for (j = 0; j < LEN(wscommands); j++)
			if (wscommands[j].fn && !strcmp(wscommands[j].name, *argv))
				fn = wscommands[j].fn;
		argv++;
	}
	if (i >= 0)
		fn(i);
}

void cmdview(int num)
{
	Workspace *ws;

	DBG("cmdview: entering");
	if (num == selws->num || !(ws = itows(num)))
		return;
	changews(ws, 0);
	focus(NULL);
	layoutws(NULL);
	restack(selws);
}

void clientcfgreq(Client *c, xcb_configure_request_event_t *e)
{
	Monitor *m;

	DBG("clientcfgreq: entering");
	if (e->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH)
		c->bw = e->border_width;
	else if (c->floating || !c->ws->layout->fn) {
		m = c->ws->mon;
		if (e->value_mask & XCB_CONFIG_WINDOW_X) {
			DBG("clientcfgreq: XCB_CONFIG_WINDOW_X: %d -> %d", c->x, m->x + e->x - c->bw);
			c->old_x = c->x;
			c->x = m->x + e->x - c->bw;
		}
		if (e->value_mask & XCB_CONFIG_WINDOW_Y) {
			DBG("clientcfgreq: XCB_CONFIG_WINDOW_Y: %d -> %d", c->y, m->y + e->y - c->bw);
			c->old_y = c->y;
			c->y = m->y + e->y - c->bw;
		}
		if (e->value_mask & XCB_CONFIG_WINDOW_WIDTH) {
			DBG("clientcfgreq: XCB_CONFIG_WINDOW_WIDTH: %d -> %d", c->w, e->width);
			c->old_w = c->w;
			c->w = e->width;
		}
		if (e->value_mask & XCB_CONFIG_WINDOW_HEIGHT) {
			DBG("clientcfgreq: XCB_CONFIG_WINDOW_HEIGHT: %d -> %d", c->h, e->height);
			c->old_h = c->h;
			c->h = e->height;
		}
		if ((c->x + c->w) > m->wx + m->ww && c->floating)
			c->x = m->wx + (m->ww / 2 - W(c) / 2);
		if ((c->y + c->h) > m->wy + m->wh && c->floating)
			c->y = m->wy + (m->wh / 2 - H(c) / 2);
		if ((e->value_mask & XYMASK) && !(e->value_mask & WHMASK))
			sendconfigure(c);
		if (c->ws == c->ws->mon->ws || (c->sticky && c->ws->mon == selws->mon))
			xcb_configure_window(con, c->win, XYMASK|WHMASK, (uint []){c->x, c->y, c->w, c->h});
		if (c->floating)
			setstackmode(c->win, XCB_STACK_MODE_ABOVE);
	} else {
		sendconfigure(c);
	}
}

Monitor *coordtomon(int x, int y)
{
	Monitor *m;

	FOR_EACH(m, monitors)
		if (x < m->x || x > m->x + m->w || y < m->y || y > m->y + m->h)
			return m;
	return selws->mon;
}

void confinetomon(Client *c)
{
	Monitor *m = c->ws->mon;

	DBG("confinetomon: entering");
	if (c->w > m->ww)
		c->w = m->ww - (2 * c->bw);
	if (c->h > m->wh)
		c->h = m->wh - (2 * c->bw);
	if (c->x + W(c) > m->wx + m->ww)
		c->x = c->ws->mon->wx + c->ws->mon->ww - W(c);
	if (c->y + H(c) > m->wy + m->wh)
		c->y = c->ws->mon->wy + c->ws->mon->wh - H(c);
	c->x = MAX(c->x, c->ws->mon->wx);
	c->y = MAX(c->y, c->ws->mon->wy);
}

void detach(Client *c, int reattach)
{
	Client **tc = &c->ws->clients;

	DBG("detach: entering");
	while (*tc && *tc != c)
		tc = &(*tc)->next;
	*tc = c->next;
	if (reattach)
		attach(c, 1);
}

void detachstack(Client *c)
{
	Client **tc = &c->ws->stack;

	DBG("detachstack: entering");
	while (*tc && *tc != c)
		tc = &(*tc)->snext;
	*tc = c->snext;
	if (c == c->ws->sel)
		c->ws->sel = c->ws->stack;
}

void *ecalloc(size_t elems, size_t size)
{
	void *p;

	DBG("ecalloc: entering");
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

		DBG("eventhandle: FOCUS_IN");
		if (e->mode == XCB_NOTIFY_MODE_GRAB
				|| e->mode == XCB_NOTIFY_MODE_UNGRAB
				|| e->detail == XCB_NOTIFY_DETAIL_POINTER
				|| e->detail == XCB_NOTIFY_DETAIL_POINTER_ROOT
				|| e->detail == XCB_NOTIFY_DETAIL_NONE)
			return;
		if (selws->sel && e->event != selws->sel->win)
			takefocus(selws->sel);
		return;
	}
	case XCB_CONFIGURE_NOTIFY:
	{
		xcb_configure_notify_event_t *e = (xcb_configure_notify_event_t *)ev;

		if (e->window == root && (scr_h != e->height || scr_w != e->width)) {
			DBG("eventhandle: CONFIGURE_NOTIFY -- screen size changed");
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
			DBG("eventhandle: CONFIGURE_REQUEST -- managed window: 0x%x", e->window);
			clientcfgreq(c, e);
		} else {
			DBG("eventhandle: CONFIGURE_REQUEST -- unmanaged window: 0x%x", e->window);
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
		xcb_aux_sync(con);
		return;
	}
	case XCB_DESTROY_NOTIFY:
	{
		xcb_destroy_notify_event_t *e = (xcb_destroy_notify_event_t *)ev;

		if ((c = wintoclient(e->window))) {
			DBG("eventhandle: DESTROY_NOTIFY -- client window");
			freeclient(c, 1);
		} else if ((p = wintopanel(e->window))) {
			DBG("eventhandle: DESTROY_NOTIFY -- panel window");
			freepanel(p, 1);
		}
		return;
	}
	case XCB_ENTER_NOTIFY:
	{
		xcb_enter_notify_event_t *e = (xcb_enter_notify_event_t *)ev;

		if ((e->mode != XCB_NOTIFY_MODE_NORMAL || e->detail == XCB_NOTIFY_DETAIL_INFERIOR)
				&& e->event != root)
			return;
		DBG("eventhandle: ENTER_NOTIFY");
		c = wintoclient(e->event);
		ws = c ? c->ws : wintows(e->event);
		if (ws != selws) {
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
		DBG("eventhandle: BUTTON_PRESS -- client window");
		focus(c);
		restack(c->ws);
		xcb_allow_events(con, XCB_ALLOW_REPLAY_POINTER, e->time);
		if (CLNMOD(e->state) == CLNMOD(mousemod))
			if (e->detail == mousemove || e->detail == mouseresize)
				mousemvr(e->detail == mousemove);
		return;
	}
	case XCB_MOTION_NOTIFY:
	{
		xcb_motion_notify_event_t *e = (xcb_motion_notify_event_t *)ev;

		if (e->event != root || (e->time - lasttime) < (1000 / 60))
			return;
		lasttime = e->time;
		if ((m = coordtomon(e->root_x, e->root_y)) != selws->mon) {
			DBG("eventhandle: MOTION_NOTIFY -- root window -- updating active monitor");
			unfocus(selws->sel, 1);
			changews(m->ws, 1);
			focus(NULL);
		}
		return;
	}
	case XCB_MAP_REQUEST:
	{
		xcb_atom_t type;
		xcb_get_geometry_reply_t *g;
		xcb_get_window_attributes_reply_t *wa;
		xcb_map_request_event_t *e = (xcb_map_request_event_t *)ev;

		if (!(g = wingeom(e->window)) || !(wa = winattr(e->window)))
			return;
		if ((c = wintoclient(e->window)) || (p = wintopanel(e->window)))
			return;
		DBG("eventhandle: MAP_REQUEST -- unmanaged window");
		if ((type = winprop(e->window, netatom[WindowType])) == netatom[Dock])
			initpanel(e->window, g);
		else if (!wa->override_redirect)
			initclient(e->window, XCB_WINDOW_NONE, g, type);
		free(wa);
		free(g);
		return;
	}
	case XCB_UNMAP_NOTIFY:
	{
		xcb_unmap_notify_event_t *e = (xcb_unmap_notify_event_t *)ev;

		DBG("eventhandle: UNMAP_NOTIFY");
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

		if (e->type == netatom[CurDesktop]) {
			DBG("CLIENT_MESSAGE: %s -- data: %d", netatoms[CurDesktop], d[0]);
			cmdview(d[0]);
		} else if ((c = wintoclient(e->window))) {
			if (e->type == netatom[WmDesktop]) {
				DBG("CLIENT_MESSAGE: %s -- window: 0x%08x - data: %d - sticky: %d",
						netatoms[WmDesktop], c->win, d[0], d[0] == 0xffffffff);
				if (d[0] == 0xffffffff) {
					setsticky(c, 1);
					layoutws(NULL);
				} else if (d[0] < (uint)numws && d[0] != (uint)c->ws->num) {
					if (c == selws->sel) {
						unfocus(c, 1);
						focus(NULL);
					}
					setclientws(c, d[0]);
					layoutws(NULL);
				}
			} else if (e->type == netatom[State] && (d[1] == fs || d[2] == fs)) {
				DBG("CLIENT_MESSAGE %s -- window: 0x%08x - data: %d", netatoms[Fullscreen],
						c->win, d[0]);
				setfullscreen(c, (d[0] == 1 || (d[0] == 2 && !c->fullscreen)));
			} else if (e->type == netatom[ActiveWindow] && c != selws->sel) {
				DBG("CLIENT_MESSAGE: %s -- window: 0x%08x", netatoms[ActiveWindow], c->win);
				if (c->ws == selws) {
					focus(c);
					restack(selws);
					if (c->urgent)
						seturgency(c, 0);
				} else if (!c->urgent)
					seturgency(c, 1);
			}
		}
		return;
	}
	case XCB_PROPERTY_NOTIFY:
	{
		xcb_window_t trans;
		xcb_property_notify_event_t *e = (xcb_property_notify_event_t *)ev;

		if (e->atom == netatom[StrutPartial] && (p = wintopanel(e->window))) {
			DBG("eventhandle: PROPERTY_NOTIFY - _NET_WM_STRUT_PARTIAL");
			updatestruts(p, 1);
			layoutws(NULL);
		} else if (e->state != XCB_PROPERTY_DELETE && (c = wintoclient(e->window))) {
			switch (e->atom) {
				case XCB_ATOM_WM_TRANSIENT_FOR:
					DBG("eventhandle: PROPERTY_NOTIFY - WM_TRANSIENT_FOR");
					if (!c->floating && (trans = wintrans(c->win))
							&& (c->floating = (c->trans = wintoclient(trans)) != NULL))
						layoutws(c->ws);
					break;
				case XCB_ATOM_WM_NORMAL_HINTS:
					DBG("eventhandle: PROPERTY_NOTIFY - WM_NORMAL_HINTS");
					sizehints(c, 0);
					break;
				case XCB_ATOM_WM_HINTS:
					DBG("eventhandle: PROPERTY_NOTIFY - WM_HINTS");
					winhints(c);
					break;
			}
			if (e->atom == netatom[WindowType]) {
				DBG("eventhandle: PROPERTY_NOTIFY - _NET_WM_WINDOW_TYPE");
				wintype(c);
			}
		}
		return;
	}
	default:
	{
		if (randrbase != -1 && ev->response_type == randrbase + XCB_RANDR_SCREEN_CHANGE_NOTIFY) {
			DBG("eventhandle: RANDR_SCREEN_CHANGE_NOTIFY");
			if (updaterandr() > 0 && monitors) {
				DBG("eventhandle: outputs changed after screen change event");
				fixupworkspaces();
			}
		} else {
			xcb_generic_error_t *e = (xcb_generic_error_t *)ev;
			if (!e || e->response_type)
				return;
			fprintf(stderr, "yaxwm: eventhandle");
			printerror(e);
		}
		return;
	}
	}
}

void eventignore(uint8_t type)
{
	xcb_generic_event_t *ev;

	DBG("eventignore: entering");
	xcb_flush(con);
	while (running && (ev = xcb_poll_for_event(con))) {
		if (XCB_EVENT_RESPONSE_TYPE(ev) != type)
			eventhandle(ev);
		free(ev);
	}
}

void eventloop(void)
{
	Client *c;
	ssize_t n;
	Workspace *ws;
	fd_set read_fds;
	char buf[PIPE_BUF];
	int confd, nfds, cmdfd;
	xcb_generic_event_t *ev;
	static struct sockaddr_un sockaddr;

	DBG("eventloop: entering");
	if (sockfd == -1) {
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
	} else {
		DBG("eventloop: using existing socket file descriptor: %d", sockfd);
	}

	execcfg();
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
			if (wrpending) {
				if (rules)
					FOR_CLIENTS(c, ws)
						applyrule(c);
				wrpending--;
			}
		}
	}
}

void execcfg(void)
{
	char *cfg, *home;
	char path[PATH_MAX];

	DBG("execcfg: entering");
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
{
	Panel *p;
	Client *c;
	Workspace *ws;

	DBG("fixupworkspaces: entering");
	assignworkspaces();
	FOR_CLIENTS(c, ws)
		if (c->fullscreen && (!c->ffs || (c->w == ws->mon->w && c->h == ws->mon->h)))
			resize(c, ws->mon->x, ws->mon->y, ws->mon->w, ws->mon->h, c->bw);
	if (panels)
		FOR_EACH(p, panels)
			updatestruts(p, 1);
	focus(NULL);
	layoutws(NULL);
	restack(selws);
}

void focus(Client *c)
{
	DBG("focus: entering");
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
		xcb_change_window_attributes(con, c->win, XCB_CW_BORDER_PIXEL, &border[Focus]);
		takefocus(c);
	} else {
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
		xcb_delete_property(con, root, netatom[ActiveWindow]);
	}
	selws->sel = c;
}

void freeclient(Client *c, int destroyed)
{
	Workspace *ws;

	if (!c)
		return;
	DBG("freeclient: entering -- freeing %sdestroyed window: 0x%08x",
			destroyed ? "" : "non-", c->win);
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
	if (running)
		xcb_delete_property(con, c->win, netatom[WmDesktop]);
	free(c);
	focus(NULL);
	DBG("freeclient: updating _NET_CLIENT_LIST");
	xcb_delete_property(con, root, netatom[ClientList]);
	FOR_CLIENTS(c, ws)
		PROP_APPEND(root, netatom[ClientList], XCB_ATOM_WINDOW, 32, 1, &c->win);
	layoutws(NULL);
}

void freerule(WindowRule *r)
{
	WindowRule **cr = &rules;

	DBG("freerule: entering -- freeing window rule");
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
{
	Monitor *mon;

	DBG("freemon: entering -- freeing monitor %s, id %d", m->name, m->id);
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

	DBG("freepanel: entering");
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
{
	uint i;
	char fdstr[20];
	Workspace *ws;

	DBG("freewm: entering");
	FOR_EACH(ws, workspaces)
		while (ws->stack)
			freeclient(ws->stack, 0);
	xcb_key_symbols_free(keysyms);
	while (panels)
		freepanel(panels, 0);
	while (workspaces)
		freews(workspaces);
	while (monitors)
		freemon(monitors);
	while (rules)
		freerule(rules);
	for (i = 0; i < LEN(cursors); i++)
		xcb_free_cursor(con, cursor[i]);
	xcb_destroy_window(con, wmcheck);
	xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT,
			XCB_INPUT_FOCUS_POINTER_ROOT, XCB_CURRENT_TIME);
	xcb_delete_property(con, root, netatom[ActiveWindow]);
	xcb_flush(con);
	xcb_disconnect(con);

	if (restart) {
		DBG("freewm: restarting")
		if (!itoa(sockfd, fdstr))
			itoa(-1, fdstr);
		char *const arg[] = { argv0, "-s", fdstr, NULL };
		execvp(arg[0], arg);
	}
	close(sockfd);
	unlink(sock);
}

void freews(Workspace *ws)
{
	Workspace *sel;

	DBG("freews: entering");
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

	DBG("grabbuttons: entering");
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
{
	int r = 0;
	xcb_generic_error_t *e;
	xcb_grab_pointer_cookie_t pc;
	xcb_grab_pointer_reply_t *ptr = NULL;

	DBG("grabpointer: entering");
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

	DBG("gravitate: entering");
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
{
	int i;
	xcb_generic_error_t *e;
	xcb_intern_atom_reply_t *r;
	xcb_intern_atom_cookie_t c[num];

	DBG("initatoms: entering");
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

void initclient(xcb_window_t win, xcb_window_t trans, xcb_get_geometry_reply_t *g, xcb_atom_t type)
{
	Client *c = NULL;
	Callback *cb = NULL;

	DBG("initclient: entering");
	if (type == netatom[Splash] || type == netatom[Desktop]) {
		DBG("initclient: mapping %s window", type == netatom[Splash] ? "splash" : "desktop");
		xcb_map_window(con, win);
		return;
	}
	c = ecalloc(1, sizeof(Client));
	c->win = win;
	c->x = c->old_x = g->x, c->y = c->old_y = g->y;
	c->w = c->old_w = g->width, c->h = c->old_h = g->height;
	c->old_bw = g->border_width;
	if ((trans != XCB_WINDOW_NONE || (trans = wintrans(c->win)) != XCB_WINDOW_NONE))
		c->trans = wintoclient(trans);
	cb = applyrule(c);
	confinetomon(c);
	if (c->trans) {
		c->x = c->trans->ws->mon->wx + c->trans->x + ((W(c->trans) - W(c)) / 2);
		c->y = c->trans->ws->mon->wy + c->trans->y + ((H(c->trans) - H(c)) / 2);
	}
	c->bw = border[Width];
	xcb_configure_window(con, c->win, BWMASK, &c->bw);
	sendconfigure(c);
	wintype(c);
	sizehints(c, 1);
	winhints(c);
	xcb_change_window_attributes(con, c->win, XCB_CW_EVENT_MASK | XCB_CW_BORDER_PIXEL,
			(uint []){ border[Unfocus], XCB_EVENT_MASK_ENTER_WINDOW
			| XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_PROPERTY_CHANGE
			| XCB_EVENT_MASK_STRUCTURE_NOTIFY });
	grabbuttons(c, 0);
	if (c->floating || (c->floating = c->oldstate = trans != XCB_WINDOW_NONE || c->fixed)) {
		if (c->x + c->y == 0)
			gravitate(c, Center, Center, 0);
		setstackmode(c->win, XCB_STACK_MODE_ABOVE);
	}
	PROP_APPEND(root, netatom[ClientList], XCB_ATOM_WINDOW, 32, 1, &c->win);
	MOVE(c->win, c->x + 2 * scr_w, c->y);
	setwinstate(c->win, XCB_ICCCM_WM_STATE_NORMAL);
	if (c->ws == c->ws->mon->ws || c->sticky) {
		if (c->ws == selws)
			unfocus(selws->sel, 0);
		c->ws->sel = c;
		layoutws(c->ws);
	} else {
		MOVE(c->win, H(c) * -2, c->y);
	}
	xcb_map_window(con, win);
	focus(NULL);
	if (cb)
		cb->fn(c);
	DBG("initclient: done: 0x%08x - workspace %d: %d,%d @ %dx%d - floating: %d - nofocus: %d",
			c->win, c->ws->num, c->x, c->y, c->w, c->h, c->floating, c->nofocus);
}

void initpanel(xcb_window_t win, xcb_get_geometry_reply_t *g)
{
	int *s;
	Panel *p;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t rc;
	xcb_get_property_reply_t *r = NULL;
	uint m = XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY;

	DBG("initpanel: entering");
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
		DBG("initpanel: struts: %d, %d, %d, %d", s[0], s[1], s[2], s[3]);
		p->strut_l = s[0], p->strut_r = s[1], p->strut_t = s[2], p->strut_b = s[3];
		updatestruts(p, 1);
	}
	free(r);
	attachpanel(p);
	xcb_change_window_attributes(con, p->win, XCB_CW_EVENT_MASK, &m);
	setwinstate(p->win, XCB_ICCCM_WM_STATE_NORMAL);
	xcb_map_window(con, p->win);
	layoutws(NULL);
	DBG("initpanel: panel mapped - mon: %s - geom: %d,%d @ %dx%d",
			p->mon->name, p->x, p->y, p->w, p->h);
}

Monitor *initmon(char *name, xcb_randr_output_t id, int x, int y, int w, int h)
{
	Monitor *m;
	uint len = strlen(name) + 1;

	DBG("initmon: entering -- initializing new monitor: %s - %d,%d - %dx%d", name, x, y, w, h);
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

	DBG("initrandr: entering");
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

void initrule(WindowRule *r)
{
	size_t len;
	WindowRule *wr;

	DBG("initrule: entering: class: %s - inst: %s - title: %s - mon: %s - ws: %d "
			"- floating: %d - sticky: %d - position: %d,%d - size: %dx%d",
			r->class, r->inst, r->title, r->mon, r->ws, r->floating,
			r->sticky, r->x, r->y, r->w, r->h);
	wr = ecalloc(1, sizeof(WindowRule));
	wr->class = NULL, wr->inst = NULL, wr->title = NULL, wr->mon = NULL;
	wr->ws = r->ws;
	wr->floating = r->floating;
	wr->sticky = r->sticky;
	wr->cb = r->cb;
	wr->x = r->x, wr->y = r->y, wr->w = r->w, wr->h = r->h;
	if (r->mon) {
		len = strlen(r->mon) + 1;
		wr->mon = ecalloc(1, len);
		strlcpy(wr->mon, r->mon, len);
	}
	if (initrulereg(wr, r)) {
		wr->next = rules;
		rules = wr;
		DBG("initrule: complete: class: %s - inst: %s - title: %s - mon: %s - ws: %d "
				"- floating: %d - sticky: %d - position: %d,%d - size: %dx%d",
				wr->class, wr->inst, wr->title, wr->mon, wr->ws, wr->floating,
				wr->sticky, wr->x, wr->y, wr->w, wr->h);
	} else {
		free(wr->mon);
		free(wr);
	}
}

int initrulereg(WindowRule *r, WindowRule *wr)
{
	int i;
	size_t len;
	char buf[NAME_MAX], *e;

	DBG("initrulereg: entering: class: %s - inst: %s - title: %s - mon: %s - ws: %d "
			"- floating: %d - sticky: %d - position: %d,%d - size: %dx%d",
			wr->class, wr->inst, wr->title, wr->mon, wr->ws, wr->floating,
			wr->sticky, wr->x, wr->y, wr->w, wr->h);
	if (wr->class) {
		len = strlen(wr->class) + 1;
		r->class = ecalloc(1, len);
		strlcpy(r->class, wr->class, len);
	}
	if (wr->inst) {
		len = strlen(wr->inst) + 1;
		r->inst = ecalloc(1, len);
		strlcpy(r->inst, wr->inst, len);
	}
	if (wr->title) {
		len = strlen(wr->title) + 1;
		r->title = ecalloc(1, len);
		strlcpy(r->title, wr->title, len);
	}
	if (r->class && (i = regcomp(&(r->classreg), r->class, REG_NOSUB | REG_EXTENDED | REG_ICASE))) {
		regerror(i, &(r->classreg), buf, sizeof(buf));
		e = "class";
		goto error;
	} else if (r->inst
			&& (i = regcomp(&(r->instreg), r->inst, REG_NOSUB | REG_EXTENDED | REG_ICASE)))
	{
		regerror(i, &(r->instreg), buf, sizeof(buf));
		e = "instance";
		goto error;
	} else if (r->title
			&& (i = regcomp(&(r->titlereg), r->title, REG_NOSUB | REG_EXTENDED | REG_ICASE)))
	{
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

void initscan(void)
{
	uint i;
	xcb_atom_t *s, type;
	xcb_window_t *w, *t;
	xcb_generic_error_t *e;
	xcb_query_tree_reply_t *rt;
	xcb_get_geometry_reply_t **g;
	xcb_get_window_attributes_reply_t **wa;
	uint8_t v = XCB_MAP_STATE_VIEWABLE, ic = XCB_ICCCM_WM_STATE_ICONIC;

	DBG("initscan: entering");
	if ((rt = xcb_query_tree_reply(con, xcb_query_tree(con, root), &e))) {
		w = xcb_query_tree_children(rt);
		s = ecalloc(rt->children_len, sizeof(xcb_atom_t));
		t = ecalloc(rt->children_len, sizeof(xcb_window_t));
		g = ecalloc(rt->children_len, sizeof(xcb_get_geometry_reply_t *));
		wa = ecalloc(rt->children_len, sizeof(xcb_get_window_attributes_reply_t *));
		for (i = 0; i < rt->children_len; i++) {
			g[i] = NULL;
			t[i] = s[i] = XCB_WINDOW_NONE;
			if (!(wa[i] = winattr(w[i])) || !(g[i] = wingeom(w[i]))) {
				w[i] = XCB_WINDOW_NONE;
			} else if (!(wa[i]->map_state == v || winprop(w[i], wmatom[WMState]) == ic)) {
				w[i] = 0;
			} else if (!(t[i] = wintrans(w[i]))) {
				if ((type = winprop(w[i], netatom[WindowType])) == netatom[Dock])
					initpanel(w[i], g[i]);
				else if (!wa[i]->override_redirect)
					initclient(w[i], t[i], g[i], type);
				w[i] = 0;
			}
		}
		for (i = 0; i < rt->children_len; i++) {
			if (w[i] && t[i]) {
				if ((type = winprop(w[i], netatom[WindowType])) == netatom[Dock])
					initpanel(w[i], g[i]);
				else if (!wa[i]->override_redirect)
					initclient(w[i], t[i], g[i], type);
			}
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
{
	uint i, j;
	int r, cws;
	Workspace *ws;
	size_t len = 1;
	xcb_void_cookie_t c;
	xcb_cursor_context_t *ctx;

	DBG("initwm: entering");
	if ((randrbase = initrandr()) < 0 || !monitors)
		monitors = initmon("default", 0, 0, 0, scr_w, scr_h);
	if (!primary)
		primary = monitors;
	xcb_warp_pointer(con, root, root, 0, 0, 0, 0,
			primary->x + (primary->w / 2), primary->y + (primary->h / 2));

	for (numws = 0; numws < (int)LEN(workspacerules); numws++) {
		FIND_TAIL(ws, workspaces);
		if (ws)
			ws->next = initws(numws, &workspacerules[numws]);
		else
			workspaces = initws(numws, &workspacerules[numws]);
	}
	assignworkspaces();

	for (i = 0; i < LEN(dborder); i++)
		dborder[i] = border[i];

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

Workspace *initws(int num, WorkspaceRule *r)
{
	Workspace *ws;

	DBG("initws: entering -- initializing new workspace: '%s': %d", r->name, num);
	ws = ecalloc(1, sizeof(Workspace));
	ws->num = num;
	ws->name = r->name;
	ws->nmaster = r->nmaster;
	ws->nstack = r->nstack;
	ws->gappx = r->gappx;
	ws->split = r->split;
	ws->ssplit = r->ssplit;
	ws->layout = r->layout;
	ws->padl = r->padl;
	ws->padr = r->padr;
	ws->padt = r->padt;
	ws->padb = r->padb;
	return ws;
}

char *itoa(int n, char *s)
{
	char c;
	int j, i = 0, sign = n;

	DBG("itoa: entering");
	if (sign < 0)
		n = -n;
	do {
		s[i++] = n % 10 + '0';
	} while ((n /= 10) > 0);
	if (sign < 0)
		s[i++] = '-';
	s[i] = '\0';
	for (j = i - 1, i = 0; i < j; i++, j--) {
		c = s[i];
		s[i] = s[j];
		s[j] = c;
	}
	return s;
}

Workspace *itows(int num)
{
	Workspace *ws;

	DBG("itows: entering");
	for (ws = workspaces; ws && (int)ws->num != num; ws = ws->next)
		;
	return ws;
}

int layoutws(Workspace *ws)
{
	int ret = 1;

	DBG("layoutws: entering");
	if (ws)
		showhide(ws->stack);
	else FOR_EACH(ws, workspaces)
		showhide(ws->stack);
	if (ws) {
		if (ws->layout->fn)
			ret = ws->layout->fn(ws);
		restack(ws);
	} else FOR_EACH(ws, workspaces)
		if (ws == ws->mon->ws && ws->layout->fn) {
			if (ws == selws)
				ret = ws->layout->fn(ws);
			else
				ws->layout->fn(ws);
		}
	return ret;
}

int mono(Workspace *ws)
{
	Client *c;
	int gap = 0, bw = 0;
	uint wx = ws->mon->wx + ws->padl;
	uint wy = ws->mon->wy + ws->padt;
	uint ww = ws->mon->ww - ws->padl - ws->padr;
	uint wh = ws->mon->wy - ws->padt - ws->padb;

	DBG("mono: entering");
	if (!border[Smart])
		bw = border[Width], gap = ws->gappx;
	for (c = nextt(ws->clients); c; c = nextt(c->next))
		resize(c, wx + gap, wy + gap, ww - gap, wh - gap, bw);
	return 1;
}

void movefocus(int direction)
{
	Client *c;

	DBG("movefocus: entering");
	if (!selws->sel || (selws->sel->fullscreen && !selws->sel->ffs))
		return;
	if (direction > 0)
		c = selws->sel->next ? selws->sel->next : selws->clients;
	else
		FIND_PREV(c, selws->sel, selws->clients);
	if (c) {
		focus(c);
		restack(c->ws);
	}
}

void movestack(int direction)
{
	int i = 0;
	Client *c;

	DBG("movestack: entering");
	if (!selws->sel || selws->sel->floating || !nextt(selws->clients->next))
		return;
	if (direction > 0) {
		detach(selws->sel, (c = nextt(selws->sel->next)) ? 0 : 1);
		if (c) {
			selws->sel->next = c->next;
			c->next = selws->sel;
		}
	} else {
		if (selws->sel == nextt(selws->clients)) {
			detach(selws->sel, 0);
			attach(selws->sel, 0);
		} else {
			FIND_PREVTILED(c, selws->sel, selws->clients);
			detach(selws->sel, (i = (c == nextt(selws->clients)) ? 1 : 0));
			if (!i) {
				selws->sel->next = c;
				FIND_PREV(c, selws->sel->next, selws->clients);
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

	DBG("mousemvr: entering");
	if (!(c = selws->sel) || (c->fullscreen && !c->ffs) || !querypointer(&mx, &my))
		return;
	ox = nx = c->x, oy = ny = c->y, ow = nw = c->w, oh = nh = c->h;
	if (!grabpointer(cursor[move ? Move : Resize]))
		return;
	while (running && !released) {
		if (!(ev = xcb_poll_for_event(con))) {
			querypointer(&x, &y);
			while (!(ev = xcb_wait_for_event(con)))
				xcb_flush(con);
		}
		switch (XCB_EVENT_RESPONSE_TYPE(ev)) {
		case XCB_MOTION_NOTIFY:
			e = (xcb_motion_notify_event_t *)ev;
			/* FIXME: we shouldn't need to query the pointer and just use the event root_x, root_y
			 * but for whatever reason there is some buffering happening and this forces
			 * a flush, using xcb_flush doesn't not seem to work in this case */
			if (!querypointer(&x, &y) || (e->time - last) < (1000 / 60))
				break;
			last = e->time;
			if (move)
				nx = ox + (x - mx), ny = oy + (y - my);
			else
				nw = ow + (x - mx), nh = oh + (y - my);
			if ((nw != c->w || nh != c->h || nx != c->x || ny != c->y)
					&& !c->floating && selws->layout->fn)
			{
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
{
	DBG("nextt: entering");
	while (c && c->floating)
		c = c->next;
	return c;
}

int optparse(char **argv, char **opts, int *argi, float *argf, int hex)
{
	float f;
	char **s = opts;
	int i = INT_MAX, ret = -1;

	DBG("optparse: entering");
	if (!argv || !*argv)
		return ret;
	if (argi)
		*argi = INT_MAX;
	if (argf)
		*argf = 0.0;
	while (*argv) {
		if (argi && ((hex && **argv == '#' && strlen(*argv) == 7)
					|| (i = strtol(*argv, NULL, 0)) || !strcmp(*argv, "0")))
			*argi = hex && **argv == '#' ? strtol(*argv + 1, NULL, 16) : i;
		else if (argf && (f = strtof(*argv, NULL)))
			*argf = f;
		else if (opts) {
			for (s = opts, i = 0; ret < 0 && s && *s; s++, i++)
				if (!strcmp(*s, *argv)) {
					if ((argi && *argi != INT_MAX) || (argf && *argf != 0.0))
						return i;
					ret = i;
				}
		}
		else
			return ret;
		argv++;
	}
	return ret;
}

Monitor *outputtomon(xcb_randr_output_t id)
{
	Monitor *m;

	DBG("outputtomon: entering");
	FOR_EACH(m, monitors)
		if (m->id == id)
			break;
	return m;
}

void print(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
}

void printerror(xcb_generic_error_t *e)
{
	if (e->error_code >= 128) {
		fprintf(stderr, ": X Extension Error:  Error code %d", e->error_code);
	} else {
		fprintf(stderr, ": X Error: %d", e->error_code);
		switch (e->error_code) {
		case XCB_ACCESS: fprintf(stderr, ": Access Denied"); break;
		case XCB_ALLOC: fprintf(stderr, ": Server Memory Allocation Failure"); break;
		case XCB_ATOM: fprintf(stderr, ": Bad Atom: 0x%x", e->resource_id); break;
		case XCB_COLORMAP: fprintf(stderr, ": Bad Color: 0x%x", e->resource_id); break;
		case XCB_CURSOR: fprintf(stderr, ": Bad Cursor: 0x%x", e->resource_id); break;
		case XCB_DRAWABLE: fprintf(stderr, ": Bad Drawable: 0x%x", e->resource_id); break;
		case XCB_FONT: fprintf(stderr, ": Bad Font: 0x%x", e->resource_id); break;
		case XCB_G_CONTEXT: fprintf(stderr, ": Bad GC: 0x%x", e->resource_id); break;
		case XCB_ID_CHOICE: fprintf(stderr, ": Bad XID: 0x%x", e->resource_id); break;
		case XCB_IMPLEMENTATION: fprintf(stderr, ": Server Implementation Failure"); break;
		case XCB_LENGTH: fprintf(stderr, ": Bad Request Length"); break;
		case XCB_MATCH: fprintf(stderr, ": Bad Match"); break;
		case XCB_NAME: fprintf(stderr, ": Bad Name"); break;
		case XCB_PIXMAP: fprintf(stderr, ": Bad Pixmap: 0x%x", e->resource_id); break;
		case XCB_REQUEST: fprintf(stderr, ": Bad Request"); break;
		case XCB_VALUE: fprintf(stderr, ": Bad Value: 0x%x", e->resource_id); break;
		case XCB_WINDOW: fprintf(stderr, ": Bad Window: 0x%x", e->resource_id); break;
		default: fprintf(stderr, ": Unknown error"); break;
		}
	}
	fprintf(stderr, ": Major code: %d", e->major_code);
	if (e->major_code >= 128)
		fprintf (stderr, ": Minor code: %d", e->minor_code);
	fprintf (stderr, ": Serial number: %d\n", e->full_sequence);
}

int querypointer(int *x, int *y)
{
	xcb_generic_error_t *e;
	xcb_query_pointer_reply_t *p;

	DBG("querypointer: entering");
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

	DBG("randrclone: entering");
	FOR_EACH(m, monitors)
		if (id != m->id && m->x == x && m->y == y)
			break;
	return m;
}

void resize(Client *c, int x, int y, int w, int h, int bw)
{
	uint v[] = { x, y, w, h, bw };

	DBG("resize: entering");
	c->old_x = c->x, c->old_y = c->y;
	c->old_w = c->w, c->old_h = c->h;
	c->x = x, c->y = y, c->w = w, c->h = h;
	xcb_configure_window(con, c->win, XYMASK | WHMASK | BWMASK, v);
	sendconfigure(c);
}

void resizehint(Client *c, int x, int y, int w, int h, int bw, int usermotion)
{
	DBG("resizehint: entering");
	if (applysizehints(c, &x, &y, &w, &h, usermotion))
		resize(c, x, y, w, h, bw);
}

void restack(Workspace *ws)
{
	Client *c;

	DBG("restack: entering");
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
	DBG("rulecmp: testing: class: %s, inst: %s, title: %s -- class: %s, inst: %s, title: %s",
			r->class, r->inst, r->title, class, inst, title);
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
{
	xcb_configure_notify_event_t ce;

	DBG("sendconfigure: entering");
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

	DBG("sendevent: entering");
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

	DBG("sendwmproto: entering");
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
	DBG("setclientws: setting client atom -- _NET_WM_DESKTOP: %d", num);
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

	DBG("setfullscreen: entering");
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
	DBG("setsticky: entering");
	if (sticky && !c->sticky)
		c->sticky = 1;
	else if (!sticky && c->sticky)
		c->sticky = 0;
}

void setstackmode(xcb_window_t win, uint mode)
{
	DBG("setstackmode: entering");
	xcb_configure_window(con, win, XCB_CONFIG_WINDOW_STACK_MODE, &mode);
}

void setwinstate(xcb_window_t win, uint32_t state)
{
	DBG("setwinstate: entering");
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
	DBG("seturgency: entering -- window: 0x%08x - urgency: %d", c->win, urg);
	if (xcb_icccm_get_wm_hints_reply(con, pc, &wmh, &e)) {
		DBG("seturgency: received WM_HINTS reply");
		wmh.flags = urg ? (wmh.flags | XCB_ICCCM_WM_HINT_X_URGENCY)
			: (wmh.flags & ~XCB_ICCCM_WM_HINT_X_URGENCY);
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
	if (c->ws == c->ws->mon->ws) {
		MOVE(c->win, c->x, c->y);
		if ((!c->ws->layout->fn || c->floating)
				&& (!c->fullscreen || (c->ffs && c->w != c->ws->mon->w && c->h != c->ws->mon->h)))
			resize(c, c->x, c->y, c->w, c->h, c->bw);
		showhide(c->snext);
	} else {
		showhide(c->snext);
		if (!c->sticky)
			MOVE(c->win, W(c) * -2, c->y);
		else if (selws && c->ws != selws && c->ws->mon == selws->mon) {
			sel = lastws->sel == c ? c : selws->sel;
			setclientws(c, selws->num);
			focus(sel);
		}
	}
}

void sighandle(int sig)
{
	DBG("sighandle: entering");
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

void sizehints(Client *c, int uss)
{
	xcb_size_hints_t s;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t pc;

	pc = xcb_icccm_get_wm_normal_hints(con, c->win);
	DBG("sizehints: entering -- getting size hints for window: 0x%08x", c->win);
	c->max_aspect = c->min_aspect = 0.0;
	c->increment_w = c->increment_h = 0;
	c->min_w = c->min_h = c->max_w = c->max_h = c->base_w = c->base_h = 0;
	if (xcb_icccm_get_wm_normal_hints_reply(con, pc, &s, &e)) {
		if (uss && s.flags & XCB_ICCCM_SIZE_HINT_US_POSITION) {
			DBG("sizehints: applying user specified position: %d,%d - current: %d,%d",
					s.x - c->bw, s.y - c->bw, c->x, c->y);
			c->x = s.x, c->y = s.y;
		}
		if (uss && s.flags & XCB_ICCCM_SIZE_HINT_US_SIZE) {
			DBG("sizehints: applying user specified size: %dx%d - current: %dx%d",
					s.width, s.height, c->w, c->h);
			c->w = s.width, c->h = s.height;
		}
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
	DBG("takefocus: entering");
	if (!c->nofocus) {
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, c->win, XCB_CURRENT_TIME);
		PROP_REPLACE(root, netatom[ActiveWindow], XCB_ATOM_WINDOW, 32, 1, &c->win);
	}
	sendwmproto(c, TakeFocus);
}

int tileresize(Client *c, Client *prev, uint wx, uint wy, uint ww, uint wh,
		uint x, uint y, int w, int h, int bw, int gap, uint *newy, int nremain, int left)
{
	int ret = 1, minh = 100;
	static int offset = 0;
	static Workspace *ws = NULL;

	DBG("tileresize: entering -- win: 0x%08x -- %d,%d @ %dx%d -- newy: %d, nremain: %d, left; %d",
			c->win, x, y, w, h, *newy, nremain, left);
	if (!c->hoff && h < minh) {
		c->floating = 1;
		if (ws != c->ws) {
			ws = c->ws;
			offset = 0;
		}
		h = MAX(wh / 6, 240);
		w = MAX(ww / 6, 360);
		x = MAX(wx + ww - w, (wx + offset + ww - w) / 4);
		y = MAX(wy + wh - h, (wy + offset + wh - h) / 4);
		offset += (x < (wx + offset + ww - w) && y < (wy + offset + wh - h)) ? 20 : offset * -1;
		setstackmode(c->win, XCB_STACK_MODE_ABOVE);
	} else if (nremain > 1 && (nremain - 1) * (minh + gap) > left) {
		h += left - ((nremain - 1) * (minh + gap));
		ret = -1;
	} else if (nremain == 1 && *newy + (h - gap) != wh) {
		DBG("tileresize: last client in stack but not using space")
		if (prev) {
			if (prev->h + left < minh) {
				ret = -1;
				resize(prev, prev->x, prev->y, prev->w, minh, bw);
				y = prev->y + minh;
				h = wh - (prev->y + prev->h);
			} else if (h < minh) {
				ret = -1;
				resize(prev, prev->x, prev->y, prev->w, prev->h + left - (minh - h), bw);
				y = prev->y + prev->h;
				h = minh;
			} else {
				resize(prev, prev->x, prev->y, prev->w, prev->h + left, bw);
				y += left;
			}
		} else {
			h = wh;
			ret = -1;
		}
	} else if (h < minh) {
		ret = -1;
		h = minh;
	}
	resize(c, x, y, w - (2 * bw), h - (2 * bw), bw);
	if (!c->floating)
		*newy += h + gap;
	return ret;
}

int tile(Workspace *ws)
{
	int h, w, ret = 1;
	Monitor *m = ws->mon;
	Client *c, *prev = NULL;
	uint i = 0, n, nr, my, sy, ssy, gap = 0;
	uint mw = 0, ss = 0, sw = 0, ssw = 0, ns = 1, bw = 0;
	uint wx = m->wx + ws->padl;
	uint wy = m->wy + ws->padt;
	uint ww = m->ww - ws->padl - ws->padr;
	uint wh = m->wh - ws->padt - ws->padb;

	for (n = 0, c = nextt(ws->clients); c; c = nextt(c->next), n++)
		;
	if (!n)
		return 1;
	if (n > 1 || !border[Smart])
		bw = border[Width], gap = ws->gappx;

	if (n <= ws->nmaster)
		mw = ww, ss = 1;
	else if (ws->nmaster)
		ns = 2, mw = ww * ws->split;

	if (n - ws->nmaster <= ws->nstack)
		sw = ww - mw;
	else
		sw = (ww - mw) * ws->ssplit;

	if (ws->nstack && n - ws->nmaster > ws->nstack)
		ss = 1, ssw = ww - mw - sw;

	DBG("tile: m->ww: %d - mw: %d - sw: %d - ssw: %d", m->ww, mw, sw, ssw);
	for (my = sy = ssy = gap, c = nextt(ws->clients); c; c = nextt(c->next), ++i) {
		if (i < ws->nmaster) {
			nr = MIN(n, ws->nmaster) - i;
			h = ((wh - my) / MAX(1, nr)) - gap + c->hoff;
			w = mw - gap * (5 - ns) / 2;
			if (tileresize(c, prev, wx + gap, wy + gap, ww - (2 * gap), wh - (2 * gap),
						wx + gap, wy + my, w, h, bw, gap, &my, nr, wh - (my + h + gap)) < 0)
				ret = -1;
		} else if (i - ws->nmaster < ws->nstack) {
			nr = MIN(n - ws->nmaster, ws->nstack) - (i - ws->nmaster);
			h = ((wh - sy) / MAX(1, nr)) - gap + c->hoff;
			w = sw - gap * (5 - ns - ss) / 2;
			if (ws->nmaster > 0 && i == ws->nmaster)
				prev = NULL;
			if (tileresize(c, prev, wx + gap, wy + gap, ww - (2 * gap), wh - (2 * gap),
						wx + mw + (gap / ns), wy + sy, w, h, bw,
						gap, &sy, nr, wh - (sy + h + gap)) < 0)
				ret = -1;
		} else {
			h = ((wh - ssy) / MAX(1, n - i)) - gap + c->hoff;
			w = ssw - gap * (5 - ns) / 2;
			if (ws->nstack + ws->nmaster > 0 && i == ws->nmaster + ws->nstack)
				prev = NULL;
			if (tileresize(c, prev, wx + gap, wy + gap, ww - (2 * gap), wh - (2 * gap),
						wx + mw + sw + (gap / ns), wy + ssy, w, h, bw,
						gap, &ssy, n - i, wh - (ssy + h + gap)) < 0)
				ret = -1;
		}
		prev = c;
	}
	return ret;
}

void unfocus(Client *c, int focusroot)
{
	DBG("unfocus: entering");
	if (!c)
		return;
	grabbuttons(c, 0);
	xcb_change_window_attributes(con, c->win, XCB_CW_BORDER_PIXEL, &border[Unfocus]);
	if (focusroot) {
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
		xcb_delete_property(con, root, netatom[ActiveWindow]);
	}
}

void ungrabpointer(void)
{
	DBG("ungrabpointer: entering");
	xcb_void_cookie_t c;

	c = xcb_ungrab_pointer_checked(con, XCB_CURRENT_TIME);
	checkerror(1, "failed to ungrab pointer", xcb_request_check(con, c));
}

void updatenumws(int needed)
{
	DBG("updatenumws: entering");
	char name[4];
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
			r.ssplit = ws->ssplit;
			r.layout = ws->layout;
			r.padl = ws->padl;
			r.padr = ws->padr;
			r.padt = ws->padt;
			r.padb = ws->padb;
			r.next = NULL;
			if (ws)
				ws->next = initws(numws, &r);
			else
				workspaces = initws(numws, &r);
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

	DBG("updateoutputs: entering -- received outputs %d outputs, requesting info for each", len);
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
			DBG("updateoutputs: output CRTC: %s -- location: %d,%d -- size: %dx%d -- status: %d",
					name, c->x, c->y, c->width, c->height, c->status);
			if ((m = randrclone(outs[i], c->x, c->y))) {
				DBG("updateoutputs: monitor %s, id %d is a clone of %s, id %d -- skipping",
						name, outs[i], m->name, m->id);
			} else if ((m = outputtomon(outs[i]))) {
				changed = (c->x != m->x || c->y != m->y || c->width != m->w || c->height != m->h);
				m->x = m->wx = c->x;
				m->y = m->wy = c->y;
				m->w = m->ww = c->width;
				m->h = m->wh = c->height;
				DBG("updateoutputs: new size and location for monitor: %s -- %d,%d @ %dx%d -- %s",
						m->name, m->x, m->y, m->w, m->h, changed ? "updated" : "unchanged");
			} else {
				FIND_TAIL(m, monitors);
				if (m)
					m->next = initmon(name, outs[i], c->x, c->y, c->width, c->height);
				else
					monitors = initmon(name, outs[i], c->x, c->y, c->width, c->height);
				changed = 1;
			}
			free(c);
		} else if ((m = outputtomon(outs[i]))
				&& o->connection == XCB_RANDR_CONNECTION_DISCONNECTED)
		{
			DBG("updateoutputs: output is inactive or disconnected: %s -- freeing", m->name);
			freemon(m);
			changed = 1;
		}
		free(o);
	}

	po = xcb_randr_get_output_primary_reply(con, xcb_randr_get_output_primary(con, root), NULL);
	primary = outputtomon(po->output);
	free(po);
	return changed;
}

int updaterandr(void)
{
	int len, changed;
	xcb_generic_error_t *e;
	xcb_timestamp_t timestamp;
	xcb_randr_output_t *outputs;
	xcb_randr_get_screen_resources_current_reply_t *r;
	xcb_randr_get_screen_resources_current_cookie_t rc;

	DBG("updaterandr: entering -- querying current randr outputs");
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

	DBG("updatestruts: entering");
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
	DBG("winattr: getting window attributes from window: 0x%08x", win);
	if (!(wa = xcb_get_window_attributes_reply(con, c, &e)))
		checkerror(0, "unable to get window attributes reply", e);
	return wa;
}

xcb_get_geometry_reply_t *wingeom(xcb_window_t win)
{
	xcb_generic_error_t *e;
	xcb_get_geometry_cookie_t gc;
	xcb_get_geometry_reply_t *g = NULL;

	gc = xcb_get_geometry(con, win);
	DBG("wingeom: getting window geometry from window: 0x%08x", win);
	if (!(g = xcb_get_geometry_reply(con, gc, &e)))
		checkerror(0, "unable to get window geometry reply", e);

	return g;
}

void winhints(Client *c)
{
	xcb_generic_error_t *e;
	xcb_icccm_wm_hints_t wmh;
	xcb_get_property_cookie_t pc;

	pc = xcb_icccm_get_wm_hints(con, c->win);
	DBG("winhints: getting window wm hints from window: 0x%08x", c->win);
	if (xcb_icccm_get_wm_hints_reply(con, pc, &wmh, &e)) {
		if (c == selws->sel && wmh.flags & XCB_ICCCM_WM_HINT_X_URGENCY) {
			wmh.flags &= ~XCB_ICCCM_WM_HINT_X_URGENCY;
			xcb_icccm_set_wm_hints(con, c->win, &wmh);
		} else {
			c->urgent = (wmh.flags & XCB_ICCCM_WM_HINT_X_URGENCY) ? 1 : 0;
		}
		c->nofocus = (wmh.flags & XCB_ICCCM_WM_HINT_INPUT) ? !wmh.input : 0;
	} else {
		checkerror(0, "unable to get window wm hints reply", e);
	}
}

xcb_atom_t winprop(xcb_window_t win, xcb_atom_t prop)
{
	xcb_atom_t ret;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t c;
	xcb_get_property_reply_t *r = NULL;

	c = xcb_get_property(con, 0, win, prop, XCB_ATOM_ANY, 0, 1);
	DBG("winprop: getting window property atom from window: 0x%08x", win);
	ret = -1;
	if ((r = xcb_get_property_reply(con, c, &e)) && xcb_get_property_value_length(r))
		ret = *(xcb_atom_t *)xcb_get_property_value(r);
	else
		checkerror(0, "unable to get window property reply", e);
	free(r);
	return ret;
}

int wintextprop(xcb_window_t w, xcb_atom_t atom, char *text, size_t size)
{
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t c;
	xcb_icccm_get_text_property_reply_t r;

	c = xcb_icccm_get_text_property(con, w, atom);
	DBG("wintextprop: getting window text property from window: 0x%08x", w);
	if (!xcb_icccm_get_text_property_reply(con, c, &r, &e)) {
		checkerror(0, "unable to get text property reply", e);
		return 0;
	}
	if(!r.name || !r.name_len)
		return 0;
	/* FIXME: encoding */
	/* if (r.encoding == XCB_ATOM_STRING) */
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
	xcb_window_t t;
	xcb_get_property_cookie_t pc;
	xcb_generic_error_t *e = NULL;

	pc = xcb_icccm_get_wm_transient_for(con, win);
	DBG("wintrans: getting transient for hint from window: 0x%08x", win);
	t = XCB_WINDOW_NONE;
	if (!xcb_icccm_get_wm_transient_for_reply(con, pc, &t, &e))
		checkerror(0, "unable to get wm transient for hint", e);
	return t;
}

void wintype(Client *c)
{
	xcb_atom_t type;

	DBG("wintype: entering");
	if (winprop(c->win, netatom[State]) == netatom[Fullscreen])
		setfullscreen(c, 1);
	if ((type = winprop(c->win, netatom[WindowType])) == netatom[Dialog])
		c->floating = 1;
	if ((int)type == -1 && wintrans(c->win))
		c->floating = 1;
}
