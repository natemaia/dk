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
#include <xcb/xcb_util.h>
#include <xcb/xcb_ewmh.h>
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

#define W(x)                ((x)->w + (2 * (x)->bw))
#define H(x)                ((x)->h + (2 * (x)->bw))
#define MIN(a, b)           ((a) < (b) ? (a) : (b))
#define MAX(a, b)           ((a) > (b) ? (a) : (b))
#define CLAMP(x, min, max)  (MIN(MAX((x), (min)), (max)))
#define LEN(x)              (sizeof(x) / sizeof(x[0]))
#define CLNMOD(mod)         (mod & ~(numlockmask | XCB_MOD_MASK_LOCK))
#define FLOATING(c)         ((c)->floating || !(c)->ws->layout->fn)
#define STICKY              (0xffffffff)
#define BWMASK              (XCB_CONFIG_WINDOW_BORDER_WIDTH)
#define XYMASK              (XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y)
#define WHMASK              (XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT)
#define BUTTONMASK          (XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE)
#define EVENT_RESPONSE_MASK (0x7f)
#define EVENT_TYPE(e)       (e->response_type &  EVENT_RESPONSE_MASK)
#define EVENT_SENT(e)       (e->response_type & ~EVENT_RESPONSE_MASK)

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

#define MOVERESIZE(win, x, y, w, h, bw)\
	xcb_configure_window(con, win, XYMASK | WHMASK | BWMASK,\
			(uint []){(x), (y), (w), (h), (bw)});

#define PROP_APPEND(win, atom, type, membsize, nmemb, value)\
	xcb_change_property(con, XCB_PROP_MODE_APPEND, (win), (atom),\
			(type), (membsize), (nmemb), (value))

#define PROP_REPLACE(win, atom, type, membsize, nmemb, value)\
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, (win), (atom),\
			(type), (membsize), (nmemb), (value))

/* aliases for long winded types */
typedef unsigned int uint;
typedef unsigned char uchar;
typedef xcb_get_geometry_reply_t Geometry;
typedef xcb_get_window_attributes_reply_t WindowAttr;

typedef struct Panel Panel;
typedef struct Client Client;
typedef struct Layout Layout;
typedef struct WsRule WsRule;
typedef struct WinRule WinRule;
typedef struct Monitor Monitor;
typedef struct Keyword Keyword;
typedef struct Command Command;
typedef struct Callback Callback;
typedef struct Workspace Workspace;
typedef struct DeskWin DeskWin;

enum Borders {
	Width, Smart, Focus, Unfocus, Urgent
};

enum Gravity {
	Left, Right, Center, Top, Bottom, None
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

struct DeskWin {
	int x, y, w, h;
	DeskWin *next;
	Monitor *mon;
	xcb_window_t win;
};

struct WsRule {
	char *name;
	uint nmaster, nstack, gappx;
	float split;
	float ssplit;
	uint padr, padl, padt, padb;
	Layout *layout;
	WsRule *next;
};

struct WinRule {
	int x, y, w, h, bw;
	int ws, floating, sticky;
	char *class, *inst, *title, *mon;
	Callback *cb;
	regex_t classreg, instreg, titlereg;
	WinRule *next;
};

struct Monitor {
	char *name;
	int num;
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

static Callback *applywinrule(Client *c);
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
static Monitor *coordtomon(int x, int y);
static void detach(Client *c, int reattach);
static void *ecalloc(size_t elems, size_t size);
static void eventignore(uint8_t type);
static void eventloop(void);
static void execcfg(void);
static void fixupworkspaces(void);
static void focus(Client *c);
static void freeclient(Client *c, int destroyed);
static void freedeskwin(DeskWin *d, int destroyed);
static void freepanel(Panel *panel, int destroyed);
static void freewin(xcb_window_t win, int destroyed);
static void freewinrule(WinRule *r);
static void freewm(void);
static void freews(Workspace *ws);
static void grabbuttons(Client *c, int focused);
static void gravitate(Client *c, int horz, int vert, int matchgap);
static void initclient(xcb_window_t win, Geometry *g);
static void initdeskwin(xcb_window_t win, Geometry *g);
static void initpanel(xcb_window_t win, Geometry *g);
static void initscan(void);
static int initwinrulereg(WinRule *r, WinRule *wr);
static void initwinrule(WinRule *r);
static void initwm(void);
static Workspace *initws(int num, WsRule *r);
static char *itoa(int n, char *s);
static Monitor *itomon(int num);
static Workspace *itows(int num);
static int layoutws(Workspace *ws);
static void mapwin(xcb_window_t win, Geometry *g, WindowAttr *wa, int cm);
static int mono(Workspace *ws);
static void mousemvr(int move);
static void movefocus(int direction);
static void movestack(int direction);
static Client *nextt(Client *c);
static int optparse(char **argv, char **opts, int *argi, float *argf, int hex);
static void printerror(xcb_generic_error_t *e);
static int querypointer(int *x, int *y);
static void resize(Client *c, int x, int y, int w, int h, int bw);
static void resizehint(Client *c, int x, int y, int w, int h, int bw, int usermotion, int mouse);
static void restack(Workspace *ws);
static int rulecmp(WinRule *r, char *title, char *class, char *inst);
static void sendconfigure(Client *c);
static void sendevent(Client *c, const char *ev, long mask);
static int sendwmproto(Client *c, int wmproto);
static void setclientws(Client *c, uint num);
static void setfullscreen(Client *c, int fullscreen);
static void setstackmode(xcb_window_t win, uint mode);
static void setsticky(Client *c, int sticky);
static void seturgent(Client *c, int urg);
static void setwinstate(xcb_window_t win, uint32_t state);
static void showhide(Client *c);
static void sighandle(int);
static void sizehints(Client *c, int uss);
static void takefocus(Client *c);
static int tile(Workspace *ws);
static void unfocus(Client *c, int focusroot);
static void ungrabpointer(void);
static void updateclientlist(void);
static int updaterandr(void);
static void updatestruts(Panel *p, int apply);
static void updateviewports(void);
static WindowAttr *winattr(xcb_window_t win);
static Geometry *wingeom(xcb_window_t win);
static void winhints(Client *c);
static xcb_atom_t winprop(xcb_window_t win, xcb_atom_t prop);
static int wintextprop(xcb_window_t w, xcb_atom_t atom, char *text, size_t size);
static Client *wintoclient(xcb_window_t win);
static DeskWin *wintodeskwin(xcb_window_t win);
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
	Active,
	Check,
	ClientList,
	CurDesktop,
	DemandsAttn,
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
	Viewport,
	WindowType,
	WmDesktop,
};
static const char *netatoms[] = {
	[Active] = "_NET_ACTIVE_WINDOW",
	[Check] = "_NET_SUPPORTING_WM_CHECK",
	[ClientList] = "_NET_CLIENT_LIST",
	[CurDesktop] = "_NET_CURRENT_DESKTOP",
	[DemandsAttn] = "_NET_WM_STATE_DEMANDS_ATTENTION",
	[DesktopNames] = "_NET_DESKTOP_NAMES",
	[Desktop] = "_NET_WM_WINDOW_TYPE_DESKTOP",
	[Dialog] = "_NET_WM_WINDOW_TYPE_DIALOG",
	[Dock] = "_NET_WM_WINDOW_TYPE_DOCK",
	[Fullscreen] = "_NET_WM_STATE_FULLSCREEN",
	[Name] = "_NET_WM_NAME",
	[NumDesktops] = "_NET_NUMBER_OF_DESKTOPS",
	[Splash] = "_NET_WM_WINDOW_TYPE_SPLASH",
	[State] = "_NET_WM_STATE",
	[StrutPartial] = "_NET_WM_STRUT_PARTIAL",
	[Strut] = "_NET_WM_STRUT",
	[Supported] = "_NET_SUPPORTED",
	[Viewport] = "_NET_DESKTOP_VIEWPORT",
	[WindowType] = "_NET_WM_WINDOW_TYPE",
	[WmDesktop] = "_NET_WM_DESKTOP",
};

static int border[] = {
	[Width] = 1,          /* border width in pixels */
	[Smart] = 1,          /* disable borders in mono layout or with only one tiled window */
	[Focus] = 0x6699cc,   /* focused window border colours */
	[Unfocus] = 0x111111, /* unfocused window border colours */
	[Urgent] = 0xee5555,  /* urgent window border colours */
};

static const char *cursors[] = {
	[Move] = "fleur",
	[Normal] = "arrow",
	[Resize] = "sizing"
};

/* primary keywords and parser functions
 * Keyword functions have the following prototype: void function(char **); */
static const Keyword keywords[] = {
	{ "rule", cmdrule },
	{ "set",  cmdset  },
	{ "win",  cmdwin  },
	{ "wm",   cmdwm   },
	{ "ws",   cmdws   },
};

/* "set" keyword options, used by cmdset() to parse arguments
 * Keyword functions have the following prototype: void function(char **); */
static const Keyword setcmds[] = {
	{ "border", cmdborder  },
	{ "gap",    cmdgappx   },
	{ "layout", cmdlayout  },
	{ "master", cmdnmaster },
	{ "mouse",  cmdmouse   },
	{ "pad",    cmdpad     },
	{ "split",  cmdsplit   },
	{ "stack",  cmdnstack  },
};

/* "win" keyword options, used by cmdwin() to parse arguments
 * Keyword functions have the following prototype: void function(char **); */
static const Keyword wincmds[] = {
	{ "cycle",    cmdcycle    },
	{ "fakefs",   cmdffs      },
	{ "float",    cmdfloat    },
	{ "focus",    cmdfocus    },
	{ "kill",     cmdkill     },
	{ "mvresize", cmdmvresize },
	{ "stick",    cmdstick    },
	{ "swap",     cmdswap     },
};

/* "ws" names used by cmdws() to parse arguments.
 * Command functions have the following prototype: void function(int); */
static const Command wscommands[] = {
	{ "follow", cmdfollow },
	{ "send",   cmdsend   },
	{ "view",   cmdview   },
};

#include "stringl.c"
#include "config.h"

extern char **environ;            /* environment variables */
static char *argv0;               /* program name */
static char *sock;                /* socket path, loaded from YAXWM_SOCK */
static int sockfd = -1;           /* socket file descriptor */
static FILE *cmdresp;             /* file for writing command messages into */
static int scr_w, scr_h;          /* root window size */
static int numws = 0;             /* number of workspaces currently allocated */
static int minxy = 10;            /* minimum window area allowed inside the screen when moving */
static int minwh = 50;            /* minimum window size allowed when resizing */
static int focusmouse = 1;        /* enable focus follows mouse */
static int focusurgent = 1;       /* enable focus on urgent window */
static int randrbase = -1;        /* randr extension response */
static int dborder[LEN(border)];  /* default border values for reset */
static uint running = 1;          /* continue handling events */
static uint restart = 0;          /* restart wm before quitting */
static uint numlockmask = 0;      /* numlock modifier bit mask */
static uint tilesizehints = 0;    /* respect size hints in tiled layouts */

static xcb_mod_mask_t mousemod = XCB_MOD_MASK_4;
static xcb_button_t   mousemove = XCB_BUTTON_INDEX_1;
static xcb_button_t   mouseresize = XCB_BUTTON_INDEX_3;

static Panel *panels;         /* panel list head */
static Monitor *primary;      /* primary monitor */
static Monitor *monitors;     /* monitor list head */
static WinRule *winrules;     /* window rule list head */
static DeskWin *deskwins;     /* desktop windows list head */
static Workspace *selws;      /* active workspace */
static Workspace *lastws;     /* last active workspace */
static Workspace *workspaces; /* workspace list head */

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
	Client *c = NULL;
	xcb_window_t sel;
	xcb_void_cookie_t ck;
	struct sigaction sa;
	static struct sockaddr_un sockaddr;
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
	}

	initscan();
	execcfg();
	layoutws(NULL);
	if ((sel = winprop(root, netatom[Active])) > 0)
		c = wintoclient(sel);
	focus(c);
	eventloop();

	return 0;
}

int adjbordergap(int i, int opt, int changing, int other)
{
	if (opt != stdabsolute)
		return i;
	return CLAMP(i, 0, (int)(selws->mon->wh - selws->padb - selws->padt / 6) - other) - changing;
}

void adjnmasterstack(int i, int opt, int master)
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
	if (n != INT_MAX && selws->clients)
		layoutws(selws);
}

void applypanelstrut(Panel *p)
{
	DBG("applypanelstrut: before: %s - %d,%d @ %dx%d",
			p->mon->name, p->mon->wx, p->mon->wy, p->mon->ww, p->mon->wh);
	if (p->mon->x + p->strut_l > p->mon->wx)
		p->mon->wx = p->strut_l;
	if (p->mon->y + p->strut_t > p->mon->wy)
		p->mon->wy = p->strut_t;
	if (p->mon->w - (p->strut_r + p->strut_l) < p->mon->ww)
		p->mon->ww = p->mon->w - (p->strut_r + p->strut_l);
	if (p->mon->h - (p->strut_b + p->strut_t) < p->mon->wh)
		p->mon->wh = p->mon->h - (p->strut_b + p->strut_t);
	DBG("applypanelstrut: after: %s - %d,%d @ %dx%d",
			p->mon->name, p->mon->wx, p->mon->wy, p->mon->ww, p->mon->wh);
}

int applysizehints(Client *c, int *x, int *y, int *w, int *h, int usermotion, int mouse)
{
	int baseismin;
	Monitor *m = c->ws->mon;

	DBG("applysizehints: 0x%08x - %d,%d @ %dx%d - usermotion: %d, mouse: %d",
			c->win, *x, *y, *w, *h, usermotion, mouse);
	*w = MAX(*w, MIN(minwh, c->min_w));
	*h = MAX(*h, MIN(minwh, c->min_h));
	if (usermotion) {
		if (!mouse) {
			if (*w > c->w && c->increment_w > *w - c->w)
				*w = c->w + c->increment_w;
			else if (*w < c->w && c->increment_w > c->w - *w)
				*w = c->w - c->increment_w;
			if (*h > c->h && c->increment_h > *h - c->h)
				*h = c->h + c->increment_h;
			else if (*h < c->h && c->increment_h > c->h - *h)
				*h = c->h - c->increment_h;
			*h = MIN(*h, m->wh);
			*w = MIN(*w, m->ww);
		}
		*x = CLAMP(*x, (*w + (2 * c->bw) - minxy) * -1, scr_w - minxy);
		*y = CLAMP(*y, (*h + (2 * c->bw) - minxy) * -1, scr_h - minxy);
	} else {
		*x = CLAMP(*x, m->wx, m->wx + m->ww - *w + (2 * c->bw));
		*y = CLAMP(*y, m->wy, m->wy + m->wh - *h + (2 * c->bw));
	}

	if (FLOATING(c) || tilesizehints) {
		if (!(baseismin = c->base_w == c->min_w && c->base_h == c->min_h))
			*w -= c->base_w, *h -= c->base_h;
		if (c->min_aspect > 0 && c->max_aspect > 0) {
			if (c->max_aspect < (float)*w / *h)
				*w = *h * c->max_aspect + 0.5;
			else if (c->min_aspect < (float)*h / *w)
				*h = *w * c->min_aspect + 0.5;
		}
		if (baseismin)
			*w -= c->base_w, *h -= c->base_h;
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
	DBG("applysizehints: 0x%08x - %d,%d @ %dx%d - usermotion: %d, mouse: %d",
			c->win, *x, *y, *w, *h, usermotion, mouse);
	return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

Callback *applywinrule(Client *c)
{
	Monitor *m;
	WinRule *r;
	int ws, num = -1;
	char title[BUFSIZ];
	Callback *cb = NULL;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t pc;
	xcb_icccm_get_wm_class_reply_t prop;

	DBG("applywinrule: 0x%08x", c->win);
	if (!wintextprop(c->win, netatom[Name], title, sizeof(title))
			&& !wintextprop(c->win, XCB_ATOM_WM_NAME, title, sizeof(title)))
		title[0] = '\0';
	pc = xcb_icccm_get_wm_class(con, c->win);

	if ((c->floating = c->trans != NULL))
		ws = c->trans->ws->num;
	else if ((ws = winprop(c->win, netatom[WmDesktop])) < 0)
		ws = selws->num;
	if (xcb_icccm_get_wm_class_reply(con, pc, &prop, &e)) {
		DBG("applywinrule: window class: %s - instance: %s - title: %s",
				prop.class_name, prop.instance_name, title);
		for (r = winrules; r; r = r->next) {
			if (!rulecmp(r, title, prop.class_name, prop.instance_name))
				continue;
			DBG("applywinrule: matched -- class: %s, inst: %s, title: %s",
					r->class, r->inst, r->title);
			c->floating = r->floating;
			c->sticky = r->sticky;
			cb = r->cb;
			if (r->bw != -1)
				c->bw = r->bw;
			if (r->x != -1)
				c->x = r->x;
			if (r->y != -1)
				c->y = r->y;
			if (r->w != -1)
				c->w = r->w;
			if (r->h != -1)
				c->h = r->h;
			if (!c->trans) {
				if (r->mon) {
					if ((num = strtol(r->mon, NULL, 0)) > 0 && (m = itomon(num))) {
						ws = m->ws->num;					
					} else for (m = monitors; m; m = m->next)
						if (!strcmp(r->mon, m->name)) {
							ws = m->ws->num;
							break;
						}
				} else if (r->ws >= 0)
					ws = r->ws;
			}
			break;
		}
		xcb_icccm_get_wm_class_reply_wipe(&prop);
	} else {
		checkerror(0, "failed to get window class", e);
	}
	setclientws(c, c->trans ? c->trans->ws->num : ws);
	DBG("applywinrule: workspace: %d, monitor: %s, floating: %d, "
			"sticky: %d, x: %d, y: %d, w: %d, h: %d, bw: %d", c->ws->num,
			c->ws->mon->name, c->floating, c->sticky, c->x, c->y, c->w, c->h, c->bw);
	return cb;
}

void assignworkspaces(void)
{
	WsRule r;
	Monitor *m;
	Workspace *ws;
	int n = 0;
	char name[4];

	FOR_EACH(m, monitors)
		n++;
	if (n > 999)
		errx(1, "attempting to allocate too many workspaces");
	else while (n > numws) {
		r.name = itoa(numws, name);
		r.nmaster = workspaces->nmaster;
		r.nstack = workspaces->nstack;
		r.ssplit = workspaces->ssplit;
		r.layout = workspaces->layout;
		r.gappx = workspaces->gappx;
		r.split = workspaces->split;
		r.padl = 0; r.padr = 0; r.padt = 0; r.padb = 0;
		r.next = NULL;
		initws(numws, &r);
		numws++;
	}
	ws = workspaces;
	FOR_EACH(m, monitors) {
		if (!m->ws) {
			DBG("assignworkspaces: %d:%s -> %s", ws->num, ws->name, m->name);
			m->ws = ws;
			ws->mon = m;
			ws = ws->next;
		} else if (m->ws == ws) {
			DBG("assignworkspaces: already assigned %d:%s - %s", ws->num, ws->name, m->name);
			ws = ws->next;
		}
	}
	FOR_EACH(ws, workspaces)
		if (!ws->mon) {
			DBG("assignworkspaces: %d:%s -> %s", ws->num, ws->name, primary->name);
			ws->mon = primary;
		}
	PROP_REPLACE(root, netatom[NumDesktops], XCB_ATOM_CARDINAL, 32, 1, &numws);
	updateviewports();
}

void attach(Client *c, int tohead)
{
	Client *t = NULL;

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
	p->next = panels;
	panels = p;
}

void attachdeskwin(DeskWin *d)
{
	d->next = deskwins;
	deskwins = d;
}

void attachstack(Client *c)
{
	c->snext = c->ws->stack;
	c->ws->stack = c;
}

void changews(Workspace *new, int usermotion)
{
	int x, y;
	int dowarp = 0;
	Monitor *m;

	DBG("changews: %d:%s", new->num, new->mon->name);
	lastws = selws;
	m = selws->mon;
	if (m != new->mon) {
		if ((selws->mon = new->mon) && new->mon->ws != new) {
			dowarp = 1;
			new->mon->ws = selws;
		}
		new->mon = m;
		m->ws = new;
		updateviewports();
	}
	selws = new;
	selws->mon->ws = new;
	PROP_REPLACE(root, netatom[CurDesktop], XCB_ATOM_CARDINAL, 32, 1, &new->num);
	if (dowarp || (!usermotion && (querypointer(&x, &y) && (m = coordtomon(x, y)) && m != new->mon))) {
		DBG("changews: warping pointer to new workspace");
		xcb_warp_pointer(con, root, root, 0, 0, 0, 0,
				new->mon->x + (new->mon->w / 2), new->mon->y + (new->mon->h / 2));
	}
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
	int i, n, opt, f = border[Focus], u = border[Unfocus], ur = border[Urgent];

	enum { colreset, colfocus, colunfocus, colurgent };
	char *colopt[] = { "reset", "focus", "unfocus", "urgent", NULL };
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
			border[Urgent] = dborder[Urgent];
		} else if (i <= 0xffffff && i >= 0) {
			if (opt == colfocus) {
				border[Focus] = i;
				if (selws->sel)
					xcb_change_window_attributes(con, selws->sel->win,
							XCB_CW_BORDER_PIXEL, &border[Focus]);
				return;
			} else if (opt == colunfocus) {
				border[Unfocus] = i;
			} else if (opt == colurgent) {
				border[Urgent] = i;
			}
		}
		if (f != border[Focus] || u != border[Unfocus] || ur != border[Urgent])
			FOR_CLIENTS(c, ws)
				xcb_change_window_attributes(con, c->win, XCB_CW_BORDER_PIXEL,
						&border[c == c->ws->sel ? Focus : (c->urgent ? Urgent : Unfocus)]);
	} else if (opt == width) {
		opt = optparse(argv + 1, stdopts, &i, NULL, 0);
		if (opt < 0 && i == INT_MAX)
			return;
		else if (opt == stdreset)
			i = dborder[Width];
		else if ((n = adjbordergap(i, opt, border[Width], selws->gappx)) != INT_MAX)
			i = CLAMP(border[Width] + n, 0, (int)((selws->mon->wh / 6) - selws->gappx));
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
	Client *c, *first;

	if (!(c = selws->sel) || FLOATING(c) || (c->fullscreen && !c->ffs))
		return;
	if (!c->ws->layout->fn || (c == (first = nextt(selws->clients)) && !nextt(c->next)))
		return;
	if (!(c = nextt(selws->sel->next)))
		c = first;
	focus(first);
	movestack(-1);
	focus(c);
	(void)(argv);
}

void cmdffs(char **argv)
{
	Client *c;
	Monitor *m;

	if (!(c = selws->sel))
		return;
	m = c->ws->mon;
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

	if (!(c = selws->sel) || (c->fullscreen && !c->ffs) || !c->ws->layout->fn)
		return;
	if ((c->floating = !c->floating || c->fixed)) {
		c->w = c->old_w, c->h = c->old_h;
		c->x = c->old_x ? c->old_x : (c->ws->mon->wx + c->ws->mon->ww - W(c)) / 2;
		c->y = c->old_y ? c->old_y : (c->ws->mon->wy + c->ws->mon->wh - H(c)) / 2;
		resizehint(c, c->x, c->y, c->w, c->h, c->bw, 0, 1);
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

	if (!selws->sel || num == selws->num || !(ws = itows(num)))
		return;
	if ((c = selws->sel)) {
		unfocus(c, 1);
		setclientws(c, num);
	}
	changews(ws, 0);
	layoutws(NULL);
	focus(NULL);
	restack(selws);
}

void cmdgappx(char **argv)
{
	int i, n, opt;
	uint ng = selws->gappx;

	if ((opt = optparse(argv, stdopts, &i, NULL, 0)) < 0 && i == INT_MAX)
		return;
	else if (opt == stdreset)
		ng = wsrules[selws->num].gappx;
	else if ((n = adjbordergap(i, opt, selws->gappx, border[Width])) != INT_MAX)
		ng = CLAMP((int)selws->gappx + n, 0, (selws->mon->wh / 6) - border[Width]);
	if (ng != selws->gappx) {
		selws->gappx = ng;
		layoutws(selws);
	}
}

void cmdkill(char **argv)
{
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

	if (!(c = selws->sel) || (c->fullscreen && !c->ffs))
		return;
	if ((absolute = !strcmp(*argv, "absolute"))) {
		w = c->w, h = c->h, x = c->x, y = c->y;
		argv++;
	}
	if (FLOATING(c)) {
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
			resizehint(c, x, y, w, h, c->bw, 1, 0);
		else
			resizehint(c, c->x + x, c->y + y, c->w + w, c->h + h, c->bw, 1, 0);
	} else if (c->ws->layout->fn == tile) {
		if (!strcmp(*argv, "y")) {
			argv++;
			if ((y = strtol(*argv, NULL, 0)))
				movestack(y > 0 ? 1 : -1);
			else {
				fprintf(cmdresp, "!invalid argument for stack location adjustment: %s", *argv);
				return;
			}
		} else if (!strcmp(*argv, "w")) {
			argv++;
			if ((w = strtol(*argv, NULL, 0))) {
				for (j = 0, t = nextt(selws->clients); t; t = nextt(t->next), j++)
					if (t == c) {
						sf = (c->ws->nmaster && j < c->ws->nmaster + c->ws->nstack) ? &c->ws->split
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
			argv++;
			if ((h = strtol(*argv, NULL, 0)) || !strcmp(*argv, "0")) {
				ohoff = c->hoff;
				c->hoff = absolute ? h : c->hoff + h;
				if (layoutws(selws) == -1) {
					fprintf(cmdresp, "!height adjustment for window exceeded limit: %d", c->hoff);
					c->hoff = ohoff;
				}
			} else {
				fprintf(cmdresp, "!invalid argument for height offset adjustment: %s", *argv);
				return;
			}
		} else {
			fprintf(cmdresp, "!invalid argument for move/resize on tiled window: %s", *argv);
			return;
		}
	} else {
		fprintf(cmdresp, "!unable to move/resize windows in the current layout");
		return;
	}
	eventignore(XCB_ENTER_NOTIFY);
}

void cmdnmaster(char **argv)
{
	int i, opt;

	opt = optparse(argv, minopts, &i, NULL, 0);
	adjnmasterstack(i, opt, 1);
}

void cmdnstack(char **argv)
{
	int i, opt;

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
	char *argv[15], k[BUFSIZ], tok[BUFSIZ], args[15][BUFSIZ];

	if (strqetok(&buf, k, sizeof(k))) {
		for (i = 0; i < LEN(keywords); i++)
			if ((matched = !strcmp(keywords[i].name, k))) {
				while (n + 1 < LEN(args) && buf && *buf && strqetok(&buf, tok, sizeof(tok))) {
					strlcpy(args[n], tok, sizeof(args[n]));
					argv[n] = args[n];
					n++;
				}
				argv[n] = NULL;
				if (*argv)
					keywords[i].func(argv);
				else
					fprintf(cmdresp, "!%s command requires additional arguments but none were given", k);
				break;
			}
		if (!matched)
			fprintf(cmdresp, "!invalid or unknown command: %s", k);
	}
	fflush(cmdresp);
	fclose(cmdresp);
}

void cmdrule(char **argv)
{
	uint ui;
	int i, rem;
	Workspace *ws;
	WinRule *wr, r;

	r.cb = NULL;
	r.sticky = 0, r.floating = 0;
	r.x = -1, r.y = -1, r.w = -1, r.h = -1, r.ws = -1, r.bw = -1;
	r.class = NULL, r.inst = NULL, r.title = NULL, r.mon = NULL;

	if ((rem = !strcmp("remove", *argv))) {
		argv++;
		if (!strcmp("all", *argv)) {
			while (winrules)
				freewinrule(winrules);
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
		} else if (!strcmp(*argv, "bw")) {
			argv++;
			if ((i = strtol(*argv, NULL, 0)) || !strcmp(*argv, "0"))
				r.bw = i;
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
				|| r.sticky || r.x != -1 || r.y != -1 || r.w != -1 || r.h != -1 || r.bw != -1))
	{
		FOR_EACH(wr, winrules) {
			if ((r.class && (r.class != wr->class || strcmp(r.class, wr->class)))
					|| (r.inst && (r.inst != wr->inst || strcmp(r.inst, wr->inst)))
					|| (r.title && (r.title != wr->title || strcmp(r.title, wr->title))))
				continue;
			if (rem && r.mon == wr->mon && r.ws == wr->ws && r.floating == wr->floating
					&& r.sticky == wr->sticky && r.x == wr->x && r.y == wr->y && r.bw == wr->bw
					&& r.w == wr->w && r.h == wr->h && r.cb == wr->cb)
				break;
			DBG("cmdrule: updating existing rule with the same match patterns");
			wr->ws = r.ws;
			wr->mon = r.mon;
			wr->sticky = r.sticky;
			wr->floating = r.floating;
			wr->x = r.x, wr->y = r.y, wr->w = r.w, wr->h = r.h, wr->bw = r.bw;
			break;
		}
		if (rem && wr)
			freewinrule(wr);
		else if (!rem && !wr)
			initwinrule(&r);
	}
}

void cmdsend(int num)
{
	Client *c;

	if (!(c = selws->sel) || num == selws->num || !itows(num))
		return;
	unfocus(c, 1);
	setclientws(c, num);
	layoutws(NULL);
	focus(NULL);
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
	fprintf(cmdresp, "!invalid argument for set command: %s", s);
}

void cmdsplit(char **argv)
{
	int opt;
	float f = 0.0, nf, *sf = &selws->split;

	if (!selws->layout->fn)
		return;
	if (!strcmp("stack", *argv)) {
		sf = &selws->ssplit;
		argv++;
	} else if (!strcmp("master", *argv))
		argv++;
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

	if (!(c = selws->sel) || (c->fullscreen && !c->ffs))
		return;
	setsticky(c, !c->sticky);
	(void)(argv);
}

void cmdswap(char **argv)
{
	Client *c;

	if (!(c = selws->sel) || (c->fullscreen && !c->ffs) || FLOATING(c))
		return;
	if (c == nextt(selws->clients) && !(c = nextt(c->next)))
		return;
	detach(c, 1);
	layoutws(c->ws);
	focus(NULL);
	(void)(argv);
}

void cmdwin(char **argv)
{
	uint i;
	char *s, **r;

	if (!(s = argv[0]))
		return;
	r = argv + 1;
	for (i = 0; i < LEN(wincmds); i++)
		if (!strcmp(wincmds[i].name, s)) {
			wincmds[i].func(r);
			return;
		}
	fprintf(cmdresp, "!invalid argument for win command: %s", s);
}

void cmdwm(char **argv)
{
	if (!strcmp("exit", *argv))
		running = 0;
	else if (!strcmp("reload", *argv))
		execcfg();
	else if (!strcmp("restart", *argv))
		running = 0, restart = 1;
	else
		fprintf(cmdresp, "!invalid argument for wm command: %s", *argv);
}

void cmdws(char **argv)
{
	uint j;
	Workspace *ws;
	int i = -1, n;
	void (*fn)(int) = cmdview;

	if (!argv || !*argv) {
		fprintf(cmdresp, "!ws command requires additional arguments but none were given");
		return;
	} else if (!strcmp("print", *argv)) {
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

	if (num == selws->num || !(ws = itows(num)))
		return;
	changews(ws, 0);
	layoutws(NULL);
	focus(NULL);
	restack(selws);
}

void clientcfgreq(Client *c, xcb_configure_request_event_t *e)
{
	Monitor *m;

	if (e->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH)
		c->bw = e->border_width;
	else if (FLOATING(c)) {
		DBG("clientcfgreq: floating window - 0x%08x", c->win);
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
		c->x = CLAMP(c->x, c->ws->mon->wx, m->wx + m->ww - W(c));
		c->y = CLAMP(c->y, c->ws->mon->wy, m->wy + m->wh - H(c));
		if ((e->value_mask & XYMASK) && !(e->value_mask & WHMASK))
			sendconfigure(c);
		if (c->ws == m->ws || (c->sticky && m == selws->mon))
			MOVERESIZE(c->win, c->x, c->y, c->w, c->h, c->bw);
		setstackmode(c->win, XCB_STACK_MODE_ABOVE);
	} else {
		sendconfigure(c);
	}
}

Monitor *coordtomon(int x, int y)
{
	Monitor *m = NULL;

	FOR_EACH(m, monitors)
		if (x >= m->x && y >= m->y && x >= m->x + m->w && y >= m->y + m->h)
			return m;
	return m;
}

void detach(Client *c, int reattach)
{
	Client **tc = &c->ws->clients;

	while (*tc && *tc != c)
		tc = &(*tc)->next;
	*tc = c->next;
	if (reattach)
		attach(c, 1);
}

void detachstack(Client *c)
{
	Client **tc = &c->ws->stack;

	while (*tc && *tc != c)
		tc = &(*tc)->snext;
	*tc = c->snext;
	if (c == c->ws->sel)
		c->ws->sel = c->ws->stack;
}

void freewin(xcb_window_t win, int destroyed)
{
	Panel *p;
	Client *c;
	DeskWin *d;

	if ((c = wintoclient(win)))
		freeclient(c, destroyed);
	else if ((p = wintopanel(win)))
		freepanel(p, destroyed);
	else if ((d = wintodeskwin(win)))
		freedeskwin(d, destroyed);
}

void *ecalloc(size_t elems, size_t size)
{
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
	static xcb_timestamp_t lasttime = 0;

	switch (EVENT_TYPE(ev)) {
	case XCB_FOCUS_IN:
	{
		xcb_focus_in_event_t *e = (xcb_focus_in_event_t *)ev;

		DBG("eventhandle: FOCUS_IN");
		if (e->mode == XCB_NOTIFY_MODE_GRAB || e->mode == XCB_NOTIFY_MODE_UNGRAB
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
			DBG("eventhandle: CONFIGURE_NOTIFY - screen size changed");
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
			DBG("eventhandle: CONFIGURE_REQUEST - managed - 0x%08x", e->window);
			clientcfgreq(c, e);
		} else {
			DBG("eventhandle: CONFIGURE_REQUEST - unmanaged - 0x%08x", e->window);
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

		freewin(e->window, 1);
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
		}
		if (focusmouse && c && c != selws->sel)
			focus(c);
		return;
	}
	case XCB_BUTTON_PRESS:
	{
		xcb_button_press_event_t *e = (xcb_button_press_event_t *)ev;

		if (!(c = wintoclient(e->event)))
			return;
		DBG("eventhandle: BUTTON_PRESS");
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

		if (e->event != root || (e->time - lasttime) < (1000 / 10))
			return;
		lasttime = e->time;
		if ((m = coordtomon(e->root_x, e->root_y)) && m != selws->mon) {
			DBG("eventhandle: MOTION_NOTIFY - updating active monitor");
			unfocus(selws->sel, 1);
			changews(m->ws, 1);
			focus(NULL);
		}
		return;
	}
	case XCB_MAP_REQUEST:
	{
		Geometry *g;
		WindowAttr *wa;
		xcb_map_request_event_t *e = (xcb_map_request_event_t *)ev;

		if (!(g = wingeom(e->window)) || !(wa = winattr(e->window)))
			return;
		DBG("eventhandle: MAP_REQUEST");
		mapwin(e->window, g, wa, 1);
		free(wa);
		free(g);
		return;
	}
	case XCB_UNMAP_NOTIFY:
	{
		xcb_unmap_notify_event_t *e = (xcb_unmap_notify_event_t *)ev;

		DBG("eventhandle: UNMAP_NOTIFY");
		if (EVENT_SENT(ev))
			setwinstate(e->window, XCB_ICCCM_WM_STATE_WITHDRAWN);
		else
			freewin(e->window, 0);
		return;
	}
	case XCB_CLIENT_MESSAGE:
	{
		xcb_client_message_event_t *e = (xcb_client_message_event_t *)ev;
		uint32_t *d = e->data.data32;

		if (e->type == netatom[CurDesktop]) {
			DBG("CLIENT_MESSAGE: %s - data: %d", netatoms[CurDesktop], d[0]);
			cmdview(d[0]);
		} else if ((c = wintoclient(e->window))) {
			if (e->type == netatom[WmDesktop]) {
				DBG("CLIENT_MESSAGE: %s - 0x%08x - data: %d - sticky: %d",
						netatoms[WmDesktop], c->win, d[0], d[0] == STICKY);
				setclientws(c, d[0] == STICKY ? (uint)c->ws->num : d[0]);
				setsticky(c, d[0] == STICKY ? 1 : 0);
				layoutws(NULL);
				focus(NULL);
			} else if (e->type == netatom[State] && (d[1] == netatom[Fullscreen]
						|| d[2] == netatom[Fullscreen]))
			{
				DBG("CLIENT_MESSAGE %s - 0x%08x - data: %d", netatoms[Fullscreen], c->win, d[0]);
				setfullscreen(c, (d[0] == 1 || (d[0] == 2 && !c->fullscreen)));
			} else if (c != selws->sel && (e->type == netatom[Active]
						|| (d[1] == netatom[DemandsAttn] || d[2] == netatom[DemandsAttn])))
			{
				DBG("CLIENT_MESSAGE: %s - 0x%08x", e->type == netatom[Active] ? netatoms[Active]
						: netatoms[DemandsAttn], c->win);
				if (focusurgent) {
					if (c->ws != selws) {
						unfocus(selws->sel, 1);
						cmdview(c->ws->num);
					}
					layoutws(NULL);
					focus(c);
				} else if (!c->urgent)
					seturgent(c, e->type == netatom[Active] ? 1
							: (d[0] == 1 || (d[0] == 2 && !c->urgent)));
			}
		}
		return;
	}
	case XCB_PROPERTY_NOTIFY:
	{
		Panel *p;
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
		xcb_generic_error_t *e = (xcb_generic_error_t *)ev;

		if (ev->response_type && randrbase > 0 && ev->response_type == randrbase + XCB_RANDR_SCREEN_CHANGE_NOTIFY) {
			DBG("eventhandle: RANDR_NOTIFY or RANDR_SCREEN_CHANGE_NOTIFY");
			if (updaterandr() > 0 && monitors) {
				DBG("eventhandle: outputs changed after randr event");
				fixupworkspaces();
			}
		} else if (!ev->response_type && e) {
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

	xcb_flush(con);
	while (running && (ev = xcb_poll_for_event(con))) {
		if (EVENT_TYPE(ev) != type)
			eventhandle(ev);
		free(ev);
	}
}

void eventloop(void)
{
	ssize_t n;
	fd_set read_fds;
	char buf[PIPE_BUF];
	int confd, nfds, cmdfd;
	xcb_generic_event_t *ev;

	confd = xcb_get_file_descriptor(con);
	nfds = MAX(confd, sockfd) + 1;
	while (running) {
		xcb_flush(con);
		FD_ZERO(&read_fds);
		FD_SET(sockfd, &read_fds);
		FD_SET(confd, &read_fds);
		if (select(nfds, &read_fds, NULL, NULL, NULL) > 0) {
			if (FD_ISSET(sockfd, &read_fds)) {
				cmdfd = accept(sockfd, NULL, 0);
				if (cmdfd > 0 && (n = recv(cmdfd, buf, sizeof(buf) - 1, 0)) > 0) {
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
{
	Panel *p;
	Client *c;
	DeskWin *d;
	Workspace *ws;

	assignworkspaces();
	FOR_CLIENTS(c, ws)
		if (c->fullscreen && (!c->ffs || (c->w == ws->mon->w && c->h == ws->mon->h)))
			resize(c, ws->mon->x, ws->mon->y, ws->mon->w, ws->mon->h, c->bw);
	FOR_EACH(p, panels)
		updatestruts(p, 1);
	FOR_EACH(d, deskwins)
		if (d->x != d->mon->wx || d->y != d->mon->wy || d->w != d->mon->ww || d->h != d->mon->wh) {
			d->x = d->mon->wx, d->y = d->mon->wy, d->w = d->mon->ww, d->h = d->mon->wh;
			MOVERESIZE(d->win, d->x, d->y, d->w, d->h, 0);
		}
	layoutws(NULL);
	focus(NULL);
}

void floatoffset(Client *c, int d, int *x, int *y, int *w, int *h)
{
	Monitor *m = c->ws->mon;
	static int offset = 0;
	static Workspace *ws = NULL;

	if (ws != c->ws) {
		ws = c->ws;
		offset = 0;
	}
	*x = MIN(m->wx + m->ww - (*w + (2 * c->bw)), m->wx + (m->ww / d) + offset);
	*y = MIN(m->wy + m->wh - (*h + (2 * c->bw)), m->wy + (m->wh / d) + offset);
	if (*x + *w + (2 * c->bw) < m->wx + m->ww && *y + *h + (2 * c->bw) < m->wy + m->wh)
		offset += minwh;
	else
		offset += offset * -1 + rand() % 100;
}

void focus(Client *c)
{
	if (!c || c->ws != c->ws->mon->ws)
		c = selws->stack;
	if (selws->sel && selws->sel != c)
		unfocus(selws->sel, c ? 0 : 1);
	if (c) {
		if (c->urgent)
			seturgent(c, 0);
		detachstack(c);
		attachstack(c);
		grabbuttons(c, 1);
		xcb_change_window_attributes(con, c->win, XCB_CW_BORDER_PIXEL, &border[Focus]);
		takefocus(c);
	}
	selws->sel = c;
}

void freeclient(Client *c, int destroyed)
{
	if (!c)
		return;
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
	updateclientlist();
	layoutws(NULL);
	focus(NULL);
}

void freedeskwin(DeskWin *d, int destroyed)
{
	DeskWin **dd = &deskwins;

	while (*dd && *dd != d)
		dd = &(*dd)->next;
	*dd = d->next;
	if (!destroyed) {
		xcb_grab_server(con);
		setwinstate(d->win, XCB_ICCCM_WM_STATE_WITHDRAWN);
		xcb_flush(con);
		xcb_ungrab_server(con);
	}
	free(d);
	updateclientlist();
}

void freemon(Monitor *m)
{
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
	updateclientlist();
	layoutws(NULL);
}

void freewinrule(WinRule *r)
{
	WinRule **cr = &winrules;

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

void freewm(void)
{
	uint i;
	char fdstr[20];
	Workspace *ws;

	FOR_EACH(ws, workspaces)
		while (ws->stack)
			freeclient(ws->stack, 0);
	while (panels)
		freepanel(panels, 0);
	while (deskwins)
		freedeskwin(deskwins, 0);
	while (workspaces)
		freews(workspaces);
	while (monitors)
		freemon(monitors);
	while (winrules)
		freewinrule(winrules);
	for (i = 0; i < LEN(cursors); i++)
		xcb_free_cursor(con, cursor[i]);
	xcb_key_symbols_free(keysyms);
	xcb_destroy_window(con, wmcheck);
	xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT,
			XCB_INPUT_FOCUS_POINTER_ROOT, XCB_CURRENT_TIME);
	xcb_delete_property(con, root, netatom[Active]);
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

	pc = xcb_grab_pointer(con, 0, root, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE
			| XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_POINTER_MOTION_HINT,
			XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, root, cursor, XCB_CURRENT_TIME);
	if ((ptr = xcb_grab_pointer_reply(con, pc, &e)))
		r = ptr->status == XCB_GRAB_STATUS_SUCCESS;
	else
		checkerror(0, "unable to grab pointer", e);
	free(ptr);
	return r;
}

void gravitate(Client *c, int horz, int vert, int matchgap)
{
	int x, y, gap;

	if (!c || !c->ws || !FLOATING(c))
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
	resizehint(c, x, y, c->w, c->h, c->bw, 0, 0);
}

void initatoms(xcb_atom_t *atoms, const char **names, int num)
{
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

void initclient(xcb_window_t win, Geometry *g)
{
	Monitor *m;
	Client *c = NULL;
	Callback *cb = NULL;
	xcb_window_t trans;

	DBG("initclient: managing new window - 0x%08x", win);
	c = ecalloc(1, sizeof(Client));
	c->win = win;
	c->x = c->old_x = g->x, c->y = c->old_y = g->y;
	c->w = c->old_w = g->width, c->h = c->old_h = g->height;
	c->old_bw = g->border_width;
	c->bw = border[Width];
	if ((trans = wintrans(c->win)) != XCB_WINDOW_NONE)
		c->trans = wintoclient(trans);
	cb = applywinrule(c);
	m = c->ws->mon;
	c->w = CLAMP(c->w, minwh, m->ww);
	c->h = CLAMP(c->h, minwh, m->wh);
	if (c->trans) {
		c->x = m->wx + c->trans->x + ((W(c->trans) - W(c)) / 2);
		c->y = m->wy + c->trans->y + ((H(c->trans) - H(c)) / 2);
	}
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
	if (FLOATING(c) || (c->floating = c->oldstate = trans != XCB_WINDOW_NONE || c->fixed)) {
		c->x = CLAMP(c->x, c->ws->mon->wx, m->wx + m->ww - W(c));
		c->y = CLAMP(c->y, c->ws->mon->wy, m->wy + m->wh - H(c));
		if (c->x + c->y <= m->wx)
			floatoffset(c, 6, &c->x, &c->y, &c->w, &c->h);
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
	} else
		MOVE(c->win, H(c) * -2, c->y);
	xcb_map_window(con, win);
	focus(NULL);
	if (cb)
		cb->fn(c);
	DBG("initclient: mapped - 0x%08x - workspace %d - %d,%d @ %dx%d - floating: %d",
			c->win, c->ws->num, c->x, c->y, c->w, c->h, FLOATING(c));
}

void initdeskwin(xcb_window_t win, Geometry *g)
{
	DeskWin *d;
	uint m = XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY;

	DBG("initdesktopwin: 0x%08x - %d,%d @ %dx%d",
			win, g->x, g->y, g->width, g->height);
	d = ecalloc(1, sizeof(DeskWin));
	d->win = win;
	if (!(d->mon = coordtomon(g->x, g->y)))
		d->mon = selws->mon;
	attachdeskwin(d);
	xcb_change_window_attributes(con, d->win, XCB_CW_EVENT_MASK, &m);
	d->x = d->mon->wx, d->y = d->mon->wy, d->w = d->mon->ww, d->h = d->mon->wh;
	MOVERESIZE(d->win, d->x, d->y, d->w, d->h, 0);
	setwinstate(d->win, XCB_ICCCM_WM_STATE_NORMAL);
	setstackmode(d->win, XCB_STACK_MODE_BELOW);
	PROP_APPEND(root, netatom[ClientList], XCB_ATOM_WINDOW, 32, 1, &d->win);
	xcb_map_window(con, d->win);
}

Monitor *initmon(int num, char *name, xcb_randr_output_t id, int x, int y, int w, int h)
{
	Monitor *m, *tail;
	uint len = strlen(name) + 1;

	DBG("initmon: %d:%s - %d,%d @ %dx%d", num, name, x, y, w, h);
	m = ecalloc(1, sizeof(Monitor));
	m->x = m->wx = x;
	m->y = m->wy = y;
	m->w = m->ww = w;
	m->h = m->wh = h;
	m->num = num;
	m->id = id;
	m->name = ecalloc(1, len);
	if (len > 1)
		strlcpy(m->name, name, len);
	FIND_TAIL(tail, monitors);
	if (tail)
		tail->next = m;
	else
		monitors = m;
	return m;
}

void initpanel(xcb_window_t win, Geometry *g)
{
	int *s;
	Panel *p;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t rc;
	xcb_get_property_reply_t *r = NULL;
	uint m = XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY;

	DBG("initpanel: 0x%08x - %d,%d @ %dx%d",
			win, g->x, g->y, g->width, g->height);
	p = ecalloc(1, sizeof(Panel));
	p->win = win;
	p->x = g->x, p->y = g->y, p->w = g->width, p->h = g->height;
	if (!(p->mon = coordtomon(p->x, p->y)))
		p->mon = selws->mon;
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
	PROP_APPEND(root, netatom[ClientList], XCB_ATOM_WINDOW, 32, 1, &p->win);
	layoutws(NULL);
	xcb_map_window(con, p->win);
	focus(NULL);
	DBG("initpanel: mapped - 0x%08x - mon: %s - %d,%d @ %dx%d",
			p->win, p->mon->name, p->x, p->y, p->w, p->h);
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
{
	uint i;
	xcb_atom_t *s;
	Geometry **g;
	WindowAttr **wa;
	xcb_window_t *w;
	xcb_generic_error_t *e;
	xcb_query_tree_reply_t *rt;
	uint8_t v = XCB_MAP_STATE_VIEWABLE, ic = XCB_ICCCM_WM_STATE_ICONIC;

	if ((rt = xcb_query_tree_reply(con, xcb_query_tree(con, root), &e)) && rt->children_len) {
		w = xcb_query_tree_children(rt);
		s = ecalloc(rt->children_len, sizeof(xcb_atom_t));
		g = ecalloc(rt->children_len, sizeof(Geometry *));
		wa = ecalloc(rt->children_len, sizeof(WindowAttr *));
		for (i = 0; i < rt->children_len; i++) {
			g[i] = NULL;
			s[i] = XCB_WINDOW_NONE;
			if (!(wa[i] = winattr(w[i])) || !(g[i] = wingeom(w[i]))
					|| !(wa[i]->map_state == v || winprop(w[i], wmatom[WMState]) == ic))
				w[i] = XCB_WINDOW_NONE;
			else if (!wintrans(w[i])) {
				mapwin(w[i], g[i], wa[i], 0);
				w[i] = XCB_WINDOW_NONE;
			}
		}
		for (i = 0; i < rt->children_len; i++) {
			if (w[i])
				mapwin(w[i], g[i], wa[i], 0);
			free(wa[i]);
			free(g[i]);
		}
		free(rt); free(s); free(wa); free(g);
	} else {
		checkerror(1, "unable to query tree from root window", e);
	}
}

void initwinrule(WinRule *r)
{
	size_t len;
	WinRule *wr;

	DBG("initwinrule: class: %s - inst: %s - title: %s - mon: %s - ws: %d "
			"- floating: %d - sticky: %d - position: %d,%d - size: %d x %d",
			r->class, r->inst, r->title, r->mon, r->ws, r->floating,
			r->sticky, r->x, r->y, r->w, r->h);
	wr = ecalloc(1, sizeof(WinRule));
	wr->class = NULL, wr->inst = NULL, wr->title = NULL, wr->mon = NULL;
	wr->ws = r->ws;
	wr->floating = r->floating;
	wr->sticky = r->sticky;
	wr->cb = r->cb;
	wr->bw = r->bw;
	wr->x = r->x, wr->y = r->y, wr->w = r->w, wr->h = r->h;
	if (r->mon) {
		len = strlen(r->mon) + 1;
		wr->mon = ecalloc(1, len);
		strlcpy(wr->mon, r->mon, len);
	}
	if (initwinrulereg(wr, r)) {
		wr->next = winrules;
		winrules = wr;
		DBG("initwinrule: complete: class: %s - inst: %s - title: %s - mon: %s - ws: %d "
				"- floating: %d - sticky: %d - position: %d,%d - size: %dx%d",
				wr->class, wr->inst, wr->title, wr->mon, wr->ws, wr->floating,
				wr->sticky, wr->x, wr->y, wr->w, wr->h);
	} else {
		free(wr->mon);
		free(wr);
	}
}

int initwinrulereg(WinRule *r, WinRule *wr)
{
	int i;
	size_t len;
	char buf[NAME_MAX], *e;

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

void initwm(void)
{
	uint i, j;
	int r, cws;
	Workspace *ws;
	size_t len = 1;
	xcb_void_cookie_t ck;
	xcb_cursor_context_t *ctx;

	if ((randrbase = initrandr()) < 0 || !monitors)
		monitors = initmon(0, "default", 0, 0, 0, scr_w, scr_h);
	if (!primary)
		primary = monitors;
	for (numws = 0; numws < (int)LEN(wsrules); numws++)
		initws(numws, &wsrules[numws]);
	assignworkspaces();
	selws = workspaces;
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
	PROP_REPLACE(root, netatom[Supported], XCB_ATOM_ATOM, 32, LEN(netatom), netatom);
	xcb_delete_property(con, root, netatom[ClientList]);
	cws = (r = winprop(root, netatom[CurDesktop])) >= 0 ? r : 0;
	changews((ws = itows(cws)) ? ws : workspaces, 0);
	FOR_EACH(ws, workspaces)
		len += strlen(ws->name) + 1;
	char names[len];
	len = 0;
	FOR_EACH(ws, workspaces)
		for (j = 0; (names[len++] = ws->name[j]); j++);
	PROP_REPLACE(root, netatom[DesktopNames], wmatom[Utf8Str], 8, --len, names);
	ck = xcb_change_window_attributes_checked(con, root, XCB_CW_EVENT_MASK | XCB_CW_CURSOR,
			(uint []){ XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT
			| XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_BUTTON_PRESS
			| XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_ENTER_WINDOW
			| XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_STRUCTURE_NOTIFY
			| XCB_EVENT_MASK_PROPERTY_CHANGE,
			cursor[Normal] });
	checkerror(1, "unable to change root window event mask and cursor", xcb_request_check(con, ck));
	if (!(keysyms = xcb_key_symbols_alloc(con)))
		err(1, "unable to get keysyms from X connection");
}

Workspace *initws(int num, WsRule *r)
{
	Workspace *ws, *tail;

	DBG("initws: %d:%s", num, r->name);
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
	FIND_TAIL(tail, workspaces);
	if (tail)
		tail->next = ws;
	else
		workspaces = ws;
	return ws;
}

char *itoa(int n, char *s)
{
	char c;
	int j, i = 0, sign = n;

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

Monitor *itomon(int num)
{
	Monitor *mon = monitors;

	while (mon && mon->num != num)
		mon = mon->next;
	return mon;
}

Workspace *itows(int num)
{
	Workspace *ws = workspaces;

	while (ws && ws->num != num)
		ws = ws->next;
	return ws;
}

int layoutws(Workspace *ws)
{
	int ret = 1;

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

void mapwin(xcb_window_t win, Geometry *g, WindowAttr *wa, int cm)
{
	Panel *p;
	Client *c;
	DeskWin *d;
	xcb_atom_t type;

	if (cm && ((c = wintoclient(win)) || (p = wintopanel(win)) || (d = wintodeskwin(win))))
		return;
	DBG("mapwin: unmanaged window - 0x%08x", win);
	type = winprop(win, netatom[WindowType]);
	if (type == netatom[Splash])
		xcb_map_window(con, win);
	else if (type == netatom[Dock])
		initpanel(win, g);
	else if (type == netatom[Desktop])
		initdeskwin(win, g);
	else if (!wa->override_redirect)
		initclient(win, g);
}

int mono(Workspace *ws)
{
	Client *c;
	int gap = 0, bw = 0;
	uint wx = ws->mon->wx + ws->padl;
	uint wy = ws->mon->wy + ws->padt;
	uint ww = ws->mon->ww - ws->padl - ws->padr;
	uint wh = ws->mon->wy - ws->padt - ws->padb;

	if (!border[Smart])
		bw = border[Width], gap = ws->gappx;
	for (c = nextt(ws->clients); c; c = nextt(c->next))
		resizehint(c, wx + gap, wy + gap, ww - gap, wh - gap, bw, 0, 0);
	return 1;
}

void movefocus(int direction)
{
	Client *c;

	if (!selws->sel || (selws->sel->fullscreen && !selws->sel->ffs))
		return;
	while (direction) {
		if (direction > 0) {
			c = selws->sel->next ? selws->sel->next : selws->clients;
			direction--;
		} else {
			FIND_PREV(c, selws->sel, selws->clients);
			direction++;
		}
		if (c)
			focus(c);
	}
}

void movestack(int direction)
{
	int i = 0;
	Client *c;

	if (!selws->sel || selws->sel->floating || !nextt(selws->clients->next))
		return;
	while (direction) {
		if (direction > 0) {
			detach(selws->sel, (c = nextt(selws->sel->next)) ? 0 : 1);
			if (c) {
				selws->sel->next = c->next;
				c->next = selws->sel;
			}
			direction--;
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
			direction++;
		}
	}
	layoutws(selws);
	focus(selws->sel);
}

void mousemvr(int move)
{
	Client *c;
	Monitor *m;
	int first = 1;
	xcb_timestamp_t last = 0;
	xcb_motion_notify_event_t *e;
	xcb_generic_event_t *ev = NULL;
	int mx, my, ox, oy, ow, oh, nw, nh, nx, ny, x, y, released = 0;

	if (!(c = selws->sel) || (c->fullscreen && !c->ffs) || !querypointer(&mx, &my))
		return;
	ox = nx = c->x, oy = ny = c->y, ow = nw = c->w, oh = nh = c->h;
	if (!grabpointer(cursor[move ? Move : Resize]))
		return;
	while (running && !released) {
		if (!(ev = xcb_poll_for_event(con))) {
			if (first--)
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
			if ((nw != c->w || nh != c->h || nx != c->x || ny != c->y) && !FLOATING(c)) {
				c->old_x = c->x, c->old_y = c->y, c->old_h = c->h, c->old_w = c->w;
				cmdfloat(NULL);
				layoutws(c->ws);
			}
			if (FLOATING(c)) {
				if (move && (m = coordtomon(x, y)) && m != c->ws->mon) {
					setclientws(c, m->ws->num);
					changews(m->ws, 1);
					focus(c);
				}
				resizehint(c, nx, ny, nw, nh, c->bw, 1, 1);
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
	while (c && c->floating)
		c = c->next;
	return c;
}

int optparse(char **argv, char **opts, int *argi, float *argf, int hex)
{
	float f;
	char **s = opts;
	int i = INT_MAX, ret = -1;

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
	c->old_x = c->x, c->old_y = c->y;
	c->old_w = c->w, c->old_h = c->h;
	c->x = x, c->y = y, c->w = w, c->h = h;
	MOVERESIZE(c->win, x, y, w, h, bw);
	sendconfigure(c);
}

void resizehint(Client *c, int x, int y, int w, int h, int bw, int usermotion, int mouse)
{
	if (applysizehints(c, &x, &y, &w, &h, usermotion, mouse))
		resize(c, x, y, w, h, bw);
}

void restack(Workspace *ws)
{
	Client *c;
	DeskWin *d;

	if (!ws)
		ws = selws;
	if (!ws || !(c = ws->sel))
		return;
	if (FLOATING(c))
		setstackmode(c->win, XCB_STACK_MODE_ABOVE);
	if (ws->layout->fn) {
		FOR_STACK(c, ws->stack)
			if (!c->floating && c->ws == c->ws->mon->ws)
				setstackmode(c->win, XCB_STACK_MODE_BELOW);
		FOR_EACH(d, deskwins)
			setstackmode(d->win, XCB_STACK_MODE_BELOW);
	}
	eventignore(XCB_ENTER_NOTIFY);
}

int rulecmp(WinRule *r, char *title, char *class, char *inst)
{
	if (!r)
		return 0;
	DBG("rulecmp: testing: class: %s, inst: %s, title: %s - class: %s, inst: %s, title: %s",
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

	DBG("clientcfgreq: sending configure notify event window - 0x%08x", c->win);
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

void sendevent(Client *c, const char *ev, long mask)
{
	xcb_generic_error_t *e;
	xcb_void_cookie_t vc;

	vc = xcb_send_event_checked(con, 0, c->win, mask, ev);
	e = xcb_request_check(con, vc);
	checkerror(0, "unable to send configure notify event to client window", e);
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
	DBG("setclientws: 0x%08x -> %d", c->win, num);
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
	int d = c->ws->num;

	if (sticky && !c->sticky)
		c->sticky = 1, d = STICKY;
	else if (!sticky && c->sticky)
		c->sticky = 0, d = c->ws->num;
	PROP_REPLACE(c->win, netatom[WmDesktop], XCB_ATOM_CARDINAL, 32, 1, &d);
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

void seturgent(Client *c, int urg)
{
	xcb_generic_error_t *e;
	xcb_icccm_wm_hints_t wmh;
	xcb_get_property_cookie_t pc;

	DBG("seturgent: 0x%08x - urgent: %d", c->win, urg);
	pc = xcb_icccm_get_wm_hints(con, c->win);
	if (c != selws->sel) {
		if ((c->urgent = urg))
			xcb_change_window_attributes(con, c->win, XCB_CW_BORDER_PIXEL, &border[Urgent]);
		else
			xcb_change_window_attributes(con, c->win, XCB_CW_BORDER_PIXEL, &border[Unfocus]);
	}
	if (xcb_icccm_get_wm_hints_reply(con, pc, &wmh, &e)) {
		DBG("seturgent: received WM_HINTS reply");
		wmh.flags = urg ? (wmh.flags | XCB_ICCCM_WM_HINT_X_URGENCY) : (wmh.flags & ~XCB_ICCCM_WM_HINT_X_URGENCY);
		xcb_icccm_set_wm_hints(con, c->win, &wmh);
	} else {
		checkerror(0, "unable to get wm window hints", e);
	}
}

void showhide(Client *c)
{
	Client *sel;
	Monitor *m;

	if (!c)
		return;
	m = c->ws->mon;
	if (c->ws == m->ws) {
		if (FLOATING(c) && (c->x <= m->x - W(c) || c->y <= m->y - H(c)
					|| c->x >= m->x + m->w || c->y >= m->y + m->h))
		{
			c->x = CLAMP(c->x, m->x, m->x + m->w - W(c));
			c->y = CLAMP(c->y, m->y, m->y + m->h - H(c));
		}
		MOVE(c->win, c->x, c->y);
		if (FLOATING(c) && (!c->fullscreen || (c->ffs && c->w != m->w && c->h != m->h)))
			resize(c, c->x, c->y, c->w, c->h, c->bw);
		showhide(c->snext);
	} else {
		showhide(c->snext);
		if (!c->sticky)
			MOVE(c->win, W(c) * -2, c->y);
		else if (selws && c->ws != selws && m == selws->mon) {
			sel = lastws->sel == c ? c : selws->sel;
			setclientws(c, selws->num);
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

void sizehints(Client *c, int uss)
{
	xcb_size_hints_t s;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t pc;

	pc = xcb_icccm_get_wm_normal_hints(con, c->win);
	DBG("sizehints: getting size hints - 0x%08x", c->win);
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
	if (!c->nofocus) {
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, c->win, XCB_CURRENT_TIME);
		PROP_REPLACE(root, netatom[Active], XCB_ATOM_WINDOW, 32, 1, &c->win);
	}
	sendwmproto(c, TakeFocus);
}

int tileresize(Client *c, Client *p, uint ww, uint wh,
		int x, int y, int w, int h, int bw, int gap, uint *newy, int nrem, int avail)
{
	int ret = 1;

	DBG("tileresize: 0x%08x - %d,%d @ %dx%d - newy: %d, nrem: %d, avail; %d",
			c->win, x, y, w, h, *newy, nrem, avail);
	if (!c->hoff && h < minwh) {
		DBG("toggling floating");
		c->floating = 1;
		h = MAX(wh / 6, 240);
		w = MAX(ww / 6, 360);
		floatoffset(c, 4, &x, &y, &w, &h);
		setstackmode(c->win, XCB_STACK_MODE_ABOVE);
	} else if (nrem > 1 && (nrem - 1) * (minwh + gap) > avail) {
		h += avail - ((nrem - 1) * (minwh + gap));
		ret = -1;
	} else if (nrem == 1 && *newy + (h - gap) != wh) {
		DBG("tileresize: last client in stack but not using space")
		if (p) {
			DBG("tileresize: adjusting previous client to fit");
			if (p->h + avail < minwh) {
				ret = -1;
				resizehint(p, p->x, p->y, p->w, minwh, bw, 0, 0);
				y = p->y + minwh;
				h = wh - (p->y + p->h);
			} else if (h < minwh) {
				ret = -1;
				resizehint(p, p->x, p->y, p->w, p->h + avail - (minwh - h), bw, 0, 0);
				y = p->y + p->h;
				h = minwh;
			} else {
				resizehint(p, p->x, p->y, p->w, p->h + avail, bw, 0, 0);
				y += avail;
			}
		} else {
			h = wh;
			ret = -1;
		}
	} else if (h < minwh) {
		ret = -1;
		h = minwh;
	}
	resizehint(c, x, y, w - (2 * bw), h - (2 * bw), bw, 0, 0);
	if (!c->floating)
		*newy += h + gap;
	return ret;
}

int tile(Workspace *ws)
{
	int h, w, ret = 1;
	Monitor *m = ws->mon;
	Client *c, *prev = NULL;
	uint i, n, nr, my, sy, ssy;
	uint wx = m->wx + ws->padl;
	uint wy = m->wy + ws->padt;
	uint ww = m->ww - ws->padl - ws->padr;
	uint wh = m->wh - ws->padt - ws->padb;
	uint mw = 0, ss = 0, sw = 0, ssw = 0, ns = 1, bw = 0, gap = 0;

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

	DBG("tile: monitor height: %d - master width: %d - stack1 width: %d - stack2 width: %d",
			m->ww, mw, sw, ssw);
	for (i = 0, my = sy = ssy = gap, c = nextt(ws->clients); c; c = nextt(c->next), ++i) {
		if (i < ws->nmaster) {
			nr = MIN(n, ws->nmaster) - i;
			h = ((wh - my) / MAX(1, nr)) - gap + c->hoff;
			w = mw - gap * (5 - ns) / 2;
			if (tileresize(c, prev, ww - (2 * gap), wh - (2 * gap), wx + gap,
						wy + my, w, h, bw, gap, &my, nr, wh - (my + h + gap)) < 0)
				ret = -1;
		} else if (i - ws->nmaster < ws->nstack) {
			nr = MIN(n - ws->nmaster, ws->nstack) - (i - ws->nmaster);
			h = ((wh - sy) / MAX(1, nr)) - gap + c->hoff;
			w = sw - gap * (5 - ns - ss) / 2;
			if (ws->nmaster > 0 && i == ws->nmaster)
				prev = NULL;
			if (tileresize(c, prev, ww - (2 * gap), wh - (2 * gap), wx + mw + (gap / ns),
						wy + sy, w, h, bw, gap, &sy, nr, wh - (sy + h + gap)) < 0)
				ret = -1;
		} else {
			h = ((wh - ssy) / MAX(1, n - i)) - gap + c->hoff;
			w = ssw - gap * (5 - ns) / 2;
			if (ws->nstack + ws->nmaster > 0 && i == ws->nmaster + ws->nstack)
				prev = NULL;
			if (tileresize(c, prev, ww - (2 * gap), wh - (2 * gap), wx + mw + sw + (gap / ns),
						wy + ssy, w, h, bw, gap, &ssy, n - i, wh - (ssy + h + gap)) < 0)
				ret = -1;
		}
		prev = c;
	}
	return ret;
}

void unfocus(Client *c, int focusroot)
{
	if (c) {
		grabbuttons(c, 0);
		xcb_change_window_attributes(con, c->win, XCB_CW_BORDER_PIXEL, &border[Unfocus]);
	}
	if (focusroot) {
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
		xcb_delete_property(con, root, netatom[Active]);
	}
}

void ungrabpointer(void)
{
	xcb_void_cookie_t c;

	c = xcb_ungrab_pointer_checked(con, XCB_CURRENT_TIME);
	checkerror(1, "failed to ungrab pointer", xcb_request_check(con, c));
}

void updateclientlist(void)
{
	Panel *p;
	Client *c;
	DeskWin *d;
	Workspace *ws;

	xcb_delete_property(con, root, netatom[ClientList]);
	FOR_CLIENTS(c, ws)
		PROP_APPEND(root, netatom[ClientList], XCB_ATOM_WINDOW, 32, 1, &c->win);
	FOR_EACH(d, deskwins)
		PROP_APPEND(root, netatom[ClientList], XCB_ATOM_WINDOW, 32, 1, &d->win);
	FOR_EACH(p, panels)
		PROP_APPEND(root, netatom[ClientList], XCB_ATOM_WINDOW, 32, 1, &p->win);
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
	xcb_randr_get_crtc_info_reply_t *crtc;
	xcb_randr_get_output_info_cookie_t oc[len];
	xcb_randr_get_output_primary_reply_t *po = NULL;
	uint8_t disconnected = XCB_RANDR_CONNECTION_DISCONNECTED;

	DBG("updateoutputs: %d outputs", len);
	for (i = 0; i < len; i++)
		oc[i] = xcb_randr_get_output_info(con, outs[i], timestamp);
	for (i = 0; i < len; i++) {
		if (!(o = xcb_randr_get_output_info_reply(con, oc[i], &e))) {
			checkerror(0, "unable to get monitor info", e);
			continue;
		}
		if (o->crtc != XCB_NONE && o->connection != disconnected) {
			ck = xcb_randr_get_crtc_info(con, o->crtc, timestamp);
			if (!(crtc = xcb_randr_get_crtc_info_reply(con, ck, &e))) {
				checkerror(0, "crtc info for randr output was NULL", e);
				free(o);
				continue;
			}
			n = xcb_randr_get_output_info_name_length(o) + 1;
			strlcpy(name, (char *)xcb_randr_get_output_info_name(o), MIN(sizeof(name), n));
			DBG("updateoutputs: %s - location: %d,%d - size: %dx%d - status: %d",
					name, crtc->x, crtc->y, crtc->width, crtc->height, crtc->status);
			if ((m = randrclone(outs[i], crtc->x, crtc->y))) {
				DBG("updateoutputs: %s is a clone of %s - skipping", name, m->name);
			} else if ((m = outputtomon(outs[i]))) {
				changed = (crtc->x != m->x || crtc->y != m->y
						|| crtc->width != m->w || crtc->height != m->h);
				m->x = m->wx = crtc->x;
				m->y = m->wy = crtc->y;
				m->w = m->ww = crtc->width;
				m->h = m->wh = crtc->height;
				DBG("updateoutputs: new size and location for monitor: %s - %d,%d @ %dx%d",
						m->name, m->x, m->y, m->w, m->h);
			} else {
				initmon(i, name, outs[i], crtc->x, crtc->y, crtc->width, crtc->height);
				changed = 1;
			}
			free(crtc);
		} else if (o->connection == disconnected && (m = outputtomon(outs[i]))) {
			DBG("updateoutputs: output is inactive or disconnected: %s - freeing", m->name);
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

	DBG("updaterandr: querying current randr outputs");
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

void updateviewports(void)
{
	int v[2];
	Workspace *ws;

	xcb_delete_property(con, root, netatom[Viewport]);
	FOR_EACH(ws, workspaces) {
		v[0] = ws->mon->x, v[1] = ws->mon->y;
		PROP_APPEND(root, netatom[Viewport], XCB_ATOM_CARDINAL, 32, 2, &v);
	}
}

WindowAttr *winattr(xcb_window_t win)
{
	WindowAttr *wa = NULL;
	xcb_generic_error_t *e;
	xcb_get_window_attributes_cookie_t c;

	if (win && win != root) {
		c = xcb_get_window_attributes(con, win);
		DBG("winattr: getting window attributes - 0x%08x", win);
		if (!(wa = xcb_get_window_attributes_reply(con, c, &e)))
			checkerror(0, "unable to get window attributes reply", e);
	}
	return wa;
}

Geometry *wingeom(xcb_window_t win)
{
	Geometry *g = NULL;
	xcb_generic_error_t *e;
	xcb_get_geometry_cookie_t gc;

	if (win && win != root) {
		gc = xcb_get_geometry(con, win);
		DBG("wingeom: getting window geometry - 0x%08x", win);
		if (!(g = xcb_get_geometry_reply(con, gc, &e)))
			checkerror(0, "unable to get window geometry reply", e);
	}
	return g;
}

void winhints(Client *c)
{
	xcb_generic_error_t *e;
	xcb_icccm_wm_hints_t wmh;
	xcb_get_property_cookie_t pc;

	pc = xcb_icccm_get_wm_hints(con, c->win);
	DBG("winhints: getting window wm hints - 0x%08x", c->win);
	if (xcb_icccm_get_wm_hints_reply(con, pc, &wmh, &e)) {
		if (c == selws->sel && wmh.flags & XCB_ICCCM_WM_HINT_X_URGENCY) {
			wmh.flags &= ~XCB_ICCCM_WM_HINT_X_URGENCY;
			xcb_icccm_set_wm_hints(con, c->win, &wmh);
		} else
			c->urgent = (wmh.flags & XCB_ICCCM_WM_HINT_X_URGENCY) ? 1 : 0;
		c->nofocus = (wmh.flags & XCB_ICCCM_WM_HINT_INPUT) ? !wmh.input : 0;
	} else {
		checkerror(0, "unable to get window wm hints reply", e);
	}
}

xcb_atom_t winprop(xcb_window_t win, xcb_atom_t prop)
{
	xcb_atom_t ret = -1;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t c;
	xcb_get_property_reply_t *r = NULL;

	if (!win)
		return ret;
	c = xcb_get_property(con, 0, win, prop, XCB_ATOM_ANY, 0, 1);
	DBG("winprop: getting window property atom - 0x%08x", win);
	if ((r = xcb_get_property_reply(con, c, &e)) && xcb_get_property_value_length(r)) {
		ret = *(xcb_atom_t *)xcb_get_property_value(r);
		DBG("winprop: success", win);
	} else
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
	DBG("wintextprop: getting window text property - 0x%08x", w);
	if (!xcb_icccm_get_text_property_reply(con, c, &r, &e)) {
		checkerror(0, "unable to get text property reply", e);
		return 0;
	}
	if (!r.name || !r.name_len)
		return 0;
	strlcpy(text, r.name, size);
	xcb_icccm_get_text_property_reply_wipe(&r);
	return 1;
}

Client *wintoclient(xcb_window_t win)
{
	Workspace *ws;
	Client *c = NULL;

	if (win != root)
		FOR_CLIENTS(c, ws)
			if (c->win == win)
				return c;
	return c;
}

Panel *wintopanel(xcb_window_t win)
{
	Panel *p = NULL;

	if (win != root)
		FOR_EACH(p, panels)
			if (p->win == win)
				return p;
	return p;
}

DeskWin *wintodeskwin(xcb_window_t win)
{
	DeskWin *d = NULL;

	if (win != root)
		FOR_EACH(d, deskwins)
			if (d->win == win)
				return d;
	return d;
}

Workspace *wintows(xcb_window_t win)
{
	int x, y;
	Client *c;
	Monitor *m;
	Workspace *ws;

	if (win == root && querypointer(&x, &y) && (m = coordtomon(x, y)))
		return m->ws;
	FOR_CLIENTS(c, ws)
		if (c->win == win)
			return ws;
	return selws;
}

xcb_window_t wintrans(xcb_window_t win)
{
	xcb_get_property_cookie_t pc;
	xcb_generic_error_t *e = NULL;
	xcb_window_t t = XCB_WINDOW_NONE;

	pc = xcb_icccm_get_wm_transient_for(con, win);
	DBG("wintrans: getting transient for hint - 0x%08x", win);
	if (!xcb_icccm_get_wm_transient_for_reply(con, pc, &t, &e))
		checkerror(0, "unable to get wm transient for hint", e);
	return t;
}

void wintype(Client *c)
{
	xcb_atom_t type;

	if (winprop(c->win, netatom[State]) == netatom[Fullscreen])
		setfullscreen(c, 1);
	if ((type = winprop(c->win, netatom[WindowType])) == netatom[Dialog])
		c->floating = 1;
	if ((int)type == -1 && (c->trans || wintrans(c->win)))
		c->floating = 1;
}
