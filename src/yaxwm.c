/* yet another X window manager
* see license file for copyright and license details
* vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
*/

#define _XOPEN_SOURCE 700

#include <sys/un.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/socket.h>

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
#define VERSION "0.6"
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

typedef unsigned int uint;
typedef unsigned char uchar;
typedef struct Panel Panel;
typedef struct Client Client;
typedef struct Layout Layout;
typedef struct WinRule WinRule;
typedef struct Monitor Monitor;
typedef struct Keyword Keyword;
typedef struct Command Command;
typedef struct DeskWin DeskWin;
typedef struct Callback Callback;
typedef struct Workspace Workspace;
typedef struct WsDefault WsDefault;
typedef xcb_get_geometry_reply_t Geometry;
typedef xcb_get_window_attributes_reply_t WindowAttr;

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

struct WinRule {
	int x, y, w, h, bw;
	int ws, floating, sticky;
	char *class, *inst, *title, *mon;
	Callback *cb;
	regex_t classreg, instreg, titlereg;
	WinRule *next;
};

struct Monitor {
	char name[NAME_MAX];
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
	void (*fn)(char **);
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
	char name[NAME_MAX];
	int nmaster, nstack, gappx, defgap;
	int padr, padl, padt, padb;
	float split;
	float ssplit;
	Layout *layout;
	Monitor *mon;
	Workspace *next;
	Client *sel, *stack, *clients, *hidden;
};

struct WsDefault {
	int nmaster, nstack, gappx;
	int padr, padl, padt, padb;
	float split;
	float ssplit;
	Layout *layout;
};

static Callback *applywinrule(Client *c);
static int checkerror(int, char *, xcb_generic_error_t *);
static void cmdborder(char **);
static void cmdcycle(char **);
static void cmdffs(char **);
static void cmdfloat(char **);
static void cmdfocus(char **);
static void cmdfollow(int);
static void cmdgappx(char **);
static void cmdkill(char **);
static void cmdlayout(char **);
static void cmdmon(char **);
static void cmdmouse(char **);
static void cmdnmaster(char **);
static void cmdnstack(char **);
static void cmdpad(char **);
static void cmdresize(char **);
static void cmdrule(char **);
static void cmdsend(int);
static void cmdset(char **);
static void cmdsplit(char **);
static void cmdstick(char **);
static void cmdswap(char **);
static void cmdview(int);
static void cmdwin(char **);
static void cmdwm(char **);
static void cmdws(char **);
static void cmdwsdef(char **);
static Monitor *coordtomon(int, int);
static void detach(Client *, int);
static void *ecalloc(size_t, size_t);
static void eventignore(uint8_t);
static void eventloop(void);
static void execcfg(void);
static void focus(Client *);
static void freeclient(Client *, int);
static void freedeskwin(DeskWin *, int);
static void freepanel(Panel *, int);
static void freewinrule(WinRule *r);
static void freewin(xcb_window_t, int);
static void freewm(void);
static void freews(Workspace *);
static void grabbuttons(Client *, int);
static void gravitate(Client *, int, int, int);
static void initclient(xcb_window_t, Geometry *);
static void initdeskwin(xcb_window_t, Geometry *);
static void initpanel(xcb_window_t, Geometry *);
static void initscan(void);
static int initwinrulereg(WinRule *, WinRule *);
static void initwinrule(WinRule *);
static void initwm(void);
static Workspace *initws(int);
static char *itoa(int n, char *);
static Monitor *itomon(int);
static Workspace *itows(int);
static int layoutws(Workspace *);
static void mapwin(xcb_window_t, Geometry *, WindowAttr *, int);
static int mono(Workspace *);
static void mousemvr(int);
static void movefocus(int);
static void movestack(int);
static Client *nextt(Client *);
static void parsecmd(char *);
static void printerror(xcb_generic_error_t *);
static int querypointer(int *, int *);
static void relocatefloating(Workspace *, Monitor *);
static void resize(Client *, int, int, int, int, int);
static void resizehint(Client *, int, int, int, int, int, int, int);
static void restack(Workspace *);
static int rulecmp(WinRule *, char *, char *, char *);
static void sendconfigure(Client *);
static void sendevent(Client *, const char *, long);
static int sendwmproto(Client *, int);
static void setclientws(Client *, int);
static void setfullscreen(Client *, int);
static void setinputfoocus(Client *);
static void setstackmode(xcb_window_t, uint);
static void setsticky(Client *, int);
static void seturgent(Client *, int);
static void setwmwinstate(xcb_window_t, uint32_t);
static void showhide(Client *);
static void sighandle(int);
static void sizehints(Client *, int);
static int tile(Workspace *);
static void unfocus(Client *, int);
static void ungrabpointer(void);
static void updateclientlist(void);
static void updatenumws(int);
static int updaterandr(void);
static void updatestruts(Panel *, int);
static void updateviewports(void);
static void updateworkspaces(int);
static WindowAttr *winattr(xcb_window_t);
static Geometry *wingeom(xcb_window_t);
static void winhints(Client *);
static xcb_atom_t winprop(xcb_window_t, xcb_atom_t);
static int wintextprop(xcb_window_t, xcb_atom_t, char *, size_t);
static Client *wintoclient(xcb_window_t);
static DeskWin *wintodeskwin(xcb_window_t);
static Panel *wintopanel(xcb_window_t);
static Workspace *wintows(xcb_window_t);
static xcb_window_t wintrans(xcb_window_t);
static void wintype(Client *);

enum { optnext, optprev, optlast };
static char *moveopts[] = { [optnext] = "next", [optprev] = "prev", [optlast] = "last", NULL };
static const char enoargs[] = "command requires additional arguments but none were given";

static int gap[] = {
	[Width] = 0, /* gap width in pixels */
	[Smart] = 1, /* disable gaps in mono layout or with only one tiled window */
};

static int border[] = {
	[Width] = 1,          /* border width in pixels */
	[Smart] = 1,          /* disable borders in mono layout or with only one tiled window */
	[Focus] = 0x6699cc,   /* focused window border colours */
	[Unfocus] = 0x000000, /* unfocused window border colours */
	[Urgent] = 0xee5555,  /* urgent window border colours */
};

static const char *cursors[] = {
	[Move] = "fleur",
	[Normal] = "arrow",
	[Resize] = "sizing"
};

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

/* primary keywords and parser functions
 * Keyword functions have the following prototype: void function(char **); */
static const Keyword keywords[] = {
	{ "mon",   cmdmon  },
	{ "rule",  cmdrule },
	{ "set",   cmdset  },
	{ "win",   cmdwin  },
	{ "wm",    cmdwm   },
	{ "ws",    cmdws   },
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
	{ "wsdef",  cmdwsdef   },
};

/* "win" keyword options, used by cmdwin() to parse arguments
 * Keyword functions have the following prototype: void function(char **); */
static const Keyword wincmds[] = {
	{ "cycle",    cmdcycle  },
	{ "fakefs",   cmdffs    },
	{ "float",    cmdfloat  },
	{ "focus",    cmdfocus  },
	{ "kill",     cmdkill   },
	{ "resize",   cmdresize },
	{ "stick",    cmdstick  },
	{ "swap",     cmdswap   },
};

/* "ws" and "mon" commands used by cmdws() and cmdmon() to parse arguments.
 * Command functions have the following prototype: void function(int); */
static const Command wsmoncmds[] = {
	{ "follow", cmdfollow },
	{ "send",   cmdsend   },
	{ "view",   cmdview   },
};

/* file for writing command messages into, needs to be defined before including parse.c */
static FILE *cmdresp;

#include "string.c"
#include "parse.c"
#include "config.h"

extern char **environ;            /* environment variables */
static char *argv0;               /* program executable name/path */
static char *sock;                /* socket path */
static int sockfd = -1;           /* socket file descriptor */
static int scr_w, scr_h;          /* root window size */
static uint running = 1;          /* continue handling events */
static uint restart = 0;          /* restart wm before quitting */
static uint numlockmask = 0;      /* numlock modifier bit mask */
static int numws = 0;             /* number of workspaces currently allocated */
static int minxy = 10;            /* minimum window area allowed inside the screen when moving */
static int minwh = 50;            /* minimum window size allowed when resizing */
static int usemoncmd = 0;         /* changes how the view, send, and follow functions work */
static int randrbase = -1;        /* randr extension response */
static int dborder[LEN(border)];  /* default border values for reset */

/* config settings */
static int cfgtsizehints = 0;     /* respect size hints in tiled layouts */
static int cfgfocusmouse = 1;     /* enable focus follows mouse */
static int cfgfocusurgent = 1;    /* enable focus on urgent window */
/* default settings for newly created workspaces */
static WsDefault wsdef = { 1, 3, 0, 0, 0, 0, 0, 0.55, 0.55, &layouts[0] };

static xcb_mod_mask_t mousemod = XCB_MOD_MASK_4;
static xcb_button_t mousemove = XCB_BUTTON_INDEX_1;
static xcb_button_t mouseresize = XCB_BUTTON_INDEX_3;

static Panel *panels;         /* panel list head */
static Monitor *primary;      /* primary monitor */
static Monitor *monitors;     /* monitor list head */
static Monitor *selmon;       /* active monitor */
static Monitor *lastmon;      /* last active monitor */
static WinRule *winrules;     /* window rule list head */
static DeskWin *deskwins;     /* desktop windows list head */
static Workspace *setws;      /* workspace to use for set commands */
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
			fprintf(stderr, arg ? "yaxwm "VERSION"\n" : "usage: yaxwm [-hv] [-s SOCKET_FD]\n");
			exit(0);
		} else {
			fprintf(stderr, "usage: yaxwm [-hv] [-s SOCKET_FD]\n");
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

void adjustsetting(int i, int relative, int *setting, int other, int setbordergap)
{
	int n = INT_MAX;
	int max = setws->mon->wh - setws->padb - setws->padt;

	if (i == INT_MAX)
		return;
	else if (setbordergap)
		max = (max / 6) - other;
	else
		max /= minwh;
	if (!relative)
		i -= *setting;
	if ((n = CLAMP(*setting + i, 0, max)) != *setting)
		*setting = n;
	if (setws->clients && setws == setws->mon->ws)
		layoutws(setws);
}

void applypanelstrut(Panel *p)
{
	if (p->mon->x + p->strut_l > p->mon->wx)
		p->mon->wx = p->strut_l;
	if (p->mon->y + p->strut_t > p->mon->wy)
		p->mon->wy = p->strut_t;
	if (p->mon->w - (p->strut_r + p->strut_l) < p->mon->ww)
		p->mon->ww = p->mon->w - (p->strut_r + p->strut_l);
	if (p->mon->h - (p->strut_b + p->strut_t) < p->mon->wh)
		p->mon->wh = p->mon->h - (p->strut_b + p->strut_t);
	DBG("applypanelstrut: %s - %d,%d @ %dx%d -> %d,%d @ %dx%d",
			p->mon->name, p->mon->x, p->mon->y, p->mon->w, p->mon->h,
			p->mon->wx, p->mon->wy, p->mon->ww, p->mon->wh);
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

	if (FLOATING(c) || cfgtsizehints) {
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
	else if (ws <= 10)
		updatenumws(ws);
	if (xcb_icccm_get_wm_class_reply(con, pc, &prop, &e)) {
		DBG("applywinrule: window property: class: %s, instance: %s, title: %s,",
				prop.class_name, prop.instance_name, title);
		for (r = winrules; r; r = r->next) {
			if (!rulecmp(r, title, prop.class_name, prop.instance_name))
				continue;
			DBG("applywinrule: matched rule: class: %s, inst: %s, title: %s",
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
	setclientws(c, c->trans && c->trans->ws ? c->trans->ws->num : ws);
	DBG("applywinrule: workspace: %s, monitor: %s, floating: %d, "
			"sticky: %d, x: %d, y: %d, w: %d, h: %d, bw: %d", c->ws->name,
			c->ws->mon->name, c->floating, c->sticky, c->x, c->y, c->w, c->h, c->bw);
	return cb;
}

void attach(Client *c, int tohead)
{
	Client *t = NULL;

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

void attachstack(Client *c)
{
	c->snext = c->ws->stack;
	c->ws->stack = c;
}

void changews(Workspace *ws, int allowswap, int allowwarp)
{
	Monitor *m, *oldmon;
	int diffmon = allowwarp && selws->mon != ws->mon;

	if (ws == selws)
		return;
	DBG("changews: %s:%s -> %s:%s - allowswap: %d - warp: %d", selws->name,
			selws->mon->name, ws->name, ws->mon->name, allowswap, diffmon);
	lastws = selws;
	lastmon = selmon;
	m = selws->mon;
	unfocus(selws->sel, 1);
	if (allowswap && m != ws->mon) {
		oldmon = ws->mon;
		selws->mon = ws->mon;
		if (ws->mon->ws == ws)
			ws->mon->ws = selws;
		ws->mon = m;
		m->ws = ws;
		updateviewports();
		relocatefloating(ws, oldmon);
		relocatefloating(lastws, selmon);
	}
	selws = ws;
	selmon = ws->mon;
	selws->mon->ws = ws;
	if (!allowswap && diffmon) {
		if (selws->stack)
			xcb_warp_pointer(con, root, root, 0, 0, 0, 0, selws->stack->x + (selws->stack->w / 2),
					selws->stack->y + (selws->stack->h / 2));
		else
			xcb_warp_pointer(con, root, root, 0, 0, 0, 0, ws->mon->x + (ws->mon->w / 2),
					ws->mon->y + (ws->mon->h / 2));
	}
	PROP_REPLACE(root, netatom[CurDesktop], XCB_ATOM_CARDINAL, 32, 1, &ws->num);
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
	int i, bw, f, u, ur, rel;

	f = border[Focus];
	u = border[Unfocus];
	ur = border[Urgent];
	while (*argv) {
		if (!strcmp(*argv, "smart")) {
			argv = parseint(argv + 1, NULL, &border[Smart], NULL, 1, 0);
		} else if (!strcmp(*argv, "colour") || !strcmp(*argv, "color")) {
			argv++;
			if (!strcmp(*argv, "reset")) {
				border[Focus] = dborder[Focus];
				border[Unfocus] = dborder[Unfocus];
				border[Urgent] = dborder[Urgent];
			} else {
				argv = parsecolor(argv, "focus", &border[Focus], 1);
				argv = parsecolor(argv, "unfocus", &border[Unfocus], 1);
				argv = parsecolor(argv, "urgent", &border[Urgent], 0);
			}
			if (f != border[Focus] || u != border[Unfocus] || ur != border[Urgent])
				FOR_CLIENTS(c, ws)
					xcb_change_window_attributes(con, c->win, XCB_CW_BORDER_PIXEL,
							&border[c == c->ws->sel ? Focus : (c->urgent ? Urgent : Unfocus)]);
		} else if (!strcmp(*argv, "width")) {
			argv++;
			bw = border[Width];
			if (!*argv) {
				fprintf(cmdresp, "!border width %s", enoargs);	
			} else if (!strcmp(*argv, "reset")) {
				bw = dborder[Width];
			} else {
				i = INT_MAX;
				argv = parseint(argv, NULL, &i, &rel, 1, 0);
				adjustsetting(i, rel, &bw, selws->gappx, 1);
			}
			if (bw != border[Width]) {
				FOR_CLIENTS(c, ws)
					if (c->bw && c->bw == border[Width])
						c->bw = bw;
				border[Width] = bw;
				layoutws(NULL);
			}
		} else {
			fprintf(cmdresp, "!invalid border command argument: %s", *argv);	
			return;
		}
		if (*argv)
			argv++;
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

	if (!selws->sel || (selws->sel->fullscreen && !selws->sel->ffs))
		return;
	if ((opt = parseopt(argv, moveopts, &i)) < 0 && i == INT_MAX) {
		fprintf(cmdresp, "!invalid argument for focus");
		return;
	} else if (opt == optlast) {
		focus(selws->sel->snext);
	} else {
		i = opt == -1 ? i : opt == optnext ? 1 : -1;
		movefocus(i);
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
	cmdview(num);
}

void cmdgappx(char **argv)
{
	int i, ng, rel;

	while (*argv) {
		if (!strcmp(*argv, "smart")) {
			argv = parseint(argv + 1, NULL, &gap[Smart], NULL, 1, 0);
		} else if (!strcmp(*argv, "width")) {
			argv++;
			ng = setws->gappx;
			if (!*argv)
				fprintf(cmdresp, "!gap width %s", enoargs);	
			else if (!strcmp(*argv, "reset"))
				ng = setws->defgap;
			else {
				i = INT_MAX;
				argv = parseint(argv, NULL, &i, &rel, 1, 0);
				adjustsetting(i, rel, &ng, border[Width], 1);
			}
			if (ng != setws->gappx) {
				setws->gappx = ng;
				if (setws->clients && setws == setws->mon->ws)
					layoutws(setws);
			}
		} else {
			fprintf(cmdresp, "!invalid gap command argument: %s", *argv);	
			return;
		}
		if (*argv)
			argv++;
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
	for (uint i = 0; i < LEN(layouts); i++)
		if (!strcmp(layouts[i].name, *argv)) {
			if (&layouts[i] != setws->layout) {
				setws->layout = &layouts[i];
				if (setws->clients && setws == setws->mon->ws)
					layoutws(setws);
			}
			return;
		}
}

void cmdmouse(char **argv)
{
	int arg;
	xcb_button_t *btn;

	while (*argv) {
		if (!strcmp("focus", *argv)) {
			argv++;
			argv = parseint(argv, NULL, &cfgfocusmouse, NULL, 1, 0);
		} else if (!strcmp("mod", *argv)) {
			argv++;
			if (!strcmp("alt", *argv) || !strcmp("mod1", *argv))
				mousemod = XCB_MOD_MASK_1;
			else if (!strcmp("super", *argv) || !strcmp("mod4", *argv))
				mousemod = XCB_MOD_MASK_4;
			else if (!strcmp("ctrl", *argv) || !strcmp("control", *argv))
				mousemod = XCB_MOD_MASK_CONTROL;
		} else if ((arg = !strcmp("move", *argv)) || !strcmp("resize", *argv)) {
			argv++;
			btn = arg ? &mousemove : &mouseresize;
			if (!strcmp("button1", *argv))
				*btn = XCB_BUTTON_INDEX_1;
			else if (!strcmp("button2", *argv))
				*btn = XCB_BUTTON_INDEX_2;
			else if (!strcmp("button3", *argv))
				*btn = XCB_BUTTON_INDEX_3;
		}
		if (*argv)
			argv++;
	}
	if (selws->sel)
		grabbuttons(selws->sel, 1);
}

void cmdresize(char **argv)
{
	int i;
	float f, *sf;
	Client *c, *t;
	int rel = 0, ohoff;
	int x = INT_MAX, y = INT_MAX, w = 0, h = 0;

	if (!(c = selws->sel) || (c->fullscreen && !c->ffs))
		return;
	while (*argv) {
		argv = parseint(argv, "x", &x, &rel, 1, 1);
		argv = parseint(argv, "y", &y, &rel, 1, 1);
		argv = parseint(argv, "w", &w, &rel, 0, 1);
		parseint(argv, "h", &h, &rel, 0, 0);
		if (x == INT_MAX && y == INT_MAX && w == 0 && h == 0) {
			fprintf(cmdresp, "!invalid argument for window move/resize command");
			return;
		} else if (*argv)
			argv++;
	}
	if (FLOATING(c)) {
		if (rel) {
			x = x == INT_MAX ? 0 : x;
			y = y == INT_MAX ? 0 : y;
			resizehint(c, c->x + x, c->y + y, c->w + w, c->h + h, c->bw, 1, 0);
		} else {
			x = x == INT_MAX ? c->x : x;
			y = y == INT_MAX ? c->y : y;
			w = w ? w : c->w;
			h = h ? h : c->h;
			resizehint(c, x, y, w, h, c->bw, 1, 0);
		}
	} else if (c->ws->layout->fn == tile) {
		if (y != INT_MAX)
			movestack(y > 0 ? 1 : -1);
		if (w) {
			sf = &c->ws->ssplit;
			for (i = 0, t = nextt(selws->clients); t; t = nextt(t->next), i++)
				if (t == c) {
					if (c->ws->nmaster && i < c->ws->nmaster + c->ws->nstack)
						sf = &c->ws->split;
					f = rel ? ((c->ws->mon->ww * *sf) + w) / c->ws->mon->ww : w / c->ws->mon->ww;
					if (f < 0.1 || f > 0.9)
						fprintf(cmdresp, "!window width exceeded limit: %f - f: %f", c->ws->mon->ww * f, f);
					else {
						*sf = f;
						layoutws(selws);
					}
					break;
				}
		}
		if (h) {
			ohoff = c->hoff;
			c->hoff += h;
			if (c->h + c->hoff < minwh || layoutws(selws) == -1) {
				fprintf(cmdresp, "!height adjustment for window exceeded limit: %d", c->hoff);
				c->hoff = ohoff;
			}
		}
	} else {
		fprintf(cmdresp, "!unable to move or resize windows in the %s layout", c->ws->layout->name);
		return;
	}
	eventignore(XCB_ENTER_NOTIFY);
}

void cmdnmaster(char **argv)
{
	int i = INT_MAX, rel = 1;

	parseint(argv, NULL, &i, &rel, 1, 0);
	adjustsetting(i, rel, &setws->nmaster, 0, 0);
}

void cmdnstack(char **argv)
{
	int i = INT_MAX, rel = 1;

	parseint(argv, NULL, &i, &rel, 1, 0);
	adjustsetting(i, rel, &setws->nstack, 0, 0);
}

void cmdpad(char **argv)
{
	if (!strcmp("print", *argv)) {
		fprintf(cmdresp, "%d %d %d %d", setws->padl, setws->padr, setws->padt, setws->padb);
	} else {
		argv = parseint(argv, "l", &setws->padl, NULL, 1, 1);
		argv = parseint(argv, "r", &setws->padr, NULL, 1, 1);
		argv = parseint(argv, "t", &setws->padt, NULL, 1, 1);
		parseint(argv, "b", &setws->padb, NULL, 1, 0);
		if (setws->clients && setws == setws->mon->ws)
			layoutws(setws);
	}
}

void cmdrule(char **argv)
{
	uint ui;
	int i, rem;
	Workspace *ws;
	WinRule *wr, r;

	r.cb = NULL;
	r.sticky = 0;
	r.floating = 0;
	r.x = -1;
	r.y = -1;
	r.w = -1;
	r.h = -1;
	r.ws = -1;
	r.bw = -1;
	r.class = NULL;
	r.inst = NULL;
	r.title = NULL;
	r.mon = NULL;
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
			r.mon = *argv;
		} else if (!strcmp(*argv, "ws")) {
			argv++;
			if ((i = strtol(*argv, NULL, 0)) <= numws && i > 0)
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
			if ((i = strtol(*argv, NULL, 0)) >= minwh)
				r.w = i;
		} else if (!strcmp(*argv, "h")) {
			argv++;
			if ((i = strtol(*argv, NULL, 0)) >= minwh)
				r.h = i;
		} else if (!strcmp(*argv, "bw")) {
			argv++;
			if ((i = strtol(*argv, NULL, 0)) || !strcmp(*argv, "0"))
				r.bw = i;
		} else if (!strcmp(*argv, "float")) {
			r.floating = 1;
		} else if (!strcmp(*argv, "stick")) {
			r.sticky = 1;
		} else {
			fprintf(cmdresp, "!invalid argument for rule command: %s", *argv);
		}
		if (*argv)
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
			wr->x = r.x;
			wr->y = r.y;
			wr->w = r.w;
			wr->h = r.h;
			wr->bw = r.bw;
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
	eventignore(XCB_ENTER_NOTIFY);
}

void cmdset(char **argv)
{
	Workspace *ws;
	int i = INT_MAX;
	setws = selws;

	if (!argv || !*argv) {
		fprintf(cmdresp, "!set %s", enoargs);
		return;
	} else while (*argv) {
		if (!strcmp("ws", *argv)) {
			argv = parseint(argv + 1, NULL, &i, NULL, 0, 0);
			if (i >= 1 && (ws = itows(i - 1))) {
				setws = ws;
			} else {
				fprintf(cmdresp, "!set ws index is out of range: %s", *argv);
				return;
			}
		} else if (!strcmp("numws", *argv)) {
			argv = parseint(argv + 1, NULL, &i, NULL, 0, 0);
			if (i > numws && i < 100)
				updatenumws(i);
		} else if (!strcmp("focus_urgent", *argv)) {
			argv = parseint(argv + 1, NULL, &cfgfocusurgent, NULL, 1, 0);
		} else if (!strcmp("tile_hints", *argv)) {
			argv = parseint(argv + 1, NULL, &cfgtsizehints, NULL, 1, 0);
		} else if (!strcmp("focus_mouse", *argv)) {
			argv = parseint(argv + 1, NULL, &cfgfocusmouse, NULL, 1, 0);
		} else {
			for (uint i = 0; i < LEN(setcmds); i++)
				if (!strcmp(setcmds[i].name, *argv)) {
					setcmds[i].fn(argv + 1);
					return;
				}
			fprintf(cmdresp, "!invalid argument for set command: %s", *argv);
		}
		if (*argv)
			argv++;
	}
}

void cmdsplit(char **argv)
{
	int rel = 1, s;
	float f = 0.0, nf, *sf = &setws->split;

	if (!setws->layout->fn)
		return;
	if ((s = !strcmp("stack", *argv)) || !strcmp("master", *argv)) {
		sf = s ? &setws->ssplit : &setws->split;
		argv++;
	}
	parsefloat(argv, NULL, &f, &rel, 0);
	if (f == 0.0)
		fprintf(cmdresp, "!invalid argument for split setting");
	else if (!rel && !(f -= *sf))
		fprintf(cmdresp, "!value argument for split setting exceeds limit");
	else if ((nf = f < 1.0 ? f + *sf : f - 1.0) < 0.1 || nf > 0.9)
		fprintf(cmdresp, "!value argument for split setting exceeds limit: %f", nf);
	else {
		*sf = nf;
		if (setws->clients && setws == setws->mon->ws)
			layoutws(setws);
	}
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

void cmdmon(char **argv)
{
	uint i;
	int opt;
	Monitor *m = NULL;
	void (*fn)(int) = cmdview;

	if (!monitors->next)
		return;
	if (!argv || !*argv) {
		fprintf(cmdresp, "!mon %s", enoargs);
	} else if (!strcmp("print", *argv)) {
		FOR_EACH(m, monitors)
			fprintf(cmdresp, "%d:%s %d:%s%s", m->num + 1, m->name, m->ws->num + 1,
					m->ws->name, m->next ? "\n" : "");
	} else {
		usemoncmd = 1;
		for (i = 0; i < LEN(wsmoncmds); i++)
			if (wsmoncmds[i].fn && !strcmp(wsmoncmds[i].name, *argv)) {
				fn = wsmoncmds[i].fn;
				argv++;
				break;
			}
		if (*argv) {
			if ((opt = parseopt(argv, moveopts, NULL)) >= 0) {
				if (opt == optlast)
					m = lastmon ? lastmon : selws->mon;
				else if (opt == optnext)
					m = selws->mon->next ? selws->mon->next : monitors;
				else
					FIND_PREV(m, selws->mon, monitors);
			} else if ((opt = strtol(*argv, NULL, 0)) > 0 && opt <= numws) {
				FOR_EACH(m, monitors)
					if (m->num == opt - 1)
						break;
			}
			if (m)
				fn(m->ws->num);
			else
				fprintf(cmdresp, "!invalid argument or monitor index out of range");
		} else
			fprintf(cmdresp, "!mon %s", enoargs);
	}
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
			wincmds[i].fn(r);
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

void cmdwsdef(char **argv)
{
	int i;
	float f;
	uint ui;
	Workspace *ws;
	static int apply = 1;

	while (*argv) {
		if (!strcmp(*argv, "layout")) {
			argv++;
			for (ui = 0; ui < LEN(layouts); ui++)
				if (!strcmp(layouts[ui].name, *argv)) {
					wsdef.layout = &layouts[ui];
					break;
				}
		} else if (!strcmp(*argv, "master")) {
			argv++;
			if ((i = strtol(*argv, NULL, 0)) > 0 || !strcmp(*argv, "0"))
				wsdef.nmaster = i;
		} else if (!strcmp(*argv, "stack")) {
			argv++;
			if ((i = strtol(*argv, NULL, 0)) > 0 || !strcmp(*argv, "0"))
				wsdef.nstack = i;
		} else if (!strcmp(*argv, "split")) {
			argv++;
			if ((f = strtof(*argv, NULL)) >= 0.1 && f <= 0.9)
				wsdef.split = f;
		} else if (!strcmp(*argv, "ssplit")) {
			argv++;
			if ((f = strtof(*argv, NULL)) >= 0.1 && f <= 0.9)
				wsdef.ssplit = f;
		} else if (!strcmp(*argv, "gap")) {
			argv++;
			if ((i = strtol(*argv, NULL, 0)) > 0 || !strcmp(*argv, "0"))
				wsdef.gappx = i;
		} else if (!strcmp(*argv, "pad")) {
			argv++;
			argv = parseint(argv, "l", &wsdef.padl, NULL, 1, 1);
			argv = parseint(argv, "r", &wsdef.padr, NULL, 1, 1);
			argv = parseint(argv, "t", &wsdef.padt, NULL, 1, 1);
			argv = parseint(argv, "b", &wsdef.padb, NULL, 1, 0);
		} else {
			fprintf(cmdresp, "!invalid argument for ws default command: %s", *argv);
			return;
		}
		if (*argv)
			argv++;
	}
	DBG("cmdwsdef: layout: %s, nmaster: %d, nstack: %d, gap: %d, split: %f,"
			" ssplit: %f, padl: %d, padr: %d, padt: %d, padb: %d", wsdef.layout->name,
			wsdef.nmaster, wsdef.nstack, wsdef.gappx, wsdef.split, wsdef.ssplit,
			wsdef.padl, wsdef.padr, wsdef.padt, wsdef.padb);
	if (apply) {
		DBG("cmdwsdef: applying new defaults to initial workspaces created before");
		FOR_EACH(ws, workspaces) {
			ws->gappx = wsdef.gappx;
			ws->defgap = wsdef.gappx;
			ws->layout = wsdef.layout;
			ws->nmaster = wsdef.nmaster;
			ws->nstack = wsdef.nstack;
			ws->split = wsdef.split;
			ws->ssplit = wsdef.ssplit;
			ws->padl = wsdef.padl;
			ws->padr = wsdef.padr;
			ws->padt = wsdef.padt;
			ws->padb = wsdef.padb;
		}
		apply = 0;
	}
}

void cmdws(char **argv)
{
	uint i;
	int opt;
	Workspace *ws = NULL;
	void (*fn)(int) = cmdview;

	if (!argv || !*argv) {
		fprintf(cmdresp, "!ws %s", enoargs);
	} else if (!strcmp("print", *argv)) {
		FOR_EACH(ws, workspaces)
			fprintf(cmdresp, "%d%s%s", ws->num + 1, ws == selws ? " *" : "", ws->next ? "\n" : "");
	} else {
		usemoncmd = 0;
		for (i = 0; i < LEN(wsmoncmds); i++)
			if (wsmoncmds[i].fn && !strcmp(wsmoncmds[i].name, *argv)) {
				fn = wsmoncmds[i].fn;
				argv++;
				break;
			}
		if (*argv) {
			if ((opt = parseopt(argv, moveopts, NULL)) >= 0) {
				if (opt == optlast)
					ws = lastws ? lastws : selws;
				else if (opt == optnext)
					ws = selws->next ? selws->next : workspaces;
				else
					FIND_PREV(ws, selws, workspaces);
			} else if ((opt = strtol(*argv, NULL, 0)) > 0 && opt <= numws)
				ws = itows(opt - 1);
			if (ws)
				fn(ws->num);
			else
				fprintf(cmdresp, "!invalid argument or workspace index out of range");
		} else
			fprintf(cmdresp, "!mon %s", enoargs);
	}
}

void cmdview(int num)
{
	Workspace *ws;

	if (num == selws->num || !(ws = itows(num)))
		return;
	if (!usemoncmd)
		changews(ws, 1, 0);
	else
		changews(ws, 0, 1);
	layoutws(NULL);
	focus(NULL);
	eventignore(XCB_ENTER_NOTIFY);
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
		if (x >= m->x && y >= m->y && x <= m->x + m->w && y <= m->y + m->h)
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

		if (e->mode == XCB_NOTIFY_MODE_GRAB || e->mode == XCB_NOTIFY_MODE_UNGRAB
				|| e->detail == XCB_NOTIFY_DETAIL_POINTER
				|| e->detail == XCB_NOTIFY_DETAIL_POINTER_ROOT
				|| e->detail == XCB_NOTIFY_DETAIL_NONE)
			return;
		DBG("eventhandle: FOCUS_IN");
		if (selws->sel && e->event != selws->sel->win)
			setinputfoocus(selws->sel);
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
				updateworkspaces(numws);
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
		xcb_flush(con);
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
			changews(ws, 0, 0);
			focus(NULL);
		}
		if (cfgfocusmouse && c && c != selws->sel)
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

		if (e->event != root || (e->time - lasttime) < 100)
			return;
		lasttime = e->time;
		if ((m = coordtomon(e->root_x, e->root_y)) && m != selws->mon) {
			DBG("eventhandle: MOTION_NOTIFY - updating active monitor");
			changews(m->ws, 0, 0);
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
			setwmwinstate(e->window, XCB_ICCCM_WM_STATE_WITHDRAWN);
		else
			freewin(e->window, 0);
		return;
	}
	case XCB_CLIENT_MESSAGE:
	{
		xcb_client_message_event_t *e = (xcb_client_message_event_t *)ev;
		uint32_t *d = e->data.data32;
		usemoncmd = 0;

		if (e->type == netatom[CurDesktop]) {
			DBG("CLIENT_MESSAGE: %s - data: %d", netatoms[CurDesktop], d[0]);
			cmdview(d[0]);
		} else if ((c = wintoclient(e->window))) {
			if (e->type == netatom[WmDesktop]) {
				DBG("CLIENT_MESSAGE: %s - 0x%08x - data: %d - sticky: %d",
						netatoms[WmDesktop], c->win, d[0], d[0] == STICKY);
				if (d[0] != STICKY)
					setclientws(c, d[0]);
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
				if (cfgfocusurgent) {
					unfocus(selws->sel, 1);
					cmdview(c->ws->num);
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

		if (ev->response_type && randrbase > 0 &&
				(ev->response_type == randrbase + XCB_RANDR_SCREEN_CHANGE_NOTIFY ||
				 ev->response_type == randrbase + XCB_RANDR_NOTIFY))
		{
			DBG("eventhandle: RANDR_NOTIFY or RANDR_SCREEN_CHANGE_NOTIFY");
			if (updaterandr() > 0 && monitors) {
				DBG("eventhandle: outputs changed after randr event");
				updateworkspaces(numws);
			}
		} else if (!ev->response_type && e && e->error_code != 3 && e->error_code != 5) {
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
						parsecmd(buf);
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
		if (!(home = getenv("XDG_CONFIG_HOME"))) {
			if (!(home = getenv("HOME")))
				return;
			strlcpy(path, home, sizeof(path));
			strlcat(path, "/.config", sizeof(path));
		} else {
			strlcpy(path, home, sizeof(path));
		}
		strlcat(path, "/yaxwm/yaxwmrc", sizeof(path));
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

void clientborder(Client *c, int focused)
{
	short w, h, b, o;
	xcb_gcontext_t gc;
	xcb_pixmap_t pmap;
	uint32_t values[1];

	if (!c)
		return;
	w = (short)c->w;
	h = (short)c->h;
	b = (unsigned short)c->bw;
	o = 1;
	xcb_rectangle_t inner[] = {
		{ w,         0,         b - o,     h + b - o },
		{ w + b + o, 0,         b - o,     h + b - o },
		{ 0,         h,         w + b - o, b - o     },
		{ 0,         h + b + o, w + b - o, b - o     },
		{ w + b + o, b + h + o, b,         b         }
	};
	xcb_rectangle_t outer[] = {
		{w + b - o, 0,         o,         h + b * 2 },
		{w + b,     0,         o,         h + b * 2 },
		{0,         h + b - o, w + b * 2, o         },
		{0,         h + b,     w + b * 2, o         },
		{1,         1,         1,         1         }
	};
	pmap = xcb_generate_id(con);
	xcb_create_pixmap(con, scr->root_depth, pmap, c->win, W(c), H(c));
	gc = xcb_generate_id(con);
	xcb_create_gc(con, gc, pmap, 0, NULL);
	values[0] = border[Unfocus];
	xcb_change_gc(con, gc, XCB_GC_FOREGROUND, values);
	xcb_poly_fill_rectangle(con, pmap, gc, 5, outer);
	values[0] = border[focused ? Focus : (c->urgent ? Urgent : Unfocus)];
	xcb_change_gc(con, gc, XCB_GC_FOREGROUND, values);
	xcb_poly_fill_rectangle(con, pmap, gc, 5, inner);
	values[0] = pmap;
	xcb_change_window_attributes(con, c->win, XCB_CW_BORDER_PIXMAP, values);
	xcb_free_pixmap(con, pmap);
	xcb_free_gc(con, gc);
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
		setinputfoocus(c);
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
		setwmwinstate(c->win, XCB_ICCCM_WM_STATE_WITHDRAWN);
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
		setwmwinstate(d->win, XCB_ICCCM_WM_STATE_WITHDRAWN);
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
		setwmwinstate(p->win, XCB_ICCCM_WM_STATE_WITHDRAWN);
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
	char fdstr[64];

	while (panels)
		freepanel(panels, 0);
	while (deskwins)
		freedeskwin(deskwins, 0);
	while (workspaces) {
		while (workspaces->stack)
			freeclient(workspaces->stack, 0);
		freews(workspaces);
	}
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
	uint i, j, mods[] = { 0, XCB_MOD_MASK_LOCK, 0, XCB_MOD_MASK_LOCK };

	numlockmask = 0;
	if ((m = xcb_get_modifier_mapping_reply(con, xcb_get_modifier_mapping(con), &e))) {
		if ((t = xcb_key_symbols_get_keycode(keysyms, nlock))
				&& (kc = xcb_get_modifier_mapping_keycodes(m)))
			for (i = 0; i < 8; i++)
				for (j = 0; j < m->keycodes_per_modifier; j++)
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
	for (i = 0; i < LEN(mods); i++) {
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
		c->x = c->trans->x + ((W(c->trans) - W(c)) / 2);
		c->y = c->trans->y + ((H(c->trans) - H(c)) / 2);
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
	setwmwinstate(c->win, XCB_ICCCM_WM_STATE_NORMAL);
	if (c->ws == c->ws->mon->ws || c->sticky) {
		if (c->ws == selws)
			unfocus(selws->sel, 0);
		c->ws->sel = c;
		layoutws(NULL);
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
	d->next = deskwins;
	deskwins = d;
	xcb_change_window_attributes(con, d->win, XCB_CW_EVENT_MASK, &m);
	d->x = d->mon->wx, d->y = d->mon->wy, d->w = d->mon->ww, d->h = d->mon->wh;
	MOVERESIZE(d->win, d->x, d->y, d->w, d->h, 0);
	setwmwinstate(d->win, XCB_ICCCM_WM_STATE_NORMAL);
	setstackmode(d->win, XCB_STACK_MODE_BELOW);
	PROP_APPEND(root, netatom[ClientList], XCB_ATOM_WINDOW, 32, 1, &d->win);
	xcb_map_window(con, d->win);
}

Monitor *initmon(int num, char *name, xcb_randr_output_t id, int x, int y, int w, int h)
{
	Monitor *m, *tail;

	DBG("initmon: %d:%s - %d,%d @ %dx%d", num, name, x, y, w, h);
	m = ecalloc(1, sizeof(Monitor));
	m->id = id;
	m->num = num;
	m->x = m->wx = x;
	m->y = m->wy = y;
	m->w = m->ww = w;
	m->h = m->wh = h;
	strlcpy(m->name, name, sizeof(m->name));
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
	p->next = panels;
	panels = p;
	xcb_change_window_attributes(con, p->win, XCB_CW_EVENT_MASK, &m);
	setwmwinstate(p->win, XCB_ICCCM_WM_STATE_NORMAL);
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

	DBG("initrandr: checking randr extension is available")
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
	uint8_t v = XCB_MAP_STATE_VIEWABLE;
	uint8_t ic = XCB_ICCCM_WM_STATE_ICONIC;

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

	wr = ecalloc(1, sizeof(WinRule));
	wr->x = r->x;
	wr->y = r->y;
	wr->w = r->w;
	wr->h = r->h;
	wr->ws = r->ws;
	wr->cb = r->cb;
	wr->bw = r->bw;
	wr->sticky = r->sticky;
	wr->floating = r->floating;
	if (r->mon) {
		len = strlen(r->mon) + 1;
		wr->mon = ecalloc(1, len);
		strlcpy(wr->mon, r->mon, len);
	}
	if (initwinrulereg(wr, r)) {
		wr->next = winrules;
		winrules = wr;
		DBG("initwinrule: class: %s inst: %s title: %s mon: %s ws: %d "
				"floating: %d sticky: %d position: %d,%d @ %d x %d",
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
	int r, cws, x, y;
	Workspace *ws;
	size_t len = 1;
	xcb_void_cookie_t ck;
	xcb_cursor_context_t *ctx;

	DBG("initwm: starting yaxwm set up")
	if ((randrbase = initrandr()) < 0 || !monitors)
		monitors = initmon(0, "default", 0, 0, 0, scr_w, scr_h);
	if (monitors->next && querypointer(&x, &y)) {
		DBG("initwm: scr_w: %d, scr_h: %d, ptr_x: %d, ptr_y: %d", scr_w, scr_h, x, y);
		if (x == scr_w / 2 || y == scr_h / 2)
			xcb_warp_pointer(con, root, root, 0, 0, 0, 0,
					primary->x + (primary->w / 2), primary->y + (primary->h / 2));
	}
	updatenumws(numws);
	selws = workspaces;
	selmon = selws->mon;
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
	if (cws >= numws)
		updatenumws(cws + 1);
	changews((ws = itows(cws)) ? ws : workspaces, 1, 0);
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

Workspace *initws(int num)
{
	Workspace *ws, *tail;

	DBG("initws: %d", num);
	ws = ecalloc(1, sizeof(Workspace));
	ws->num = num;
	itoa(num + 1, ws->name);
	ws->gappx = wsdef.gappx;
	ws->defgap = wsdef.gappx;
	ws->layout = wsdef.layout;
	ws->nmaster = wsdef.nmaster;
	ws->nstack = wsdef.nstack;
	ws->split = wsdef.split;
	ws->ssplit = wsdef.ssplit;
	ws->padl = wsdef.padl;
	ws->padr = wsdef.padr;
	ws->padt = wsdef.padt;
	ws->padb = wsdef.padb;
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
	int g, bw;
	int wx, wy, ww, wh;

	wx = ws->mon->wx + ws->padl;
	wy = ws->mon->wy + ws->padt;
	ww = ws->mon->ww - ws->padl - ws->padr;
	wh = ws->mon->wy - ws->padt - ws->padb;
	g = gap[Smart] ? 0 : ws->gappx;
	bw = border[Smart] ? 0 : border[Width];
	for (c = nextt(ws->clients); c; c = nextt(c->next))
		resizehint(c, wx + g, wy + g, ww - g, wh - g, bw, 0, 0);
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
				if (move && (m = coordtomon(x, y)) && m->ws != c->ws) {
					setclientws(c, m->ws->num);
					changews(m->ws, 0, 0);
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

Monitor *outputtomon(xcb_randr_output_t id)
{
	Monitor *m;

	FOR_EACH(m, monitors)
		if (m->id == id)
			return m;
	return m;
}

void parsecmd(char *buf)
{
	uint i, n = 0, matched = 0;
	char *argv[15], k[BUFSIZ], tok[BUFSIZ], args[15][BUFSIZ];

	DBG("parsecmd: tokenizing input buffer: %s", buf);
	if (strqetok(&buf, k, sizeof(k))) {
		for (i = 0; i < LEN(keywords); i++)
			if ((matched = !strcmp(keywords[i].name, k))) {
				while (n + 1 < LEN(args) && buf && *buf && strqetok(&buf, tok, sizeof(tok))) {
					strlcpy(args[n], tok, sizeof(args[n]));
					argv[n] = args[n];
					DBG("parsecmd: parsed token: argv[%d] = %s", n, argv[n]);
					n++;
				}
				argv[n] = NULL;
				if (*argv)
					keywords[i].fn(argv);
				else
					fprintf(cmdresp, "!%s %s", k, enoargs);
				break;
			}
		if (!matched)
			fprintf(cmdresp, "!invalid or unknown command: %s", k);
	}
	fflush(cmdresp);
	fclose(cmdresp);
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

void relocatefloating(Workspace *ws, Monitor *old)
{
	Client *c;
	Monitor *m;
	int xoff, yoff;
	float xdiv, ydiv;

	FOR_EACH(c, ws->clients)
		if (FLOATING(c) && (m = ws->mon)) {
			if ((xoff = c->x - old->x) && (xdiv = old->w / xoff) != 0.0)
				c->x = CLAMP(m->x + (m->w / xdiv), ws->mon->x - (W(c) - minxy), m->wx + m->ww - minxy);
			else
				c->x = CLAMP(c->x, ws->mon->x - (W(c) - minxy), m->wx + m->ww - minxy);
			if ((yoff = c->y - old->y) && (ydiv = old->h / yoff) != 0.0)
				c->y = CLAMP(m->y + (m->h / ydiv), ws->mon->y - (H(c) - minxy), m->wy + m->wh - minxy);
			else
				c->y = CLAMP(c->y, ws->mon->y - (H(c) - minxy), m->wy + m->wh - minxy);
		}
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

void setclientws(Client *c, int num)
{
	DBG("setclientws: 0x%08x -> %d", c->win, num);
	if (c->ws) {
		detach(c, 0);
		detachstack(c);
	}
	if (!(c->ws = itows(num)))
		c->ws = selws;
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
		c->x = c->old_x; c->y = c->old_y;
		c->w = c->old_w; c->h = c->old_h;
		resize(c, c->x, c->y, c->w, c->h, c->bw);
		layoutws(c->ws);
	}
}

void setinputfoocus(Client *c)
{
	if (!c->nofocus) {
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, c->win, XCB_CURRENT_TIME);
		PROP_REPLACE(root, netatom[Active], XCB_ATOM_WINDOW, 32, 1, &c->win);
	}
	sendwmproto(c, TakeFocus);
}

void setsticky(Client *c, int sticky)
{
	int d = c->ws->num;

	if (sticky && !c->sticky)
		c->sticky = 1, d = STICKY;
	else if (!sticky && c->sticky)
		c->sticky = 0;
	PROP_REPLACE(c->win, netatom[WmDesktop], XCB_ATOM_CARDINAL, 32, 1, &d);
}

void setstackmode(xcb_window_t win, uint mode)
{
	xcb_configure_window(con, win, XCB_CONFIG_WINDOW_STACK_MODE, &mode);
}

void setwmwinstate(xcb_window_t win, uint32_t state)
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
		if (uss && s.flags & XCB_ICCCM_SIZE_HINT_US_SIZE) {
			DBG("sizehints: user specified size: %dx%d -> %dx%d",
					c->w, c->h, s.width, s.height);
			c->w = s.width, c->h = s.height;
		}
		if (uss && s.flags & XCB_ICCCM_SIZE_HINT_US_POSITION) {
			DBG("sizehints: user specified position: %d,%d -> %d,%d",
					c->x, c->y, s.x - c->bw, s.y - c->bw);
			c->x = s.x, c->y = s.y;
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

int tileresize(Client *c, Client *p, int ww, int wh, int x, int y,
		int w, int h, int bw, int gap, int *newy, int nrem, int avail)
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
	int i, n, nr, my, sy, ssy, bw, g;
	int wx, wy, ww, wh, mw, ss, sw, ssw, ns;

	for (n = 0, c = nextt(ws->clients); c; c = nextt(c->next), n++)
		;
	if (!n)
		return 1;

	wx = m->wx + ws->padl;
	wy = m->wy + ws->padt;
	ww = m->ww - ws->padl - ws->padr;
	wh = m->wh - ws->padt - ws->padb;
	mw = 0, ss = 0, sw = 0, ssw = 0, ns = 1;
	g = gap[Smart] && n == 1 ? 0 : ws->gappx;
	bw = border[Smart] && n == 1 ? 0 : border[Width];
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
	for (i = 0, my = sy = ssy = g, c = nextt(ws->clients); c; c = nextt(c->next), ++i) {
		if (i < ws->nmaster) {
			nr = MIN(n, ws->nmaster) - i;
			h = ((wh - my) / MAX(1, nr)) - g + c->hoff;
			w = mw - g * (5 - ns) / 2;
			if (tileresize(c, prev, ww - (2 * g), wh - (2 * g), wx + g,
						wy + my, w, h, bw, g, &my, nr, wh - (my + h + g)) < 0)
				ret = -1;
		} else if (i - ws->nmaster < ws->nstack) {
			nr = MIN(n - ws->nmaster, ws->nstack) - (i - ws->nmaster);
			h = ((wh - sy) / MAX(1, nr)) - g + c->hoff;
			w = sw - g * (5 - ns - ss) / 2;
			if (ws->nmaster > 0 && i == ws->nmaster)
				prev = NULL;
			if (tileresize(c, prev, ww - (2 * g), wh - (2 * g), wx + mw + (g / ns),
						wy + sy, w, h, bw, g, &sy, nr, wh - (sy + h + g)) < 0)
				ret = -1;
		} else {
			h = ((wh - ssy) / MAX(1, n - i)) - g + c->hoff;
			w = ssw - g * (5 - ns) / 2;
			if (ws->nstack + ws->nmaster > 0 && i == ws->nmaster + ws->nstack)
				prev = NULL;
			if (tileresize(c, prev, ww - (2 * g), wh - (2 * g), wx + mw + sw + (g / ns),
						wy + ssy, w, h, bw, g, &ssy, n - i, wh - (ssy + h + g)) < 0)
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
	FOR_EACH(p, panels)
		PROP_APPEND(root, netatom[ClientList], XCB_ATOM_WINDOW, 32, 1, &p->win);
	FOR_EACH(d, deskwins)
		PROP_APPEND(root, netatom[ClientList], XCB_ATOM_WINDOW, 32, 1, &d->win);
}

void updatenumws(int needed)
{
	int n = 0;
	Workspace *ws;
	Monitor *m = NULL;

	FOR_EACH(m, monitors)
		n++;
	if (n > 999)
		errx(1, "attempting to allocate too many workspaces");
	else while (!numws || n > numws || needed > numws) {
		initws(numws);
		numws++;
	}
	FOR_EACH(ws, workspaces) {
		if (!m)
			m = monitors;
		if (m && !m->ws)
			m->ws = ws;
		ws->mon = m;
		DBG("updatenumws: %d:%s -> %s - visible: %d", ws->num, ws->name, m->name, ws == m->ws);
		if (m)
			m = m->next;
	}
	PROP_REPLACE(root, netatom[NumDesktops], XCB_ATOM_CARDINAL, 32, 1, &numws);
	updateviewports();
}

int updateoutputs(xcb_randr_output_t *outs, int len, xcb_timestamp_t timestamp)
{
	uint n;
	Monitor *m;
	char name[64];
	int i, nmons, changed = 0;
	xcb_generic_error_t *e;
	xcb_randr_get_crtc_info_cookie_t ck;
	xcb_randr_get_output_info_reply_t *o;
	xcb_randr_get_crtc_info_reply_t *crtc;
	xcb_randr_get_output_info_cookie_t oc[len];
	xcb_randr_get_output_primary_reply_t *po = NULL;
	uint8_t disconnected = XCB_RANDR_CONNECTION_DISCONNECTED;

	DBG("updateoutputs: received %d randr outputs", len);
	for (i = 0; i < len; i++)
		oc[i] = xcb_randr_get_output_info(con, outs[i], timestamp);
	for (i = 0, nmons = 0; i < len; i++) {
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
				initmon(nmons++, name, outs[i], crtc->x, crtc->y, crtc->width, crtc->height);
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
	po = xcb_randr_get_output_primary_reply(con,
			xcb_randr_get_output_primary(con, root), NULL);
	if (!(primary = outputtomon(po->output)) && monitors) {
		primary = monitors;
		xcb_randr_set_output_primary(con, root, primary->id);
	}
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
		if (!ws->mon)
			ws->mon = primary;
		v[0] = ws->mon->x, v[1] = ws->mon->y;
		PROP_APPEND(root, netatom[Viewport], XCB_ATOM_CARDINAL, 32, 2, &v);
	}
}

void updateworkspaces(int needed)
{
	Panel *p;
	Client *c;
	DeskWin *d;
	Workspace *ws;

	updatenumws(needed);
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
	eventignore(XCB_ENTER_NOTIFY);
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

	if (win == root) {
		if (querypointer(&x, &y) && (m = coordtomon(x, y)))
			return m->ws;
	} else FOR_CLIENTS(c, ws)
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
