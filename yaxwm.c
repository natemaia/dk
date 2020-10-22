/* yet another X window manager
* see license file for copyright and license details
* vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
*/

#define _XOPEN_SOURCE 700

#include <sys/un.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <poll.h>
#include <stdio.h>
#include <regex.h>
#include <unistd.h>
#include <limits.h>
#include <locale.h>

#include <xcb/randr.h>
#include <xcb/xproto.h>
#include <xcb/xcb_util.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_cursor.h>
#include <xcb/xcb_keysyms.h>

#include "include/strl.c"
#include "include/util.c"

#ifdef DEBUG
#define DBG(fmt, ...) warnx("%d: " fmt, __LINE__, ##__VA_ARGS__);
#else
#define DBG(fmt, ...)
#endif

#ifndef VERSION
#define VERSION "0.84"
#endif

#define W(c) (c->w + (2 * c->bw))
#define H(c) (c->h + (2 * c->bw))
#define LEN(x) (sizeof(x) / sizeof(x[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(x, min, max) (MIN(MAX((x), (min)), (max)))
#define CLNMOD(mod) ((mod) & ~(lockmask | XCB_MOD_MASK_LOCK))
#define SAVEOLD(c) c->old_x = c->x, c->old_y = c->y, c->old_w = c->w, c->old_h = c->h

#define FLOATING(c) (c->state & STATE_FLOATING || !c->ws->layout->func)
#define FULLSCREEN(c) (c->state & STATE_FULLSCREEN && !(c->state & STATE_FAKEFULL))

#define FOR_EACH(v, list) for (v = list; v; v = v->next)
#define FOR_CLIENTS(c, ws) FOR_EACH(ws, workspaces) FOR_EACH(c, ws->clients)

#define FIND_TAIL(v, list) for (v = list; v && v->next; v = v->next)
#define FIND_PREV(v, cur, list) for (v = list; v && v->next && v->next != cur; v = v->next)

#define ATTACH(v, list) do { v->next = list; list = v; } while (0)
#define DETACH(v, listptr)                    \
	do {                                      \
		while (*(listptr) && *(listptr) != v) \
			(listptr) = &(*(listptr))->next;  \
		*(listptr) = v->next;                 \
	} while (0)

#define PROP(mode, win, atom, type, membsize, nmemb, value) \
	xcb_change_property(con, XCB_PROP_MODE_##mode, win, atom, type, \
			(membsize), (nmemb), value)
#define MOVE(win, x, y)                                                       \
	xcb_configure_window(con, win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, \
			(unsigned int[]){(x), (y)})
#define MOVERESIZE(win, x, y, w, h, bw)                                 \
	xcb_configure_window(con, win,                                      \
			XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y                   \
			| XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT        \
			| XCB_CONFIG_WINDOW_BORDER_WIDTH,                           \
			(unsigned int[]){(x), (y), MAX((w), globalcfg[GLB_MIN_WH]), \
			MAX((h), globalcfg[GLB_MIN_WH]), (bw)})
#define CMOVERESIZE(c, x, y, w, h, bw)            \
	MOVERESIZE(c->win, (x), (y), (w), (h), (bw)); \
	drawborder(c, c == selws->sel);               \
	sendconfigure(c)


enum States {
	STATE_NONE         = 0,
	STATE_FAKEFULL     = 1 << 0,
	STATE_FIXED        = 1 << 1,
	STATE_FLOATING     = 1 << 2,
	STATE_FULLSCREEN   = 1 << 3,
	STATE_NOBORDER     = 1 << 4,
	STATE_NOINPUT      = 1 << 5,
	STATE_STICKY       = 1 << 6,
	STATE_URGENT       = 1 << 7,
	STATE_NEEDSMAP     = 1 << 8,
	STATE_NEEDSRESIZE  = 1 << 9,
	STATE_WASFLOATING  = 1 << 10,
};

enum Cursors {
	CURS_MOVE   = 0,
	CURS_NORMAL = 1,
	CURS_RESIZE = 2,
	CURS_LAST   = 3,
};

enum Gravity {
	GRAV_NONE   = 0,
	GRAV_LEFT   = 1,
	GRAV_RIGHT  = 2,
	GRAV_CENTER = 3,
	GRAV_TOP    = 4,
	GRAV_BOTTOM = 5,
};

enum Borders {
	BORD_WIDTH     = 0,
	BORD_FOCUS     = 1,
	BORD_URGENT    = 2,
	BORD_UNFOCUS   = 3,
	BORD_O_WIDTH   = 4,
	BORD_O_FOCUS   = 5,
	BORD_O_URGENT  = 6,
	BORD_O_UNFOCUS = 7,
};

enum DirOpts {
	DIR_NEXT          = 0,
	DIR_PREV          = 1,
	DIR_LAST          = 2,
	DIR_NEXT_NONEMPTY = 3,
	DIR_PREV_NONEMPTY = 4,
};

enum WMAtoms {
	WM_DELETE  = 0,
	WM_FOCUS   = 1,
	WM_MOTIF   = 2,
	WM_PROTO   = 3,
	WM_STATE   = 4,
	WM_UTF8STR = 5,
};

enum NetAtoms {
	NET_ACTIVE      = 0,
	NET_CLIENTS     = 1,
	NET_CLOSE       = 2,
	NET_DESK_CUR    = 3,
	NET_DESK_GEOM   = 4,
	NET_DESK_NAMES  = 5,
	NET_DESK_NUM    = 6,
	NET_DESK_VP     = 7,
	NET_DESK_WA     = 8,
	NET_STATE_FULL  = 9,
	NET_SUPPORTED   = 10,
	NET_TYPE_DESK   = 11,
	NET_TYPE_DIALOG = 12,
	NET_TYPE_DOCK   = 13,
	NET_TYPE_SPLASH = 14,
	NET_WM_CHECK    = 15,
	NET_WM_DESK     = 16,
	NET_WM_NAME     = 17,
	NET_WM_STATE    = 18,
	NET_WM_STRUT    = 19,
	NET_WM_STRUTP   = 20,
	NET_WM_TYPE     = 21,
};

enum GlobalCfg {
	GLB_FOCUS_MOUSE  = 0,
	GLB_FOCUS_OPEN   = 1,
	GLB_FOCUS_URGENT = 2,
	GLB_MIN_WH       = 3,
	GLB_MIN_XY       = 4,
	GLB_NUMWS        = 5,
	GLB_TILEHINTS    = 6,
	GLB_SMART_BORDER = 7,
	GLB_SMART_GAP    = 8,
	GLB_TILETOHEAD   = 9,
	GLB_STATICWS     = 10,
};


static char *opts[] = {
	[DIR_NEXT] = "next", [DIR_PREV] = "prev", [DIR_LAST] = "last",
	[DIR_NEXT_NONEMPTY] = "nextne", [DIR_PREV_NONEMPTY] = "prevne", NULL
};

static const char *gravities[] = {
	[GRAV_NONE] = "none",     [GRAV_LEFT] = "left", [GRAV_RIGHT] = "right",
	[GRAV_CENTER] = "center", [GRAV_TOP] = "top",   [GRAV_BOTTOM] = "bottom",
};

static const char *wmatoms[] = {
	[WM_DELETE] = "WM_DELETE_WINDOW", [WM_FOCUS] = "WM_TAKE_FOCUS",
	[WM_MOTIF] = "_MOTIF_WM_HINTS",   [WM_PROTO] = "WM_PROTOCOLS",
	[WM_STATE] = "WM_STATE",          [WM_UTF8STR] = "UTF8_STRING",
};

static const char *netatoms[] = {
	[NET_ACTIVE] = "_NET_ACTIVE_WINDOW",
	[NET_CLIENTS] = "_NET_CLIENT_LIST",
	[NET_CLOSE] = "_NET_CLOSE_WINDOW",
	[NET_DESK_CUR] = "_NET_CURRENT_DESKTOP",
	[NET_DESK_GEOM] = "_NET_DESKTOP_GEOMETRY",
	[NET_DESK_NAMES] = "_NET_DESKTOP_NAMES",
	[NET_DESK_NUM] = "_NET_NUMBER_OF_DESKTOPS",
	[NET_DESK_VP] = "_NET_DESKTOP_VIEWPORT",
	[NET_DESK_WA] = "_NET_WORKAREA",
	[NET_STATE_FULL] = "_NET_WM_STATE_FULLSCREEN",
	[NET_SUPPORTED] = "_NET_SUPPORTED",
	[NET_TYPE_DESK] = "_NET_WM_WINDOW_TYPE_DESKTOP",
	[NET_TYPE_DIALOG] = "_NET_WM_WINDOW_TYPE_DIALOG",
	[NET_TYPE_DOCK] = "_NET_WM_WINDOW_TYPE_DOCK",
	[NET_TYPE_SPLASH] = "_NET_WM_WINDOW_TYPE_SPLASH",
	[NET_WM_CHECK] = "_NET_SUPPORTING_WM_CHECK",
	[NET_WM_DESK] = "_NET_WM_DESKTOP",
	[NET_WM_NAME] = "_NET_WM_NAME",
	[NET_WM_STATE] = "_NET_WM_STATE",
	[NET_WM_STRUTP] = "_NET_WM_STRUT_PARTIAL",
	[NET_WM_STRUT] = "_NET_WM_STRUT",
	[NET_WM_TYPE] = "_NET_WM_WINDOW_TYPE",
};


typedef struct Callback Callback;
typedef struct Workspace Workspace;

typedef struct Monitor {
	char name[64];
	int num, connected;
	int x, y, w, h;
	int wx, wy, ww, wh;
	xcb_randr_output_t id;
	struct Monitor *next;
	Workspace *ws;
} Monitor;

typedef struct Desk {
	int x, y, w, h;
	unsigned int state;
	xcb_window_t win;
	struct Desk *next;
	Monitor *mon;
} Desk;

typedef struct Rule {
	int x, y, w, h, bw;
	int xgrav, ygrav;
	int ws, focus;
	unsigned int state;
	char *title, *class, *inst, *mon;
	const Callback *cb;
	regex_t titlereg, classreg, instreg;
	struct Rule *next;
} Rule;

typedef struct Panel {
	int x, y, w, h;
	int l, r, t, b; /* struts */
	unsigned int state;
	xcb_window_t win;
	struct Panel *next;
	Monitor *mon;
} Panel;

typedef struct Client {
	char title[NAME_MAX], class[64], inst[64];
	int x, y, w, h, bw, hoff, depth;
	int old_x, old_y, old_w, old_h, old_bw;
	int max_w, max_h, min_w, min_h;
	int base_w, base_h, inc_w, inc_h;
	float min_aspect, max_aspect;
	unsigned int state, old_state;
	xcb_window_t win;
	struct Client *trans, *next, *snext;
	Workspace *ws;
	const Callback *cb;
} Client;

typedef struct VoidCmd {
	const char *str;
	void (*func)(char **);
} VoidCmd;

typedef struct WsCmd {
	const char *str;
	void (*func)(Workspace *);
} WsCmd;

typedef struct Layout {
	const char *name;
	int (*func)(Workspace *);
} Layout;

struct Callback {
	const char *name;
	void (*func)(Client *, int);
};

struct Workspace {
	int nmaster, nstack, gappx;
	int padr, padl, padt, padb;
	float msplit, ssplit;
	const Layout *layout;
	int num;
	char name[64];
	Monitor *mon;
	Workspace *next;
	Client *sel, *stack, *clients;
};


static void cmdborder(char **argv);
static void cmdcycle(char **argv);
static void cmdfakefull(char **argv);
static void cmdfloat(char **argv);
static void cmdfocus(char **argv);
static void cmdfollow(Workspace *ws);
static void cmdfull(char **argv);
static void cmdgappx(char **argv);
static void cmdkill(char **argv);
static void cmdlayout(char **argv);
static void cmdmon(char **argv);
static void cmdmouse(char **argv);
static void cmdmsplit(char **argv);
static void cmdnmaster(char **argv);
static void cmdnstack(char **argv);
static void cmdpad(char **argv);
static void cmdresize(char **argv);
static void cmdrule(char **argv);
static void cmdsend(Workspace *ws);
static void cmdset(char **argv);
static void cmdssplit(char **argv);
static void cmdstick(char **argv);
static void cmdswap(char **argv);
static void cmdview(Workspace *ws);
static void cmdwin(char **argv);
static void cmdwm(char **argv);
static void cmdws(char **argv);
static void cmdwsdef(char **argv);
static void detach(Client *c, int reattach);
static void drawborder(Client *c, int focused);
static int dwindle(Workspace *ws);
static void eventhandle(xcb_generic_event_t *ev);
static void eventignore(uint8_t type);
static void execcfg(void);
static int fib(Workspace *ws, int out);
static void focus(Client *c);
static void freemon(Monitor *m);
static void freerule(Rule *r);
static void freewm(void);
static void freews(Workspace *ws);
static void grabbuttons(Client *c, int focused);
static void gravitate(Client *c, int horz, int vert, int matchgap);
static int grid(Workspace *ws);
static int iferr(int lvl, char *msg, xcb_generic_error_t *e);
static Rule *initrule(Rule *wr);
static void initscan(void);
static void initsock(void);
static void initwm(void);
static char *itoa(int n, char *s);
static Monitor *itomon(int num);
static Workspace *itows(int num);
static void manage(xcb_window_t win, xcb_get_geometry_reply_t *g, xcb_get_window_attributes_reply_t *wa);
static int mono(Workspace *ws);
static void movefocus(int direction);
static void movestack(int direction);
static Monitor *nextmon(Monitor *m);
static Client *nexttiled(Client *c);
static Monitor *outputtomon(xcb_randr_output_t id);
static void popfloat(Client *c);
static void pushstatus(void);
static void quadrant(Client *c, int *x, int *y, int *w, int *h);
static void refresh(void);
static void relocate(Client *c, Monitor *new, Monitor *old);
static void relocatews(Workspace *ws, Monitor *old);
static void resize(Client *c, int x, int y, int w, int h, int bw);
static void resizehint(Client *c, int x, int y, int w, int h, int bw, int usermotion, int mouse);
static void restack(Workspace *ws);
static int rulecmp(Client *c, Rule *r);
static void sendconfigure(Client *c);
static int sendwmproto(Client *c, int wmproto);
static void setfullscreen(Client *c, int fullscreen);
static void setinputfocus(Client *c);
static void setnetwsnames(void);
static void setstackmode(xcb_window_t win, unsigned int mode);
static void seturgent(Client *c, int urg);
static void setwmwinstate(xcb_window_t win, long state);
static void setworkspace(Client *c, int num, int stacktail);
static void showhide(Client *c);
static void sighandle(int sig);
static void sizehints(Client *c, int uss);
static int spiral(Workspace *ws);
static int tile(Workspace *ws);
static void unfocus(Client *c, int focusroot);
static void unmanage(xcb_window_t win, int destroyed);
static void updnetworkspaces(void);
static int updoutputs(xcb_randr_output_t *outs, int nouts, xcb_timestamp_t t);
static int updrandr(void);
static void updstruts(Panel *p, int apply);
static void updworkspaces(int needed);
static int usage(int e, char flag);
static xcb_get_window_attributes_reply_t *winattr(xcb_window_t win);
static xcb_get_geometry_reply_t *wingeom(xcb_window_t win);
static int winprop(xcb_window_t win, xcb_atom_t prop, xcb_atom_t *ret);
static Client *wintoclient(xcb_window_t win);
static Desk *wintodesk(xcb_window_t win);
static Panel *wintopanel(xcb_window_t win);
static xcb_window_t wintrans(xcb_window_t win);


extern char **environ;
static FILE *cmdresp;
static unsigned int lockmask = 0;
static char *argv0, sock[NAME_MAX], status[NAME_MAX];
static int scr_h, scr_w, sockfd, running, restart, randrbase, cmdusemon, needsrefresh;
static const char *ebadarg = "invalid argument for";
static const char *enoargs = "command requires additional arguments but none were given";

static Desk *desks;
static Rule *rules;
static Panel *panels;
static Client *cmdclient;
static Monitor *primary, *monitors, *selmon, *lastmon;
static Workspace *setws, *selws, *lastws, *workspaces;

static xcb_screen_t *scr;
static xcb_connection_t *con;
static xcb_window_t root, wmcheck;
static xcb_key_symbols_t *keysyms;
static xcb_cursor_t cursor[CURS_LAST];
static xcb_atom_t wmatom[LEN(wmatoms)], netatom[LEN(netatoms)];

#include "yaxwm.h"
#include "include/parse.c"


int main(int argc, char *argv[])
{
	ssize_t n;
	Client *c = NULL;
	fd_set read_fds;
	xcb_window_t sel;
	struct timeval tv;
	xcb_generic_event_t *ev;
	char *end, buf[PIPE_BUF];
	int confd, nfds, cmdfd;

	argv0 = argv[0];
	randrbase = -1;
	running = needsrefresh = 1;
	sockfd = restart = cmdusemon = 0;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-s")) {
			if (!(sockfd = strtol(argv[++i], &end, 0)) || *end != '\0') {
				warnx("invalid socket file descriptor: %s", argv[i]);
				sockfd = 0;
			}
		} else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "-h")) {
			return usage(0, argv[i][1]);
		} else {
			return usage(1, 'h');
		}
	}

	if (!setlocale(LC_ALL, ""))
		err(1, "no locale support");

	if (xcb_connection_has_error((con = xcb_connect(NULL, NULL))))
		err(1, "error connecting to X");
	atexit(freewm);

	if (!(scr = xcb_setup_roots_iterator(xcb_get_setup(con)).data))
		errx(1, "error getting default screen from X connection");
	root = scr->root;
	scr_w = scr->width_in_pixels;
	scr_h = scr->height_in_pixels;
	iferr(1, "is another window manager running?",
			xcb_request_check(con,
				xcb_change_window_attributes_checked(con, root, XCB_CW_EVENT_MASK,
					(unsigned int[]){XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT})));
	initwm();
	initsock();
	initscan();
	execcfg();

	if (winprop(root, netatom[NET_ACTIVE], &sel) && (c = wintoclient(sel))) {
		focus(c);
		xcb_warp_pointer(con, root, root, 0, 0, 0, 0, c->x + (c->w / 2), c->y + (c->h / 2));
	} else if (nextmon(monitors->next) && primary) {
		xcb_warp_pointer(con, root, root, 0, 0, 0, 0, primary->x + (primary->w / 2),
				primary->y + (primary->h / 2));
	}

	confd = xcb_get_file_descriptor(con);
	nfds = MAX(confd, sockfd) + 1;
	while (running) {
		if (xcb_connection_has_error(con))
			break;
		tv.tv_sec = 2;
		tv.tv_usec = 0;
		xcb_flush(con);
		if (needsrefresh) {
			refresh();
			needsrefresh = 0;
		}
		FD_ZERO(&read_fds);
		FD_SET(sockfd, &read_fds);
		FD_SET(confd, &read_fds);
		if (select(nfds, &read_fds, NULL, NULL, &tv) > 0) {
			if (FD_ISSET(sockfd, &read_fds)) {
				cmdfd = accept(sockfd, NULL, 0);
				if (cmdfd > 0 && (n = recv(cmdfd, buf, sizeof(buf) - 1, 0)) > 0) {
					if (buf[n - 1] == '\n')
						n--;
					buf[n] = '\0';
					if ((cmdresp = fdopen(cmdfd, "w"))) {
						parsecmd(buf);
					} else {
						warn("unable to open the socket as file: %s", sock);
						close(cmdfd);
					}
				}
			}
			if (FD_ISSET(confd, &read_fds))
				while ((ev = xcb_poll_for_event(con))) {
					eventhandle(ev);
					free(ev);
				}
		}
	}
	return 0;
}

void adjustfsetting(float f, int relative, float *setting)
{
	float nf;

	if (f == 0.0 || !setws->layout->func || (!relative && !(f -= *setting)))
		return;
	if ((nf = CLAMP(f < 1.0 ? f + *setting : f - 1.0, 0.05, 0.95)) != *setting)
		*setting = nf;
}

void adjustisetting(int i, int relative, int *setting, int other, int setbordergap)
{
	int n = INT_MAX;
	int max = setws->mon->wh - setws->padb - setws->padt;

	if (i == INT_MAX || (!relative && !(i -= *setting)))
		return;
	n = CLAMP(*setting + i, 0, setbordergap ? (max / 6) - other : max / globalcfg[GLB_MIN_WH]);
	if (n != *setting)
		*setting = n;
}

void adjustwsormon(char **argv)
{
	int opt, e = 0;
	void (*fn)(Workspace *);
	Monitor *m = NULL, *cm;
	Workspace *ws = NULL, *cur;

	fn = cmdview;
	cur = selws;
	cm = cur->mon;
	cmdclient = selws->sel;

	if (*argv) {
		for (unsigned int i = 0; i < LEN(wscmds); i++)
			if (!strcmp(wscmds[i].str, *argv)) {
				fn = wscmds[i].func;
				argv++;
				break;
			}
		if (fn != cmdview && (cmdclient = parseclient(*argv, &e))) {
			cur = cmdclient->ws;
			cm = cur->mon;
			argv++;
		} else if (e == -1) {
			return;
		} else {
			cmdclient = selws->sel;
		}
	}

	if (!*argv) {
		fprintf(cmdresp, "!%s %s\n", cmdusemon ? "mon" : "ws", enoargs);
		return;
	}

	if ((opt = parseopt(*argv, opts)) >= 0) {
		if (opt == DIR_LAST) {
			ws = cmdusemon
				? (lastmon && lastmon->connected ? lastmon->ws : cur)
				: lastws ? lastws : cur;
		} else if (opt == DIR_NEXT && cmdusemon) {
			if (!(m = nextmon(cm->next)))
				m = nextmon(monitors);
			ws = m->ws;
		} else if (opt == DIR_NEXT) {
			ws = cur->next ? cur->next : workspaces;
		} else if (cmdusemon && opt == DIR_PREV) {
			for (m = nextmon(monitors); m && nextmon(m->next)
					&& nextmon(m->next) != cm; m = nextmon(m->next))
				;
			ws = m ? m->ws : selws;
		} else if (opt == DIR_PREV) {
			FIND_PREV(ws, cur, workspaces);
		} else {
			int r = 0;
			Workspace *save = cur;
			while (!ws && r < globalcfg[GLB_NUMWS]) {
				if (opt == DIR_NEXT_NONEMPTY) {
					if (cmdusemon) {
						if (!(m = nextmon(cm)))
							m = nextmon(monitors);
						ws = m->ws;
					} else
						ws = cur->next ? cur->next : workspaces;
				} else if (cmdusemon) {
					for (m = nextmon(monitors); m && nextmon(m->next)
							&& nextmon(m->next) != cm; m = nextmon(m->next))
						;
					ws = m ? m->ws : selws;
				} else {
					FIND_PREV(ws, cur, workspaces);
				}
				cur = ws;
				cm = ws->mon;
				if (!ws->clients && ws != save)
					ws = NULL;
				r++;
			}
		}
	} else {
		ws = parsewsormon(argv, cmdusemon);
	}

	if (ws)
		fn(ws);
	else
		fprintf(cmdresp, "!unable to locate %s\n", cmdusemon ? "monitor" : "workspace");
}

void applypanelstrut(Panel *p)
{
	if (p->mon->x + p->l > p->mon->wx)
		p->mon->wx = p->l;
	if (p->mon->y + p->t > p->mon->wy)
		p->mon->wy = p->t;
	if (p->mon->w - (p->r + p->l) < p->mon->ww)
		p->mon->ww = p->mon->w - (p->r + p->l);
	if (p->mon->h - (p->b + p->t) < p->mon->wh)
		p->mon->wh = p->mon->h - (p->b + p->t);
	DBG("applypanelstrut: %s - %d,%d @ %dx%d -> %d,%d @ %dx%d",
			p->mon->name, p->mon->x, p->mon->y, p->mon->w, p->mon->h,
			p->mon->wx, p->mon->wy, p->mon->ww, p->mon->wh)
}

int applysizehints(Client *c, int *x, int *y, int *w, int *h, int bw, int usermotion, int mouse)
{
	Monitor *m = c->ws->mon;

	*w = MAX(1, *w);
	*h = MAX(1, *h);
	if (usermotion) {
		if (!mouse) {
			if (*w > c->w && c->inc_w > *w - c->w)
				*w = c->w + c->inc_w;
			else if (*w < c->w && c->inc_w > c->w - *w)
				*w = c->w - c->inc_w;
			if (*h > c->h && c->inc_h > *h - c->h)
				*h = c->h + c->inc_h;
			else if (*h < c->h && c->inc_h > c->h - *h)
				*h = c->h - c->inc_h;
			*h = MIN(*h, m->wh);
			*w = MIN(*w, m->ww);
		}
		*x = CLAMP(*x, (*w + (2 * bw) - globalcfg[GLB_MIN_XY]) * -1, scr_w - globalcfg[GLB_MIN_XY]);
		*y = CLAMP(*y, (*h + (2 * bw) - globalcfg[GLB_MIN_XY]) * -1, scr_h - globalcfg[GLB_MIN_XY]);
	} else {
		*x = CLAMP(*x, m->wx, m->wx + m->ww - *w + (2 * bw));
		*y = CLAMP(*y, m->wy, m->wy + m->wh - *h + (2 * bw));
	}

	if (FLOATING(c) || globalcfg[GLB_TILEHINTS]) {
		int baseismin;
		if (!(baseismin = (c->base_w == c->min_w && c->base_h == c->min_h)))
			*w -= c->base_w, *h -= c->base_h;
		if (c->min_aspect > 0 && c->max_aspect > 0) {
			if (c->max_aspect < (float)*w / *h)
				*w = *h * c->max_aspect + 0.5;
			else if (c->min_aspect < (float)*h / *w)
				*h = *w * c->min_aspect + 0.5;
		}
		if (baseismin)
			*w -= c->base_w, *h -= c->base_h;
		if (c->inc_w)
			*w -= *w % c->inc_w;
		if (c->inc_h)
			*h -= *h % c->inc_h;
		*w += c->base_w;
		*h += c->base_h;
		*w = MAX(*w, c->min_w);
		*h = MAX(*h, c->min_h);
		if (c->max_w)
			*w = MIN(*w, c->max_w);
		if (c->max_h)
			*h = MIN(*h, c->max_h);
	}
	return *x != c->x || *y != c->y || *w != c->w || *h != c->h || bw != c->bw;
}

void attach(Client *c, int tohead)
{
	Client *tail = NULL;

	if (!tohead)
		FIND_TAIL(tail, c->ws->clients);
	if (tail)
		ATTACH(c, tail->next);
	else
		ATTACH(c, c->ws->clients);
}

int assignws(Workspace *ws, Monitor *new)
{
	int n;
	Workspace *ows;

	if (ws->mon == new)
		return 1;
	DBG("assignws: ws: %d -> new mon: %s", ws->num, new->name)
	for (n = 0, ows = workspaces; ows; ows = ows->next) {
		if (ows->mon == ws->mon) {
			n++;
			if (ows != ws) {
				n++;
				break;
			}
		}
	}
	if (n > 1 && ows) {
		DBG("assignws: old mon: %s has available workspace: %d", ws->mon->name, ows->num)
		if (ws == ws->mon->ws)
			ws->mon->ws = ows;
		Monitor *old = ws->mon;
		ws->mon = new;
		relocatews(ws, old);
		needsrefresh = 1;
	} else {
		fprintf(cmdresp, "!unable to assign last/only workspace on monitor\n");
		return 0;
	}
	return 1;
}

void changews(Workspace *ws, int swap, int warp)
{
	Monitor *m;
	int dowarp;

	if (!ws || ws == selws)
		return;
	DBG("changews: %d:%s -> %d:%s - swap: %d - warp: %d",
			selws->num, selws->mon->name, ws->num, ws->mon->name, swap, warp)
	dowarp = !swap && warp && selws->mon != ws->mon;
	lastws = selws;
	lastmon = selmon;
	m = selws->mon;
	if (selws->sel)
		unfocus(selws->sel, 1);
	if (swap && m != ws->mon) {
		Monitor *old = ws->mon;
		selws->mon = ws->mon;
		if (ws->mon->ws == ws)
			ws->mon->ws = selws;
		ws->mon = m;
		m->ws = ws;
		updnetworkspaces();
		relocatews(ws, old);
		if (lastws->mon->ws == lastws)
			relocatews(lastws, selmon);
	}
	selws = ws;
	selmon = selws->mon;
	selmon->ws = selws;
	if (dowarp)
		xcb_warp_pointer(con, root, root, 0, 0, 0, 0,
				ws->sel ? ws->sel->x + (ws->sel->w / 2) : ws->mon->x + (ws->mon->w / 2),
				ws->sel ? ws->sel->y + (ws->sel->h / 2) : ws->mon->y + (ws->mon->h / 2));
	PROP(REPLACE, root, netatom[NET_DESK_CUR], XCB_ATOM_CARDINAL, 32, 1, &ws->num);
	needsrefresh = 1;
}

void clienthints(Client *c)
{
	xcb_generic_error_t *e;
	xcb_icccm_wm_hints_t wmh;
	xcb_get_property_cookie_t pc;

	pc = xcb_icccm_get_wm_hints(con, c->win);
	DBG("clienthints: getting window wm hints - 0x%08x", c->win)
	if (xcb_icccm_get_wm_hints_reply(con, pc, &wmh, &e)) {
		if (c == selws->sel && wmh.flags & XCB_ICCCM_WM_HINT_X_URGENCY) {
			wmh.flags &= ~XCB_ICCCM_WM_HINT_X_URGENCY;
			xcb_icccm_set_wm_hints(con, c->win, &wmh);
		} else if (wmh.flags & XCB_ICCCM_WM_HINT_X_URGENCY) {
			c->state |= STATE_URGENT;
		}
		if ((wmh.flags & XCB_ICCCM_WM_HINT_INPUT) && !wmh.input)
			c->state |= STATE_NOINPUT;
	} else {
		iferr(0, "unable to get window wm hints reply", e);
	}
}

int clientname(Client *c)
{
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t pc;
	xcb_icccm_get_text_property_reply_t r;

	pc = xcb_icccm_get_text_property(con, c->win, netatom[NET_WM_NAME]);
	if (!xcb_icccm_get_text_property_reply(con, pc, &r, &e)) {
		iferr(0, "unable to get NET_WM_NAME text property reply", e);
		pc = xcb_icccm_get_text_property(con, c->win, XCB_ATOM_WM_NAME);
		if (!xcb_icccm_get_text_property_reply(con, pc, &r, &e)) {
			iferr(0, "unable to get WM_NAME text property reply", e);
			c->title[0] = '\0';
			return 0;
		}
	}
	strlcpy(c->title, r.name, sizeof(c->title));
	xcb_icccm_get_text_property_reply_wipe(&r);
	return 1;
}

void clientrule(Client *c, Rule *wr, int nofocus)
{
	Monitor *m;
	Rule *r = wr;
	int ws, dofocus = 0;
	xcb_atom_t cur = selws->num;

	DBG("clientrule: 0x%08x", c->win)
	if (c->trans)
		cur = c->trans->ws->num;
	else if (!winprop(c->win, netatom[NET_WM_DESK], &cur) || cur > 99)
		cur = selws->num;
	ws = cur;

	if (!r) {
		for (r = rules; r; r = r->next)
			if (rulecmp(c, r)) break;
	} else if (!rulecmp(c, r)) {
		r = NULL;
	}
	if (r) {
		DBG("clientrule: matched: %s, %s, %s", r->class, r->inst, r->title)
		c->cb = r->cb;
		dofocus = r->focus;
		c->state |= r->state;
		c->x = r->x != -1 ? r->x : c->x;
		c->y = r->y != -1 ? r->y : c->y;
		c->w = r->w != -1 ? r->w : c->w;
		c->h = r->h != -1 ? r->h : c->h;
		c->bw = r->bw != -1 && !(c->state & STATE_NOBORDER) ? r->bw : c->bw;
		if (!c->trans) {
			if ((cmdusemon = (r->mon != NULL))) {
				int num;
				if ((num = strtol(r->mon, NULL, 0)) > 0 && (m = itomon(num))) {
					ws = m->ws->num;
				} else for (m = monitors; m; m = m->next) {
					if (!strcmp(r->mon, m->name)) {
						ws = m->ws->num;
						break;
					}
				}
			} else if (r->ws > 0 && r->ws <= globalcfg[GLB_NUMWS]) {
				ws = r->ws - 1;
			}
		}
	}

	if (ws + 1 > globalcfg[GLB_NUMWS] && ws <= 99)
		updworkspaces(ws + 1);
	setworkspace(c, MIN(ws, globalcfg[GLB_NUMWS]), nofocus);
	if (dofocus && c->ws != selws)
		cmdview(c->ws);
	if (r)
		gravitate(c, r->xgrav, r->ygrav, 1);
	cmdusemon = 0;
}

void clienttype(Client *c)
{
	xcb_atom_t type, state;

	DBG("clienttype: getting window type hint - 0x%08x", c->win)
	if (winprop(c->win, netatom[NET_WM_STATE], &state) && state == netatom[NET_STATE_FULL])
		setfullscreen(c, 1);
	if (winprop(c->win, netatom[NET_WM_TYPE], &type)) {
		if (type == netatom[NET_TYPE_DIALOG] || type == netatom[NET_TYPE_SPLASH])
			c->state |= STATE_FLOATING;
	} else if (c->trans || (c->trans = wintoclient(wintrans(c->win)))) {
		c->state |= STATE_FLOATING;
	}
}

void cmdborder(char **argv)
{
	Client *c;
	Workspace *ws;
	int i, old, bw, ow, rel, col = 0, first;

	bw = border[BORD_WIDTH];
	ow = border[BORD_O_WIDTH];

	while (*argv) {
		int outer;
		if ((outer = !strcmp("outer", *argv) || !strcmp("outer_width", *argv))
				|| !strcmp(*argv, "width"))
		{
			col = 0;
			if ((i = parseint(*(++argv), &rel, 1)) == INT_MIN)
				break;
			adjustisetting(i, rel, outer ? &ow : &bw, selws->gappx + (outer ? bw : 0), 1);
		} else if (col || (first = !strcmp(*argv, "colour") || !strcmp(*argv, "color"))) {
			if (!col) {
				col = 1;
				argv++;
			}
			if (!strcmp("focus", *argv)) {
				if (parsecolour(*(++argv), &border[BORD_FOCUS]) < 0) break;
			} else if (!strcmp("urgent", *argv)) {
				if (parsecolour(*(++argv), &border[BORD_URGENT]) < 0) break;
			} else if (!strcmp("unfocus", *argv)) {
				if (parsecolour(*(++argv), &border[BORD_UNFOCUS]) < 0) break;
			} else if (!strcmp("outer_focus", *argv)) {
				if (parsecolour(*(++argv), &border[BORD_O_FOCUS]) < 0) break;
			} else if (!strcmp("outer_urgent", *argv)) {
				if (parsecolour(*(++argv), &border[BORD_O_URGENT]) < 0) break;
			} else if (!strcmp("outer_unfocus", *argv)) {
				if (parsecolour(*(++argv), &border[BORD_O_UNFOCUS]) < 0) break;
			} else if (first) {
				fprintf(cmdresp, "!%s border colour: %s\n", ebadarg, *argv);
				break;
			} else {
				col = first = 0;
				continue;
			}
		} else {
			fprintf(cmdresp, "!%s border: %s\n", ebadarg, *argv);
			break;
		}
		argv++;
	}

	old = border[BORD_WIDTH];
	if (bw - ow < 1 && (unsigned int)ow != border[BORD_O_WIDTH])
		fprintf(cmdresp, "!border outer exceeds limit: %d - maximum: %d\n", ow, bw - 1);
	else if (bw - ow > 0)
		border[BORD_O_WIDTH] = ow;
	border[BORD_WIDTH] = bw;
	FOR_CLIENTS(c, ws) {
		if (!(c->state & STATE_NOBORDER)) {
			if (c->bw == old) c->bw = bw;
			drawborder(c, c == selws->sel);
		}
	}
}

void cmdcycle(char **argv)
{
	Client *c, *first;

	if (!(c = cmdclient) || FLOATING(c) || FULLSCREEN(c))
		return;
	first = nexttiled(selws->clients);
	if (c == first && !nexttiled(c->next))
		return;
	if (!(c = nexttiled(selws->sel->next)))
		c = first;
	focus(first);
	movestack(-1);
	focus(c);
	(void)(argv);
}

void cmdfakefull(char **argv)
{
	Client *c;

	if (!(c = cmdclient))
		return;
	if ((c->state ^= STATE_FAKEFULL) & STATE_FULLSCREEN) {
		if (c->w != c->ws->mon->w || c->h != c->ws->mon->h)
			c->bw = c->old_bw;
		if (!(c->state & STATE_FAKEFULL))
			resize(c, c->ws->mon->x, c->ws->mon->y, c->ws->mon->w, c->ws->mon->h, c->bw);
		needsrefresh = 1;
	}
	(void)(argv);
}

void cmdfloat(char **argv)
{
	Client *c;

	if (!(c = cmdclient) || !c->ws->layout->func)
		return;
	if (argv && *argv && !strcmp(*argv, "all")) {
		FOR_EACH(c, cmdclient->ws->clients) {
			cmdclient = c;
			if (FLOATING(c) || c->state & STATE_WASFLOATING) {
				if (FLOATING(c))
					c->state |= STATE_WASFLOATING;
				else
					c->state &= ~STATE_WASFLOATING;
				cmdfloat(NULL);
			}
		}
		return;
	}
	if (FULLSCREEN(c) || c->state & (STATE_STICKY | STATE_FIXED))
		return;
	if ((c->state ^= STATE_FLOATING) & STATE_FLOATING) {
		if (c->old_x + c->old_y == c->ws->mon->wx + c->ws->mon->wy)
			quadrant(c, &c->old_x, &c->old_y, &c->old_w, &c->old_h);
		resizehint(c, c->old_x, c->old_y, c->old_w, c->old_h, c->bw, 0, 1);
	} else {
		SAVEOLD(c);
	}
	needsrefresh = 1;
}

void cmdfocus(char **argv)
{
	int i = 0, opt;

	if (!cmdclient || FULLSCREEN(cmdclient))
		return;
	if (cmdclient != selws->sel) {
		focus(cmdclient);
		return;
	}
	if ((opt = parseopt(*argv, opts)) < 0 && (i = parseint(*argv, NULL, 0)) == INT_MIN) {
		fprintf(cmdresp, "!%s focus: %s\n", ebadarg, *argv);
		return;
	}
	if (opt == DIR_LAST)
		focus(cmdclient->snext);
	else
		movefocus(opt == -1 ? i : opt == DIR_NEXT ? 1 : -1);
}

void cmdfollow(Workspace *ws)
{
	cmdsend(ws);
	cmdview(ws);
}

void cmdfull(char **argv)
{
	if (!cmdclient)
		return;
	setfullscreen(cmdclient, !(cmdclient->state & STATE_FULLSCREEN));
	(void)(argv);
}

void cmdgappx(char **argv)
{
	int i, ng, rel;

	if (!strcmp(*argv, "width"))
		argv++;
	ng = setws->gappx;
	if (!*argv) {
		fprintf(cmdresp, "!gap %s\n", enoargs);
	} else if ((i = parseint(*argv, &rel, 1)) != INT_MIN) {
		adjustisetting(i, rel, &ng, border[BORD_WIDTH], 1);
		if (ng != setws->gappx)
			setws->gappx = ng;
	}
}

void cmdkill(char **argv)
{
	if (!cmdclient)
		return;
	if (!sendwmproto(cmdclient, WM_DELETE)) {
		xcb_grab_server(con);
		xcb_set_close_down_mode(con, XCB_CLOSE_DOWN_DESTROY_ALL);
		xcb_kill_client(con, cmdclient->win);
		xcb_aux_sync(con);
		xcb_ungrab_server(con);
	} else {
		xcb_flush(con);
	}
	(void)(argv);
}

void cmdlayout(char **argv)
{
	for (unsigned int i = 0; i < LEN(layouts); i++)
		if (!strcmp(layouts[i].name, *argv)) {
			if (&layouts[i] != setws->layout)
				setws->layout = &layouts[i];
			return;
		}
	fprintf(cmdresp, "!invalid layout name: %s\n", *argv);
}

void cmdmon(char **argv)
{
	if (monitors && nextmon(monitors)) {
		cmdusemon = 1;
		adjustwsormon(argv);
		cmdusemon = 0;
	}
}

void cmdmouse(char **argv)
{
	int arg;

	while (*argv) {
		if (!strcmp("mod", *argv)) {
			argv++;
			if (!strcmp("alt", *argv) || !strcmp("mod1", *argv))
				mousemod = XCB_MOD_MASK_1;
			else if (!strcmp("super", *argv) || !strcmp("mod4", *argv))
				mousemod = XCB_MOD_MASK_4;
			else if (!strcmp("ctrl", *argv) || !strcmp("control", *argv))
				mousemod = XCB_MOD_MASK_CONTROL;
			else {
				fprintf(cmdresp, "!invalid modifier: %s\n", *argv);
				break;
			}
		} else if ((arg = !strcmp("move", *argv)) || !strcmp("resize", *argv)) {
			argv++;
			xcb_button_t *btn = arg ? &mousemove : &mouseresize;
			if (!strcmp("button1", *argv))
				*btn = XCB_BUTTON_INDEX_1;
			else if (!strcmp("button2", *argv))
				*btn = XCB_BUTTON_INDEX_2;
			else if (!strcmp("button3", *argv))
				*btn = XCB_BUTTON_INDEX_3;
			else {
				fprintf(cmdresp, "!invalid button: %s\n", *argv);
				break;
			}
		} else {
			fprintf(cmdresp, "!%s mouse: %s\n", ebadarg, *argv);
			break;
		}
		if (*argv)
			argv++;
	}
	if (selws->sel)
		grabbuttons(selws->sel, 1);
}

void cmdnmaster(char **argv)
{
	int i, rel = 1;

	if ((i = parseint(*argv, &rel, 1)) != INT_MIN)
		adjustisetting(i, rel, &setws->nmaster, 0, 0);
}

void cmdnstack(char **argv)
{
	int i, rel = 1;

	if ((i = parseint(*argv, &rel, 1)) != INT_MIN)
		adjustisetting(i, rel, &setws->nstack, 0, 0);
}

void cmdpad(char **argv)
{
	int i, rel;

#define PAD(v, o)                                                   \
	if ((i = parseintclamp(*(++argv), &rel, v * -1, o)) == INT_MIN) \
		break;                                                      \
	v = CLAMP(rel ? v + i : i, 0, o);                               \
	needsrefresh = 1

	while (*argv) {
		if (!strcmp("l", *argv) || !strcmp("left", *argv)) {
			PAD(setws->padl, setws->mon->w / 3);
		} else if (!strcmp("r", *argv) || !strcmp("right", *argv)) {
			PAD(setws->padr, setws->mon->w / 3);
		} else if (!strcmp("t", *argv) || !strcmp("top", *argv)) {
			PAD(setws->padt, setws->mon->h / 3);
		} else if (!strcmp("b", *argv) || !strcmp("bottom", *argv)) {
			PAD(setws->padb, setws->mon->h / 3);
		} else {
			fprintf(cmdresp, "!%s pad: %s\n", ebadarg, *argv);
			break;
		}
		argv++;
	}
#undef PAD
}

void cmdresize(char **argv)
{
	Client *c, *t;
	int i, ohoff;
	float f, *sf;
	int xgrav = GRAV_NONE, ygrav = GRAV_NONE;
	int x = INT_MIN, y = INT_MIN, w = INT_MIN, h = INT_MIN, bw = INT_MIN;
	int relx = 0, rely = 0, relw = 0, relh = 0, relbw = 0;

	if (!(c = cmdclient) || FULLSCREEN(c))
		return;

#define ARG(val, relptr, z)						         \
	if ((i = parseint(*(++argv), relptr, z)) == INT_MIN) \
		break;                                           \
	val = i

	while (*argv) {
		if (!strcmp("x", *argv)) {
			argv++;
			if (!parsegeom(*argv, 'x', &x, &relx, &xgrav)) break;
		} else if (!strcmp("y", *argv)) {
			argv++;
			if (!parsegeom(*argv, 'y', &y, &rely, &ygrav)) break;
		} else if (!strcmp("w", *argv) || !strcmp("width", *argv)) {
			ARG(w, &relw, 0);
		} else if (!strcmp("h", *argv) || !strcmp("height", *argv)) {
			ARG(h, &relh, 0);
		} else if (!strcmp("bw", *argv) || !strcmp("border_width", *argv)) {
			ARG(bw, &relbw, 1);
		} else {
			fprintf(cmdresp, "!%s resize: %s\n", ebadarg, *argv);
			break;
		}
		argv++;
	}
#undef ARG

	if (FLOATING(c)) {
		x = x == INT_MIN || xgrav != GRAV_NONE ? c->x : (relx ? c->x + x : x);
		y = y == INT_MIN || ygrav != GRAV_NONE ? c->y : (rely ? c->y + y : y);
		w = w == INT_MIN ? c->w : (relw ? c->w + w : w);
		h = h == INT_MIN ? c->h : (relh ? c->h + h : h);
		bw = bw == -1 ? c->bw : (relbw ? c->bw + bw : bw);
		resizehint(c, x, y, w, h, bw, 1, 0);
		gravitate(c, xgrav, ygrav, 1);
	} else if (c->ws->layout->func == tile) {
		if (bw != INT_MIN) {
			c->bw = relbw ? c->bw + bw : bw;
			if (y == INT_MIN && !w && !h)
				drawborder(c, c == selws->sel);
		}
		if (y != INT_MIN)
			movestack(y > 0 || ygrav == GRAV_BOTTOM ? 1 : -1);
		if (w) {
			sf = &c->ws->ssplit;
			for (i = 0, t = nexttiled(c->ws->clients); t; t = nexttiled(t->next), i++)
				if (t == c) {
					if (c->ws->nmaster && i < c->ws->nmaster + c->ws->nstack)
						sf = &c->ws->msplit;
					f = relw ? ((c->ws->mon->ww * *sf) + w) / c->ws->mon->ww : w / c->ws->mon->ww;
					if (f < 0.05 || f > 0.95) {
						fprintf(cmdresp, "!width exceeded limit: %f\n", c->ws->mon->ww * f);
					} else {
						*sf = f;
						if (!h)
							needsrefresh = 1;
					}
					break;
				}
		}
		if (h) {
			ohoff = c->hoff;
			c->hoff = relh ? c->hoff + h : h;
			if (c->ws->layout->func(c->ws) == -1) {
				fprintf(cmdresp, "!height exceeded limit: %d\n", c->hoff);
				c->hoff = ohoff;
			}
		}
	} else {
		fprintf(cmdresp, "!unable to resize windows in %s layout\n", c->ws->layout->name);
		return;
	}
	eventignore(XCB_ENTER_NOTIFY);
}

void cmdrule(char **argv)
{
	int j;
	Client *c;
	Workspace *ws;
	Rule *pr, *nr = NULL;
	unsigned int i, delete, apply = 0;
	Rule r = {
		.x = -1, .y = -1, .w = -1, .h = -1, .ws = -1, .bw = -1,
		.focus = 0, .state = STATE_NONE, .xgrav = GRAV_NONE, .ygrav = GRAV_NONE,
		.cb = NULL, .mon = NULL, .inst = NULL, .class = NULL, .title = NULL,
	};

	if ((apply = !strcmp("apply", *argv))) {
		argv++;
		if (!strcmp("all", *argv))
			goto applyall;
	} else if ((delete = !strcmp("remove", *argv) || !strcmp("delete", *argv))) {
		argv++;
		if (!strcmp("all", *argv)) {
			while (rules)
				freerule(rules);
			return;
		}
	}
#define ARG(val)                                       \
	if ((j = parseint(*(++argv), NULL, 0)) == INT_MIN) \
		break;                                         \
	val = j

	while (*argv) {
		if (!r.class && !strcmp(*argv, "class")) {
			r.class = *(++argv);
		} else if (!r.inst && !strcmp(*argv, "instance")) {
			r.inst = *(++argv);
		} else if (!r.title && !strcmp(*argv, "title")) {
			r.title = *(++argv);
		} else if (!strcmp(*argv, "mon")) {
			r.mon = *(++argv);
		} else if (!strcmp(*argv, "ws")) {
			argv++;
			if ((r.ws = parseintclamp(*argv, NULL, 1, 99)) == INT_MIN) {
				r.ws = -1;
				FOR_EACH(ws, workspaces)
					if (!strcmp(ws->name, *argv)) {
						r.ws = ws->num;
						break;
					}
			}
		} else if (!strcmp(*argv, "callback")) {
			argv++;
			for (i = 0; i < LEN(callbacks); i++)
				if (!strcmp(callbacks[i].name, *argv)) {
					r.cb = &callbacks[i];
					break;
				}
		} else if (!strcmp(*argv, "x")) {
			argv++;
			if (!parsegeom(*argv, 'y', &r.y, NULL, &r.ygrav)) break;
		} else if (!strcmp(*argv, "y")) {
			argv++;
			if (!parsegeom(*argv, 'y', &r.y, NULL, &r.ygrav)) break;
		} else if (!strcmp("w", *argv) || !strcmp("width", *argv)) {
			ARG(r.w);
		} else if (!strcmp("h", *argv) || !strcmp("height", *argv)) {
			ARG(r.h);
		} else if (!strcmp("bw", *argv) || !strcmp("border_width", *argv)) {
			argv++;
			if ((j = parseintclamp(*argv, NULL, 0, scr_h / 6)) == INT_MIN) break;
			r.bw = j;
			if (r.bw == 0 && border[BORD_WIDTH])
				r.state |= STATE_NOBORDER;
		} else if (!strcmp(*argv, "float")) {
			argv++;
			if ((j = parsebool(*argv)) < 0) break;
			r.state |= j ? STATE_FLOATING : STATE_NONE;
		} else if (!strcmp(*argv, "stick")) {
			argv++;
			if ((j = parsebool(*argv)) < 0) break;
			r.state |= j ? STATE_STICKY | STATE_FLOATING : STATE_NONE;
		} else if (!strcmp(*argv, "focus")) {
			argv++;
			if ((j = parsebool(*argv)) < 0) break;
			r.focus = j;
		} else {
			fprintf(cmdresp, "!%s rule: %s\n", ebadarg, *argv);
			break;
		}
		if (*argv)
			argv++;
	}
#undef ARG

	if ((r.class || r.inst || r.title) && (r.ws != -1 || r.mon || r.focus || r.cb
				|| r.state != STATE_NONE || r.x != -1 || r.y != -1 || r.w != -1
				|| r.h != -1 || r.bw != -1 || r.xgrav != GRAV_NONE || r.ygrav != GRAV_NONE))
	{
#define M(a, b) (a == NULL || (b && !strcmp(a, b)))
		FOR_EACH(pr, rules) {
			if (M(r.class, pr->class) && M(r.inst, pr->inst) && M(r.title, pr->title)) {
				freerule(pr);
				break;
			}
		}
#undef M
		if (!delete) {
			if ((nr = initrule(&r)) && apply) {
applyall:
				FOR_CLIENTS(c, ws) {
					clientrule(c, nr, 0);
					if (c->cb)
						c->cb->func(c, 0);
				}
			}
		}
	}
}

void cmdsend(Workspace *ws)
{
	Monitor *old;

	if (!ws || !cmdclient || ws == cmdclient->ws)
		return;
	old = cmdclient->ws->mon;
	unfocus(cmdclient, 1);
	setworkspace(cmdclient, ws->num, cmdclient != cmdclient->ws->sel);
	if (cmdclient->ws->mon != old && cmdclient->ws->mon->ws == cmdclient->ws)
		relocate(cmdclient, cmdclient->ws->mon, old);
	needsrefresh = 1;
}

void cmdset(char **argv)
{
	Workspace *ws = NULL;
	unsigned int j;
	int i, names = 0, set = 0;

#define BOOL(val)                       \
	if ((i = parsebool(*(++argv))) < 0) \
		break; \
	globalcfg[GLB_##val] = i

	setws = selws;
	if (!*argv) {
		fprintf(cmdresp, "!set %s\n", enoargs);
		return;
	}
	while (*argv) {
		if (!strcmp("ws", *argv)) {
			argv++;
			if (!strcmp("default", *argv)) {
				cmdwsdef(argv + 1);
				break;
			} else if (!(ws = parsewsormon(argv, 0))) {
				fprintf(cmdresp, "!%s ws: %s\n", ebadarg, *argv);
				break;
			}
			setws = ws;
			set = 1;
		} else if (!strcmp("mon", *argv)) {
			argv++;
			if (!globalcfg[GLB_STATICWS]) {
				fprintf(cmdresp, "!unable to set monitor with dynamic workspaces enabled\n");
				break;
			} else if (!set) {
				fprintf(cmdresp, "!workspace index or name is required to set the monitor\n");
				break;
			} else if (!(ws = parsewsormon(argv, 1))) {
				fprintf(cmdresp, "!monitor index or name is required to assign workspace\n");
				break;
			}
			assignws(setws, ws->mon);
		} else if (!strcmp("numws", *argv)) {
			argv++;
			if ((i = parseintclamp(*argv, NULL, 1, 99)) == INT_MIN)
				break;
			if (i > globalcfg[GLB_NUMWS])
				updworkspaces(i);
		} else if (!strcmp("name", *argv)) {
			argv++;
			if (!*argv) {
				fprintf(cmdresp, "!set ws name %s\n", enoargs);
				break;
			}
			strlcpy(setws->name, *argv, sizeof(setws->name));
			names = 1;
		} else if (!strcmp("tile_hints", *argv)) {
			BOOL(TILEHINTS);
		} else if (!strcmp("tile_tohead", *argv)) {
			BOOL(TILETOHEAD);
		} else if (!strcmp("smart_gap", *argv)) {
			BOOL(SMART_GAP);
		} else if (!strcmp("smart_border", *argv)) {
			BOOL(SMART_BORDER);
		} else if (!strcmp("focus_urgent", *argv)) {
			BOOL(FOCUS_URGENT);
		} else if (!strcmp("focus_mouse", *argv)) {
			BOOL(FOCUS_MOUSE);
		} else if (!strcmp("focus_open", *argv)) {
			BOOL(FOCUS_OPEN);
		} else if (!strcmp("static_ws", *argv)) {
			BOOL(STATICWS);
		} else if (!strcmp("win_minxy", *argv)) {
			argv++;
			if ((i = parseintclamp(*argv, NULL, 10, 1000)) == INT_MIN)
				break;
			globalcfg[GLB_MIN_XY] = i;
		} else if (!strcmp("win_minwh", *argv)) {
			argv++;
			if ((i = parseintclamp(*argv, NULL, 10, 1000)) == INT_MIN)
				break;
			globalcfg[GLB_MIN_WH] = i;
		} else {
			for (j = 0; j < LEN(setcmds); j++)
				if (!strcmp(setcmds[j].str, *argv)) {
					((void (*)(char **))setcmds[j].func)(argv + 1);
					goto finish;
				}
			fprintf(cmdresp, "!%s set: %s\n", ebadarg, *argv);
		}
		argv++;
	}
#undef BOOL

finish:
	needsrefresh = 1;
	if (names)
		setnetwsnames();

}

void cmdmsplit(char **argv)
{
	int rel = 1;
	float f = 0.0;

	if ((f = parsefloat(*argv, &rel)) != -1.0)
		adjustfsetting(f, rel, &setws->msplit);
}

void cmdssplit(char **argv)
{
	int rel = 1;
	float f = 0.0;

	if ((f = parsefloat(*argv, &rel)) != -1.0)
		adjustfsetting(f, rel, &setws->ssplit);
}

void cmdstick(char **argv)
{
	Client *c;
	unsigned int all = 0xffffffff;

	if (!(c = cmdclient) || FULLSCREEN(c))
		return;
	if ((c->state ^= STATE_STICKY) & STATE_STICKY) {
		c->state &= ~STATE_STICKY;
		PROP(REPLACE, c->win, netatom[NET_WM_DESK], XCB_ATOM_CARDINAL, 32, 1, &c->ws->num);
	} else {
		cmdfloat(NULL);
		c->state |= STATE_STICKY | STATE_FLOATING;
		PROP(REPLACE, c->win, netatom[NET_WM_DESK], XCB_ATOM_CARDINAL, 32, 1, &all);
	}
	(void)(argv);
}

void cmdswap(char **argv)
{
	static Client *last = NULL;
	Client *c, *old, *cur = NULL, *prev = NULL;

	if (!(c = cmdclient) || FLOATING(c)
			|| (c->state & STATE_FULLSCREEN && c->w == c->ws->mon->w && c->h == c->ws->mon->h))
		return;
	if (c == nexttiled(c->ws->clients)) {
		FIND_PREV(cur, last, c->ws->clients);
		if (cur != c->ws->clients)
			prev = nexttiled(cur->next);
		if (!prev || prev != last) {
			last = NULL;
			if (!(c = nexttiled(c->next)))
				return;
		} else {
			c = prev;
		}
	}
	if (c != (old = nexttiled(c->ws->clients)) && !cur)
		FIND_PREV(cur, c, c->ws->clients);
	detach(c, 1);
	if (c != old && cur && cur != c->ws->clients) {
		last = old;
		if (old && cur != old) {
			detach(old, 0);
			ATTACH(old, cur->next);
		}
	}
	needsrefresh = 1;
	(void)(argv);
}

void cmdwin(char **argv)
{
	int e = 0;
	unsigned int ui;

	if ((cmdclient = parseclient(*argv, &e)))
		argv++;
	else if (e == -1)
		return;
	else
		cmdclient = selws->sel;

	if (!*argv) {
		fprintf(cmdresp, "!win %s\n", enoargs);
		return;
	}
	for (ui = 0; ui < LEN(wincmds); ui++)
		if (!strcmp(wincmds[ui].str, *argv)) {
			((void (*)(char **))wincmds[ui].func)(argv + 1);
			return;
		}
	fprintf(cmdresp, "!%s win: %s\n", ebadarg, *argv);
}

void cmdwm(char **argv)
{
	if (!strcmp("exit", *argv)) {
		running = 0;
	} else if (!strcmp("reload", *argv)) {
		execcfg();
	} else if (!strcmp("restart", *argv)) {
		running = 0, restart = 1;
	} else {
		fprintf(cmdresp, "!%s wm: %s\n", ebadarg, *argv);
	}
}

void cmdws(char **argv)
{
	if (workspaces && workspaces->next)
		adjustwsormon(argv);
}

void cmdwsdef(char **argv)
{
	float f;
	Workspace *ws;
	unsigned int i;
	int j, pad = 0, first, apply = 0;


#define PAD(v)                                                             \
		if ((j = parseintclamp(*(++argv), NULL, 0, scr_h / 3)) == INT_MIN) \
			break;                                                         \
		v = j

	while (*argv) {
		int *s;
		float *ff;
		if (!strcmp(*argv, "apply")) {
			apply = 1;
		} else if (!strcmp(*argv, "layout")) {
			argv++;
			pad = 0;
			for (i = 0; i < LEN(layouts); i++)
				if (!strcmp(layouts[i].name, *argv)) {
					wsdef.layout = &layouts[i];
					break;
				}
		} else if ((s = !strcmp(*argv, "master") ? &wsdef.nmaster
					: !strcmp(*argv, "stack") ? &wsdef.nstack : NULL))
		{
			pad = 0;
			argv++;
			if ((j = parseintclamp(*argv, NULL, 0, INT_MAX - 1)) == INT_MIN) break;
			*s = j;
		} else if ((ff = !strcmp(*argv, "msplit") ? &wsdef.msplit
					: !strcmp(*argv, "ssplit") ? &wsdef.ssplit : NULL))
		{
			pad = 0;
			argv++;
			if ((f = parsefloat(*argv, NULL)) == -1.0) break;
			*ff = f;
		} else if (!strcmp(*argv, "gap")) {
			pad = 0;
			argv++;
			if ((j = parseintclamp(*argv, NULL, 0, scr_h / 6)) == INT_MIN) break;
			wsdef.gappx = j;
		} else if (pad || (first = !strcmp(*argv, "pad"))) {
			if (!pad) {
				pad = 1;
				argv++;
			}
			if (!strcmp("l", *argv) || !strcmp("left", *argv)) {
				PAD(wsdef.padl);
			} else if (!strcmp("r", *argv) || !strcmp("right", *argv)) {
				PAD(wsdef.padr);
			} else if (!strcmp("t", *argv) || !strcmp("top", *argv)) {
				PAD(wsdef.padt);
			} else if (!strcmp("b", *argv) || !strcmp("bottom", *argv)) {
				PAD(wsdef.padb);
			} else if (first) {
				fprintf(cmdresp, "!%s pad: %s\n", ebadarg, *argv);
				break;
			} else {
				pad = first = 0;
				continue;
			}
		} else {
			fprintf(cmdresp, "!%s workspace default: %s\n", ebadarg, *argv);
			break;
		}
		argv++;
	}

	if (apply) {
		FOR_EACH(ws, workspaces) {
			ws->layout = wsdef.layout;
			ws->gappx = wsdef.gappx;
			ws->nmaster = wsdef.nmaster;
			ws->nstack = wsdef.nstack;
			ws->msplit = wsdef.msplit;
			ws->ssplit = wsdef.ssplit;
			ws->padl = wsdef.padl;
			ws->padr = wsdef.padr;
			ws->padt = wsdef.padt;
			ws->padb = wsdef.padb;
		}
	}
#undef PAD
}

void cmdview(Workspace *ws)
{
	if (!ws)
		return;
	changews(ws, globalcfg[GLB_STATICWS] ? 0 : !cmdusemon,
			cmdusemon || (globalcfg[GLB_STATICWS] && selws->mon != ws->mon));
	needsrefresh = 1;
}

void drawborder(Client *c, int focused)
{ /* modified from swm/wmutils */
	int o, b;
	unsigned int in, out;
	xcb_gcontext_t gc;
	xcb_pixmap_t pmap;

	if (c->state & STATE_NOBORDER || !c->bw)
		return;
	b = c->bw;
	o = border[BORD_O_WIDTH];
	in = border[focused ? BORD_FOCUS : ((c->state & STATE_URGENT) ? BORD_URGENT : BORD_UNFOCUS)];
	out = border[focused ? BORD_O_FOCUS : ((c->state & STATE_URGENT) ? BORD_O_URGENT : BORD_O_UNFOCUS)];
	xcb_rectangle_t inner[] = {
		{ c->w,         0,            b - o,        c->h + b - o },
		{ c->w + b + o, 0,            b - o,        c->h + b - o },
		{ 0,            c->h,         c->w + b - o, b - o        },
		{ 0,            c->h + b + o, c->w + b - o, b - o        },
		{ c->w + b + o, c->h + b + o, b,            b            }
	};
	xcb_rectangle_t outer[] = {
		{ c->w + b - o, 0,            o,            c->h + b * 2 },
		{ c->w + b,     0,            o,            c->h + b * 2 },
		{ 0,            c->h + b - o, c->w + b * 2, o            },
		{ 0,            c->h + b,     c->w + b * 2, o            },
		{ 1,            1,            1,            1            }
	};

	pmap = xcb_generate_id(con);
	xcb_create_pixmap(con, c->depth, pmap, c->win, W(c), H(c));
	gc = xcb_generate_id(con);
	xcb_create_gc(con, gc, pmap, XCB_GC_FOREGROUND, &in);
	if (b - o > 0) {
		xcb_poly_fill_rectangle(con, pmap, gc, LEN(inner), inner);
		xcb_change_gc(con, gc, XCB_GC_FOREGROUND, &out);
	}
	xcb_poly_fill_rectangle(con, pmap, gc, LEN(outer), outer);
	xcb_change_window_attributes(con, c->win, XCB_CW_BORDER_PIXMAP, &pmap);
	xcb_free_pixmap(con, pmap);
	xcb_free_gc(con, gc);
}

Monitor *coordtomon(int x, int y)
{
	Monitor *m = NULL;

	FOR_EACH(m, monitors)
		if (m->connected && x >= m->x && y >= m->y && x <= m->x + m->w && y <= m->y + m->h)
			return m;
	return m;
}

void detach(Client *c, int reattach)
{
	Client **cc = &c->ws->clients;

	DETACH(c, cc);
	if (reattach)
		ATTACH(c, c->ws->clients);
}

void detachstack(Client *c)
{
	Client **cc = &c->ws->stack;

	while (*cc && *cc != c)
		cc = &(*cc)->snext;
	*cc = c->snext;
	if (c == c->ws->sel)
		c->ws->sel = c->ws->stack;
}

int dwindle(Workspace *ws)
{
	return fib(ws, 1);
}

void eventhandle(xcb_generic_event_t *ev)
{
	Client *c;
	Monitor *m;
	static xcb_timestamp_t last = 0;
	static int grabbing = 0, grabmove = 0;
	static int mx, my, ox, oy, ow, oh, nw, nh, nx, ny;

	switch (ev->response_type & 0x7f) {
	case XCB_CONFIGURE_REQUEST: {
		xcb_configure_request_event_t *e = (xcb_configure_request_event_t *)ev;

		if ((c = wintoclient(e->window))) {
			DBG("eventhandle: CONFIGURE_REQUEST - managed %s window 0x%08x",
					FLOATING(c) ? "floating" : "tiled", e->window)
			if (e->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH) {
				c->bw = e->border_width;
			} else if (FLOATING(c)) {
				m = c->ws->mon;
				SAVEOLD(c);
				if (e->value_mask & XCB_CONFIG_WINDOW_X && e->x != W(c) * -2)
					c->x = m->x + e->x - c->bw;
				if (e->value_mask & XCB_CONFIG_WINDOW_Y && e->x != W(c) * -2)
					c->y = m->y + e->y - c->bw;
				if (e->value_mask & XCB_CONFIG_WINDOW_WIDTH)
					c->w = e->width;
				if (e->value_mask & XCB_CONFIG_WINDOW_HEIGHT)
					c->h = e->height;
				if (c->x + c->w > m->wx + m->ww)
					c->x = m->wx + ((m->ww - W(c)) / 2);
				if (c->y + c->h > m->wy + m->wh)
					c->y = m->wy + ((m->wh - H(c)) / 2);
				if (e->value_mask & (XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y)
						&& !(e->value_mask & (XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT)))
					sendconfigure(c);
				if (c->ws == m->ws && e->x != W(c) * -2)
					resize(c, c->x, c->y, c->w, c->h, c->bw);
				else
					c->state |= STATE_NEEDSRESIZE;
			} else {
				sendconfigure(c);
			}
		} else {
			DBG("eventhandle: CONFIGURE_REQUEST - unmanaged - 0x%08x", e->window)
			xcb_params_configure_window_t wc;
			wc.x = e->x, wc.y = e->y, wc.border_width = e->border_width;
			wc.width = e->width, wc.height = e->height;
			wc.sibling = e->sibling;
			wc.stack_mode = e->stack_mode;
			xcb_configure_window(con, e->window, e->value_mask, &wc);
		}
		xcb_aux_sync(con);
		return;
	}
	case XCB_ENTER_NOTIFY: {
		Workspace *ws;
		xcb_enter_notify_event_t *e = (xcb_enter_notify_event_t *)ev;

		if (e->event != root && (e->mode != XCB_NOTIFY_MODE_NORMAL
					|| e->detail == XCB_NOTIFY_DETAIL_INFERIOR))
			return;
		DBG("eventhandle: ENTER_NOTIFY - 0x%08x", e->event)
		ws = selws;
		if ((c = wintoclient(e->event)))
			ws = c->ws;
		else if ((m = coordtomon(e->root_x, e->root_y)))
			ws = m->ws;
		if (ws && ws != selws)
			changews(ws, 0, 0);
		if (c && globalcfg[GLB_FOCUS_MOUSE])
			focus(c);
		return;
	}
	case XCB_FOCUS_IN: {
		xcb_focus_in_event_t *e = (xcb_focus_in_event_t *)ev;

		if (e->mode == XCB_NOTIFY_MODE_GRAB
				|| e->mode == XCB_NOTIFY_MODE_UNGRAB
				|| e->detail == XCB_NOTIFY_DETAIL_POINTER
				|| e->detail == XCB_NOTIFY_DETAIL_POINTER_ROOT
				|| e->detail == XCB_NOTIFY_DETAIL_NONE)
			return;
		if (selws->sel && e->event != selws->sel->win) {
			DBG("eventhandle: FOCUS_IN - 0x%08x", e->event)
			setinputfocus(selws->sel);
		}
		return;
	}
	case XCB_CONFIGURE_NOTIFY: {
		xcb_configure_notify_event_t *e = (xcb_configure_notify_event_t *)ev;

		if (e->window == root)
			scr_w = e->width, scr_h = e->height;
		return;
	}
	case XCB_DESTROY_NOTIFY: {
		unmanage(((xcb_destroy_notify_event_t *)ev)->window, 1);
		return;
	}
	case XCB_MAP_REQUEST: {
		xcb_get_geometry_reply_t *g;
		xcb_get_window_attributes_reply_t *wa;
		xcb_map_request_event_t *e = (xcb_map_request_event_t *)ev;

		if (!(wa = winattr(e->window)) || !(g = wingeom(e->window)))
			return;
		manage(e->window, g, wa);
		free(wa);
		free(g);
		return;
	}
	case XCB_UNMAP_NOTIFY: {
		xcb_unmap_notify_event_t *e = (xcb_unmap_notify_event_t *)ev;

		if (e->response_type & ~0x7f)
			setwmwinstate(e->window, XCB_ICCCM_WM_STATE_WITHDRAWN);
		else {
			DBG("eventhandle: UNMAP_NOTIFY - 0x%08x", e->window)
			unmanage(e->window, 0);
		}
		return;
	}
	case XCB_CLIENT_MESSAGE: {
		xcb_client_message_event_t *e = (xcb_client_message_event_t *)ev;
		unsigned int *d = e->data.data32;

		DBG("eventhandle: CLIENT_MESSAGE - 0x%08x", e->window)
		if (e->window == root && e->type == netatom[NET_DESK_CUR]) {
			unfocus(selws->sel, 1);
			cmdview(itows(d[0]));
		} else if (e->type == netatom[NET_CLOSE]) {
			unmanage(e->window, 1);
		} else if ((c = wintoclient(e->window))) {
			if (e->type == netatom[NET_WM_DESK]) {
				if (!itows(d[0])) {
					warnx("invalid workspace index: %d", d[0]);
					return;
				}
				setworkspace(c, d[0], c != c->ws->sel);
				needsrefresh = 1;
			} else if (e->type == netatom[NET_WM_STATE]
					&& (d[1] == netatom[NET_STATE_FULL] || d[2] == netatom[NET_STATE_FULL]))
			{
				setfullscreen(c, (d[0] == 1 || (d[0] == 2 && !(c->state & STATE_FULLSCREEN))));
			} else if (e->type == netatom[NET_ACTIVE] && c != selws->sel) {
				if (globalcfg[GLB_FOCUS_URGENT]) {
					if (c->ws != selws) {
						unfocus(selws->sel, 1);
						cmdview(c->ws);
					}
					focus(c);
				} else {
					seturgent(c, 1);
				}
			}
		}
		return;
	}
	case XCB_BUTTON_PRESS: {
		xcb_generic_error_t *err;
		xcb_grab_pointer_cookie_t pc;
		xcb_grab_pointer_reply_t *ptr = NULL;
		xcb_button_press_event_t *e = (xcb_button_press_event_t *)ev;

		if (!(c = wintoclient(e->event)))
			return;
		DBG("eventhandle: BUTTON_PRESS - 0x%08x - button: %d", e->event, e->detail)
		focus(c);
		restack(c->ws);
		xcb_allow_events(con, XCB_ALLOW_REPLAY_POINTER, e->time);
		if (grabbing || CLNMOD(e->state) != CLNMOD(mousemod))
			return;
		if (e->detail == mousemove || e->detail == mouseresize) {
			DBG("eventhandle: BUTTON_PRESS - grabbing pointer - 0x%08x", e->event)
			grabmove = e->detail == mousemove;
			if (!(c = selws->sel) || FULLSCREEN(c) || ((c->state & STATE_FIXED) && !grabmove))
				return;
			mx = e->root_x;
			my = e->root_y;
			pc = xcb_grab_pointer(con, 0, root, XCB_EVENT_MASK_BUTTON_RELEASE
					| XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_POINTER_MOTION,
					XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, root,
					cursor[grabmove ? CURS_MOVE : CURS_RESIZE], XCB_CURRENT_TIME);
			if ((ptr = xcb_grab_pointer_reply(con, pc, &err))
					&& ptr->status == XCB_GRAB_STATUS_SUCCESS)
			{
				last = 0;
				grabbing = 1;
				ox = nx = c->x;
				oy = ny = c->y;
				ow = nw = c->w;
				oh = nh = c->h;
			} else {
				iferr(0, "unable to grab pointer", err);
			}
			free(ptr);
		}
		return;
	}
	case XCB_BUTTON_RELEASE: {
		if (grabbing) {
			DBG("eventhandle: BUTTON_RELEASE - ungrabbing pointer - 0x%08x", selws->sel->win)
			iferr(1, "failed to ungrab pointer",
					xcb_request_check(con, xcb_ungrab_pointer_checked(con, XCB_CURRENT_TIME)));
			if (!grabmove)
				eventignore(XCB_ENTER_NOTIFY);
			grabbing = 0;
		}
		break;
	}
	case XCB_MOTION_NOTIFY: {
		xcb_motion_notify_event_t *e = (xcb_motion_notify_event_t *)ev;

		if (grabbing && (c = selws->sel)) {
			if ((e->time - last) < (1000 / 120))
				return;
			last = e->time;
			if (!grabmove && !FLOATING(c) && selws->layout->func == tile) {
				int i;
				Client *p;
				for (i = 0, p = nexttiled(selws->clients); p && p != c; p = nexttiled(p->next), i++)
					;
				if (i >= selws->nstack + selws->nmaster)
					selws->ssplit =
						(double)(ox - selws->mon->x + (e->root_x - mx)
								- (selws->mon->ww * selws->msplit))
						/ (double)(selws->mon->ww - (selws->mon->ww * selws->msplit));
				else if (i >= selws->nmaster)
					selws->msplit = (double)(ox - selws->mon->x + (e->root_x - mx))
						/ (double)selws->mon->ww;
				else
					selws->msplit = (double)((ox - selws->mon->x + ow) + (e->root_x - mx))
						/ (double)selws->mon->ww;
				int ohoff = c->hoff;
				if (i + 1 == selws->nmaster || i + 1 == selws->nmaster + selws->nstack
						|| !nexttiled(c->next))
					c->hoff = (e->root_y - my) * -1;
				else
					c->hoff = e->root_y - my;
				if (selws->layout->func(selws) < 0)
					c->hoff = ohoff;
			} else {
				if (grabmove) {
					nx = ox + (e->root_x - mx);
					ny = oy + (e->root_y - my);
				} else {
					nw = ow + (e->root_x - mx);
					nh = oh + (e->root_y - my);
				}
				if ((nw != c->w || nh != c->h || nx != c->x || ny != c->y)) {
					if (!FLOATING(c) || (c->state & STATE_FULLSCREEN
								&& c->state & STATE_FAKEFULL
								&& !(c->old_state & STATE_FLOATING)))
					{
						c->state |= STATE_FLOATING;
						c->old_state |= STATE_FLOATING;
						if (c->max_w)
							c->w = MIN(c->w, c->max_w);
						if (c->max_h)
							c->h = MIN(c->h, c->max_h);
						c->x = CLAMP(c->x, selws->mon->wx,
								selws->mon->wx + selws->mon->ww - W(c));
						c->y = CLAMP(c->y, selws->mon->wy,
								selws->mon->wy + selws->mon->wh - H(c));
						resizehint(c, c->x, c->y, c->w, c->h, c->bw, 1, 1);
						if (selws->layout->func)
							selws->layout->func(selws);
						restack(selws);
					}
					if (grabmove && (m = coordtomon(e->root_x, e->root_y)) && m->ws != c->ws) {
						setworkspace(c, m->ws->num, 0);
						changews(m->ws, 0, 0);
						focus(c);
					}
					resizehint(c, nx, ny, nw, nh, c->bw, 1, 1);
				}
			}
		} else if (e->event == root && (m = coordtomon(e->root_x, e->root_y)) && m->ws != selws) {
			DBG("eventhandle: MOTION_NOTIFY - updating active monitor - 0x%08x", e->event)
			changews(m->ws, 0, 0);
			focus(NULL);
		}
		return;
	}
	case XCB_PROPERTY_NOTIFY: {
		Panel *p;
		xcb_property_notify_event_t *e = (xcb_property_notify_event_t *)ev;

#ifdef DEBUG
		if (e->window != root) {
			for (unsigned int i = 0; i < LEN(netatom); i++)
				if (netatom[i] == e->atom) {
					DBG("eventhandle: PROPERTY_NOTIFY - atom: %s - 0x%08x", netatoms[i], e->window)
					break;
				}
			for (unsigned int i = 0; i < LEN(wmatom); i++)
				if (wmatom[i] == e->atom) {
					DBG("eventhandle: PROPERTY_NOTIFY - atom: %s - 0x%08x", wmatoms[i], e->window)
					break;
				}
		}
#endif
		if (e->state == XCB_PROPERTY_DELETE)
			return;
		if ((c = wintoclient(e->window))) {
			switch (e->atom) {
			case XCB_ATOM_WM_HINTS:
				clienthints(c); return;
			case XCB_ATOM_WM_NORMAL_HINTS:
				sizehints(c, 0); return;
			case XCB_ATOM_WM_TRANSIENT_FOR:
				if ((c->trans = wintoclient(wintrans(c->win))) && !FLOATING(c)) {
					c->state |= STATE_FLOATING;
					needsrefresh = 1;
				}
				return;
			default:
				if ((e->atom == XCB_ATOM_WM_NAME || e->atom == netatom[NET_WM_NAME])
						&& clientname(c))
					pushstatus();
				else if (e->atom == netatom[NET_WM_TYPE])
					clienttype(c);
				return;
			}
		} else if ((e->atom == netatom[NET_WM_STRUTP] || e->atom == netatom[NET_WM_STRUT])
				&& (p = wintopanel(e->window)))
		{
			updstruts(p, 1);
			needsrefresh = 1;
		}
		return;
	}
	case 0: {
		xcb_generic_error_t *e = (xcb_generic_error_t*)ev;

		fprintf(stderr, "yaxwm: previous request returned error %i, \"%s\""
				" major code %u, minor code %u resource id %u sequence %u\n",
				(int)e->error_code, xcb_event_get_error_label(e->error_code),
				(uint32_t) e->major_code, (uint32_t) e->minor_code,
				(uint32_t) e->resource_id, (uint32_t) e->sequence);
		break;
	}
	default: {
		if (ev->response_type == randrbase + XCB_RANDR_SCREEN_CHANGE_NOTIFY)
			if (((xcb_randr_screen_change_notify_event_t *)ev)->root == root && updrandr())
				updworkspaces(globalcfg[GLB_NUMWS]);
	}
	}
}

void eventignore(uint8_t type)
{
	xcb_generic_event_t *ev = NULL;

	xcb_flush(con);
	while (running && (ev = xcb_poll_for_event(con))) {
		if ((ev->response_type & 0x7f) != type)
			eventhandle(ev);
		free(ev);
	}
}

void execcfg(void)
{
	char *cfg;
	char path[PATH_MAX];

	if (!(cfg = getenv("YAXWMRC"))) {
		char *home;
		if (!(home = getenv("XDG_CONFIG_HOME"))) {
			if (!(home = getenv("HOME"))) {
				warn("getenv");
				return;
			}
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

int fib(Workspace *ws, int out)
{
	Client *c;
	Monitor *m = ws->mon;
	unsigned int i, n, x, y;
	int w, h, g, f = 0, ret = 1;

	for (n = 0, c = nexttiled(ws->clients); c; c = nexttiled(c->next), n++)
		;
	if (!n) return ret;

	g = globalcfg[GLB_SMART_GAP] && n == 1 ? 0 : ws->gappx;
	x = m->wx + ws->padl;
	y = m->wy + ws->padt;
	w = m->ww - ws->padl - ws->padr;
	h = m->wh - ws->padt - ws->padb;

	for (i = 0, c = nexttiled(ws->clients); c; c = nexttiled(c->next), i++) {
		unsigned int ox = x;
		int *p = (i % 2) ? &h : &w;
		int b = globalcfg[GLB_SMART_BORDER] && n == 1 ? 0 : c->bw;
		if (i < n - 1) {
			*p /= 2;
			if (!out) {
				if (i % 4 == 2) x += w;
				else if (i % 4 == 3) y += h;
			}
		}
		switch (i % 4) {
		case 0: y += out ? h : h * -1; break;
		case 1: x += w; break;
		case 2: y += h; break;
		case 3: x += out ? w : w * -1; break;
		}
		if (!i) {
			if (n > 1)
				w = ((m->ww - ws->padl - ws->padr) * ws->msplit) - g;
			y = m->wy - ws->padt;
		} else if (i == 1) {
			w = m->ww - ws->padl - ws->padr - w - g;
		}
		if (f || *p - (2 * b) - (n > 1 ? g : (2 * g)) < globalcfg[GLB_MIN_WH]) {
			*p *= 2;
			x = (i % 2) ? x : ox;
			if (f) {
				popfloat(c);
				continue;
			}
			f = 1;
			ret = -1;
		}
		resizehint(c, x + g, y + g, w - (2 * b) - (n > 1 ? g : (2 * g)),
				h - (2 * b) - (n > 1 ? g : (2 * g)), b, 0, 0);
	}
	return ret;
}

void focus(Client *c)
{
	if (!c || c->ws != selws)
		c = selws->stack;
	if (selws->sel && selws->sel != c)
		unfocus(selws->sel, 0);
	if (c) {
		if (c->state & STATE_URGENT)
			seturgent(c, 0);
		detachstack(c);
		c->snext = c->ws->stack;
		c->ws->stack = c;
		grabbuttons(c, 1);
		drawborder(c, 1);
		setinputfocus(c);
	} else {
		unfocus(c, 1);
	}
	selws->sel = c;
}

void freemon(Monitor *m)
{
	Monitor *mon;

	if (m == monitors) {
		monitors = monitors->next;
	} else {
		FIND_PREV(mon, m, monitors);
		if (mon) mon->next = m->next;
	}
	free(m);
}

void freerule(Rule *r)
{
	Rule **rr = &rules;

	DETACH(r, rr);
	if (r->class) { regfree(&(r->classreg)); free(r->class); }
	if (r->inst)  { regfree(&(r->instreg));  free(r->inst);  }
	if (r->title) { regfree(&(r->titlereg)); free(r->title); }
	free(r->mon);
	free(r);
}

void freewm(void)
{
	while (panels)
		unmanage(panels->win, 0);
	while (desks)
		unmanage(desks->win, 0);
	while (workspaces) {
		while (workspaces->stack)
			unmanage(workspaces->stack->win, 0); // NOLINT
		freews(workspaces);
	}
	while (monitors)
		freemon(monitors);
	while (rules)
		freerule(rules);

	if (con) {
		for (unsigned int i = 0; i < LEN(cursors); i++)
			xcb_free_cursor(con, cursor[i]);
		xcb_key_symbols_free(keysyms);
		xcb_destroy_window(con, wmcheck);
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT,
				XCB_INPUT_FOCUS_POINTER_ROOT, XCB_CURRENT_TIME);
		if (!restart)
			xcb_delete_property(con, root, netatom[NET_ACTIVE]);
		xcb_flush(con);
		xcb_disconnect(con);
	}

	if (restart) {
		char fdstr[64];
		if (!itoa(sockfd, fdstr))
			itoa(-1, fdstr);
		char *const arg[] = { argv0, "-s", fdstr, NULL };
		execvp(arg[0], arg);
	}

	close(sockfd);
	unlink(sock);
	unlink(status);
}

void freews(Workspace *ws)
{
	Workspace *sel;

	if (ws == workspaces) {
		workspaces = workspaces->next;
	} else {
		FIND_PREV(sel, ws, workspaces);
		if (sel) sel->next = ws->next;
	}
	free(ws);
}

void grabbuttons(Client *c, int focused)
{
	xcb_generic_error_t *e;
	xcb_keysym_t nlock = 0xff7f;
	xcb_get_modifier_mapping_reply_t *m = NULL;
	unsigned int mods[] = { 0, XCB_MOD_MASK_LOCK, 0, XCB_MOD_MASK_LOCK };

	lockmask = 0;
	if ((m = xcb_get_modifier_mapping_reply(con, xcb_get_modifier_mapping(con), &e))) {
		xcb_keycode_t *kc, *t = NULL;
		if ((t = xcb_key_symbols_get_keycode(keysyms, nlock))
				&& (kc = xcb_get_modifier_mapping_keycodes(m)))
		{
			for (unsigned int i = 0; i < 8; i++)
				for (unsigned int j = 0; j < m->keycodes_per_modifier; j++)
					if (kc[i * m->keycodes_per_modifier + j] == *t)
						lockmask = (1 << i);
		}
		free(t);
	} else {
		iferr(0, "unable to get modifier mapping for numlock", e);
	}
	free(m);

	mods[2] |= lockmask, mods[3] |= lockmask;
	xcb_ungrab_button(con, XCB_BUTTON_INDEX_ANY, c->win, XCB_BUTTON_MASK_ANY);
	if (!focused)
		xcb_grab_button(con, 0, c->win,
				XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE,
				XCB_GRAB_MODE_SYNC, XCB_GRAB_MODE_SYNC, XCB_NONE, XCB_NONE,
				XCB_BUTTON_INDEX_ANY, XCB_BUTTON_MASK_ANY);
	for (unsigned int i = 0; i < LEN(mods); i++) {
			xcb_grab_button(con, 0, c->win,
					XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE,
					XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_SYNC, XCB_NONE, XCB_NONE,
					mousemove, mousemod | mods[i]);
			xcb_grab_button(con, 0, c->win,
					XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE,
					XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_SYNC, XCB_NONE, XCB_NONE,
					mouseresize, mousemod | mods[i]);
	}
}

void gravitate(Client *c, int xgrav, int ygrav, int matchgap)
{
	int x, y, gap;
	int mx, my, mw, mh;

	if (!c || !c->ws || !FLOATING(c))
		return;
	x = c->x;
	y = c->y;
	if (c->trans) {
		gap = 0;
		mx = c->trans->x, my = c->trans->y;
		mw = c->trans->w, mh = c->trans->h;
	} else {
		gap = matchgap ? c->ws->gappx : 0;
		mx = c->ws->mon->wx, my = c->ws->mon->wy;
		mw = c->ws->mon->ww, mh = c->ws->mon->wh;
	}
	switch (xgrav) {
	case GRAV_LEFT:   x = mx + gap; break;
	case GRAV_RIGHT:  x = mx + mw - W(c) - gap; break;
	case GRAV_CENTER: x = (mx + mw - W(c)) / 2; break;
	}
	switch (ygrav) {
	case GRAV_TOP:    y = my + gap; break;
	case GRAV_BOTTOM: y = my + mh - H(c) - gap; break;
	case GRAV_CENTER: y = (my + mh - H(c)) / 2; break;
	}
	if (c->ws == c->ws->mon->ws)
		resizehint(c, x, y, c->w, c->h, c->bw, 0, 0);
}

int grid(Workspace *ws)
{
	Client *c;
	Monitor *m = ws->mon;
	int wx, wy, ww, wh;
	int i, n, g, cols, rows, col, row;

	for (n = 0, c = nexttiled(ws->clients); c; c = nexttiled(c->next), n++)
		;
	if (!n)
		return 1;
	for (cols = 0; cols <= n / 2; cols++)
		if (cols * cols >= n)
			break;
	if (n == 5)
		cols = 2;
	rows = n / cols;
	wx = m->wx + ws->padl;
	wy = m->wy + ws->padt;
	ww = m->ww - ws->padl - ws->padr;
	wh = m->wh - ws->padt - ws->padb;
	g = globalcfg[GLB_SMART_GAP] && n == 1 ? 0 : ws->gappx;

	for (i = col = row = 0, c = nexttiled(ws->clients); c; i++, c = nexttiled(c->next)) {
		if (i / rows + 1 > cols - n % cols)
			rows = n / cols + 1;
		int b = globalcfg[GLB_SMART_BORDER] && n == 1 ? 0 : c->bw;
		int ch = rows ? (wh - g) / rows : wh;
		int cw = cols ? (ww - g) / cols : ww;
		int cx = (wx + g) + col * cw;
		int cy = (wy + g) + row * ch;
		resizehint(c, cx, cy, cw - (2 * b) - g, ch - (2 * b) - g, b, 0, 0);
		if (++row >= rows) {
			row = 0;
			col++;
		}
	}
	return 1;
}

int iferr(int lvl, char *msg, xcb_generic_error_t *e)
{
	if (!e)
		return 1;
	warn("%s", msg);
	free(e);
	if (lvl)
		exit(lvl);
	return 0;
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
			iferr(0, "unable to initialize atom", e);
		}
	}
}

void initclient(xcb_window_t win, xcb_get_geometry_reply_t *g)
{
	Client *c;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t pc;
	xcb_get_property_reply_t *pr = NULL;
	xcb_icccm_get_wm_class_reply_t p;

	DBG("initclient: 0x%08x", win)
	c = ecalloc(1, sizeof(Client));
	c->win = win;
	c->depth = g->depth;
	c->x = c->old_x = g->x;
	c->y = c->old_y = g->y;
	c->w = c->old_w = g->width;
	c->h = c->old_h = g->height;
	c->bw = c->old_bw = border[BORD_WIDTH];
	c->state = STATE_NEEDSMAP;
	c->old_state = STATE_NONE;
	c->trans = wintoclient(wintrans(c->win));

	clientname(c);
	pc = xcb_icccm_get_wm_class(con, c->win);
	if (!xcb_icccm_get_wm_class_reply(con, pc, &p, &e)) {
		iferr(0, "failed to get window class", e);
		c->class[0] = c->inst[0] = '\0';
	} else {
		strlcpy(c->class, p.class_name, sizeof(c->class));
		strlcpy(c->inst, p.instance_name, sizeof(c->inst));
		xcb_icccm_get_wm_class_reply_wipe(&p);
	}

	pc = xcb_get_property(con, 0, c->win, wmatom[WM_MOTIF], wmatom[WM_MOTIF], 0, 5);
	if ((pr = xcb_get_property_reply(con, pc, &e))) {
		if (((xcb_atom_t *)xcb_get_property_value(pr))[2] == 0) {
			c->bw = 0;
			c->state |= STATE_NOBORDER;
		}
	} else {
		iferr(0, "unable to get window motif hints reply", e);
	}
	free(pr);

	clientrule(c, NULL, !globalcfg[GLB_FOCUS_OPEN]);
	c->w = CLAMP(c->w, globalcfg[GLB_MIN_WH], c->ws->mon->ww);
	c->h = CLAMP(c->h, globalcfg[GLB_MIN_WH], c->ws->mon->wh);
	if (c->trans) {
		c->state |= STATE_FLOATING;
		c->x = c->trans->x + ((W(c->trans) - W(c)) / 2);
		c->y = c->trans->y + ((H(c->trans) - H(c)) / 2);
	}
	clienttype(c);
	sizehints(c, 1);
	clienthints(c);
	xcb_change_window_attributes(con, c->win, XCB_CW_EVENT_MASK,
			(unsigned int[]){ XCB_EVENT_MASK_ENTER_WINDOW
							| XCB_EVENT_MASK_FOCUS_CHANGE
							| XCB_EVENT_MASK_PROPERTY_CHANGE
							| XCB_EVENT_MASK_STRUCTURE_NOTIFY });
	drawborder(c, 0);
	grabbuttons(c, 0);
	if (FLOATING(c) || c->state & STATE_FIXED) {
		c->x = CLAMP(c->x, c->ws->mon->wx, c->ws->mon->wx + c->ws->mon->ww - W(c));
		c->y = CLAMP(c->y, c->ws->mon->wy, c->ws->mon->wy + c->ws->mon->wh - H(c));
		if (c->x == c->ws->mon->wx && c->y == c->ws->mon->wy)
			quadrant(c, &c->x, &c->y, &c->w, &c->h);
	}
	MOVE(c->win, c->x + 2 * scr_w, c->y);
	if (globalcfg[GLB_FOCUS_OPEN])
		focus(c);
	if (c->cb)
		c->cb->func(c, 0);
}

void initdesk(xcb_window_t win, xcb_get_geometry_reply_t *g)
{
	Desk *d;

	DBG("initdesk: 0x%08x", win)
	d = ecalloc(1, sizeof(Desk));
	d->win = win;
	if (!(d->mon = coordtomon(g->x, g->y)))
		d->mon = selws->mon;
	d->x = d->mon->wx;
	d->y = d->mon->wy;
	d->w = d->mon->ww;
	d->h = d->mon->wh;
	d->state |= STATE_NEEDSMAP;
	ATTACH(d, desks);
	xcb_change_window_attributes(con, d->win, XCB_CW_EVENT_MASK,
			(unsigned int[]){ XCB_EVENT_MASK_PROPERTY_CHANGE
							| XCB_EVENT_MASK_STRUCTURE_NOTIFY });
	MOVERESIZE(d->win, d->x, d->y, d->w, d->h, 0);
	setstackmode(d->win, XCB_STACK_MODE_BELOW);
}

void initmon(int num, char *name, xcb_randr_output_t id, int x, int y, int w, int h)
{
	Monitor *m, *tail;

	DBG("initmon: %d:%s - %d,%d @ %dx%d", num, name, x, y, w, h)
	m = ecalloc(1, sizeof(Monitor));
	m->id = id;
	m->num = num;
	m->connected = 1;
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
}

void initpanel(xcb_window_t win, xcb_get_geometry_reply_t *g)
{
	int *s;
	Panel *p;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t rc;
	xcb_get_property_reply_t *prop = NULL;

	DBG("initpanel: 0x%08x", win)
	rc = xcb_get_property(con, 0, win, netatom[NET_WM_STRUTP], XCB_ATOM_CARDINAL, 0, 4);
	p = ecalloc(1, sizeof(Panel));
	p->win = win;
	p->x = g->x;
	p->y = g->y;
	p->w = g->width;
	p->h = g->height;
	p->state |= STATE_NEEDSMAP;
	if (!(p->mon = coordtomon(p->x, p->y)))
		p->mon = selws->mon;
	if (!(prop = xcb_get_property_reply(con, rc, &e)) || prop->type == XCB_NONE) {
		iferr(0, "unable to get _NET_WM_STRUT_PARTIAL reply from window", e);
		rc = xcb_get_property(con, 0, p->win, netatom[NET_WM_STRUT], XCB_ATOM_CARDINAL, 0, 4);
		if (!(prop = xcb_get_property_reply(con, rc, &e)))
			iferr(0, "unable to get _NET_WM_STRUT reply from window", e);
	}
	if (prop && prop->value_len && (s = xcb_get_property_value(prop))) {
		DBG("initpanel: 0x%08x - struts: %d, %d, %d, %d", p->win, s[0], s[1], s[2], s[3])
		p->l = s[0];
		p->r = s[1];
		p->t = s[2];
		p->b = s[3];
		updstruts(p, 1);
	}
	free(prop);
	ATTACH(p, panels);
	xcb_change_window_attributes(con, p->win, XCB_CW_EVENT_MASK,
			(unsigned int[]){ XCB_EVENT_MASK_PROPERTY_CHANGE
							| XCB_EVENT_MASK_STRUCTURE_NOTIFY });
}

Rule *initrule(Rule *wr)
{
	int i;
	Rule *r;
	size_t len;
	char buf[NAME_MAX];

#define INITREG(str, wstr, reg)                                                \
	if (wstr) {                                                                \
		str = ecalloc(1, (len = strlen(wstr) + 1));                            \
		strlcpy(str, wstr, len);                                               \
		if ((i = regcomp(reg, str, REG_NOSUB|REG_EXTENDED|REG_ICASE))) {       \
			regerror(i, reg, buf, sizeof(buf));                                \
			fprintf(cmdresp, "!invalid regex %s: %s\n", str, buf);             \
			goto error;                                                        \
		}                                                                      \
	}
#define FREEREG(str, wstr, reg)                                                \
	if (wstr) {                                                                \
		regfree(reg);                                                          \
		free(str);                                                             \
	}

	r = ecalloc(1, sizeof(Rule));
	r->x = wr->x;
	r->y = wr->y;
	r->w = wr->w;
	r->h = wr->h;
	r->bw = wr->bw;
	r->xgrav = wr->xgrav;
	r->ygrav = wr->ygrav;
	r->focus = wr->focus;
	r->state = wr->state;
	r->ws = wr->ws;
	r->cb = wr->cb;
	if (wr->mon) {
		r->mon = ecalloc(1, (len = strlen(wr->mon) + 1));
		strlcpy(r->mon, wr->mon, len);
	}
	INITREG(r->title, wr->title, &(r->titlereg))
	INITREG(r->class, wr->class, &(r->classreg))
	INITREG(r->inst, wr->inst, &(r->instreg))
	ATTACH(r, rules);
	return r;

error:
	FREEREG(r->title, wr->title, &(r->titlereg))
	FREEREG(r->class, wr->class, &(r->classreg))
	FREEREG(r->inst, wr->inst, &(r->instreg))
	free(r->mon);
	free(r);
	return NULL;
#undef INITREG
#undef FREEREG
}

void initscan(void)
{
	unsigned int i;
	xcb_window_t *w;
	xcb_atom_t state;
	xcb_generic_error_t *e;
	xcb_query_tree_reply_t *rt;
	xcb_get_geometry_reply_t **g;
	xcb_get_window_attributes_reply_t **wa;

	if (!(rt = xcb_query_tree_reply(con, xcb_query_tree(con, root), &e))) {
		iferr(1, "unable to query tree from root window", e);
	} else if (rt->children_len) {
		w = xcb_query_tree_children(rt);
		g = ecalloc(rt->children_len, sizeof(xcb_get_geometry_reply_t *));
		wa = ecalloc(rt->children_len, sizeof(xcb_get_window_attributes_reply_t *));
		for (i = 0; i < rt->children_len; i++) {
			g[i] = NULL;
			if (!(wa[i] = winattr(w[i])) || !(g[i] = wingeom(w[i]))
					|| !(wa[i]->map_state == XCB_MAP_STATE_VIEWABLE
						|| (winprop(w[i], wmatom[WM_STATE], &state)
							&& state == XCB_ICCCM_WM_STATE_ICONIC)))
			{
				w[i] = XCB_WINDOW_NONE;
			} else if (!wintrans(w[i])) {
				manage(w[i], g[i], wa[i]);
				w[i] = XCB_WINDOW_NONE;
			}
		}
		for (i = 0; i < rt->children_len; i++) {
			if (w[i] != XCB_WINDOW_NONE)
				manage(w[i], g[i], wa[i]);
			free(g[i]);
			free(wa[i]);
		}
		free(g);
		free(wa);
	}
	free(rt);
}

void initsock(void)
{
	int r = 0;
	char *hostname = NULL;
	int display = 0, screen = 0;
	static struct sockaddr_un addr;

#define ENVPATH(var, str, fmt, fallback)                                          \
	do {                                                                          \
		char *env;                                                                \
		if (!(env = getenv(var))) {                                               \
			if (r || (r = xcb_parse_display(NULL, &hostname, &display, &screen))) \
				snprintf(str, sizeof(str), fmt, hostname, display, screen);       \
			else                                                                  \
				strlcpy(str, fallback, sizeof(str));                              \
			if (setenv(var, str, 0) < 0)                                          \
				warn("unable to set %s environment variable", var);               \
		} else {                                                                  \
			strlcpy(str, env, sizeof(str));                                       \
		}                                                                         \
	} while (0)

	if (sockfd <= 0) {
		ENVPATH("YAXWM_SOCK", sock, "/tmp/yaxwm_%s_%i_%i.socket", "/tmp/yaxwm.socket"); // NOLINT
		addr.sun_family = AF_UNIX;
		strlcpy(addr.sun_path, sock, sizeof(addr.sun_path));
		check((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)), "unable to create socket");
		unlink(sock);
		check(bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)), "unable to bind socket");
		check(listen(sockfd, SOMAXCONN), "unable to listen on socket");
	}
	ENVPATH("YAXWM_STATUS", status, "/tmp/yaxwm_%s_%i_%i.status", "/tmp/yaxwm.status"); // NOLINT
	free(hostname);

#undef ENVPATH
}

void initwm(void)
{
	int cws;
	xcb_atom_t r;
	Workspace *ws;
	unsigned int i;
	xcb_cursor_context_t *ctx;
	const xcb_query_extension_reply_t *ext;
	struct sigaction sa;
	int sigs[] = { SIGTERM, SIGINT, SIGHUP, SIGCHLD };

	sa.sa_handler = sighandle;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	for (i = 0; i < LEN(sigs); i++)
		check(sigaction(sigs[i], &sa, NULL), "unable to setup signal handler");

	check(xcb_cursor_context_new(con, scr, &ctx), "unable to create cursor context");
	for (i = 0; i < LEN(cursors); i++)
		cursor[i] = xcb_cursor_load_cursor(ctx, cursors[i]);
	xcb_cursor_context_free(ctx);

	initatoms(wmatom, wmatoms, LEN(wmatoms));
	initatoms(netatom, netatoms, LEN(netatoms));

	if ((ext = xcb_get_extension_data(con, &xcb_randr_id)) && ext->present) {
		randrbase = ext->first_event;
		xcb_randr_select_input(con, root, XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE);
		updrandr();
	} else {
		warnx("unable to get randr extension data");
	}
	if (randrbase < 0 || !nextmon(monitors))
		initmon(0, "default", 0, 0, 0, scr_w, scr_h);

	cws = winprop(root, netatom[NET_DESK_CUR], &r) && r < 100 ? r : 0;
	updworkspaces(MAX(cws + 1, globalcfg[GLB_NUMWS]));
	selws = workspaces;
	selmon = selws->mon;
	changews((ws = itows(cws)) ? ws : workspaces, globalcfg[GLB_STATICWS], 1);

	wmcheck = xcb_generate_id(con);
	xcb_create_window(con, XCB_COPY_FROM_PARENT, wmcheck, root, -1, -1, 1, 1, 0,
			XCB_WINDOW_CLASS_INPUT_ONLY, scr->root_visual, 0, NULL);
	PROP(REPLACE, wmcheck, netatom[NET_WM_CHECK], XCB_ATOM_WINDOW, 32, 1, &wmcheck);
	PROP(REPLACE, wmcheck, netatom[NET_WM_NAME], wmatom[WM_UTF8STR], 8, 5, "yaxwm");
	PROP(REPLACE, root, netatom[NET_WM_CHECK], XCB_ATOM_WINDOW, 32, 1, &wmcheck);
	PROP(REPLACE, root, netatom[NET_SUPPORTED], XCB_ATOM_ATOM, 32, LEN(netatom), netatom);
	xcb_delete_property(con, root, netatom[NET_CLIENTS]);

	iferr(1, "unable to change root window event mask or cursor",
			xcb_request_check(con,
				xcb_change_window_attributes_checked(
					con, root, XCB_CW_EVENT_MASK | XCB_CW_CURSOR,
					(unsigned int[]){XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT
									| XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY
									| XCB_EVENT_MASK_BUTTON_PRESS
									| XCB_EVENT_MASK_POINTER_MOTION
									| XCB_EVENT_MASK_ENTER_WINDOW
									| XCB_EVENT_MASK_LEAVE_WINDOW
									| XCB_EVENT_MASK_STRUCTURE_NOTIFY
									| XCB_EVENT_MASK_PROPERTY_CHANGE,
									cursor[CURS_NORMAL]})));

	if (!(keysyms = xcb_key_symbols_alloc(con)))
		err(1, "unable to get keysyms from X connection");
}

Workspace *initws(int num)
{
	Workspace *ws, *tail;

	DBG("initws: %d", num)
	ws = ecalloc(1, sizeof(Workspace));
	ws->num = num;
	itoa(num + 1, ws->name);
	ws->gappx = MAX(0, wsdef.gappx);
	ws->layout = wsdef.layout;
	ws->nmaster = MAX(0, wsdef.nmaster);
	ws->nstack = MAX(0, wsdef.nstack);
	ws->msplit = CLAMP(wsdef.msplit, 0.05, 0.95);
	ws->ssplit = CLAMP(wsdef.ssplit, 0.05, 0.95);
	ws->padl = MAX(0, wsdef.padl);
	ws->padr = MAX(0, wsdef.padr);
	ws->padt = MAX(0, wsdef.padt);
	ws->padb = MAX(0, wsdef.padb);
	FIND_TAIL(tail, workspaces);
	if (tail)
		tail->next = ws;
	else
		workspaces = ws;
	return ws;
}

char *itoa(int n, char *s)
{
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
		char c = s[i];
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

void manage(xcb_window_t win, xcb_get_geometry_reply_t *g, xcb_get_window_attributes_reply_t *wa)
{
	xcb_atom_t type;

	DBG("manage: 0x%08x - %d,%d @ %dx%d", win, g->x, g->y, g->width, g->height)
	if (!wintoclient(win) && !wintopanel(win) && !wintodesk(win)) {
		if (winprop(win, netatom[NET_WM_TYPE], &type)) {
			if (type == netatom[NET_TYPE_DOCK])
				initpanel(win, g);
			else if (type == netatom[NET_TYPE_DESK])
				initdesk(win, g);
			else if (type != netatom[NET_TYPE_SPLASH] && !wa->override_redirect)
				initclient(win, g);
		} else if (!wa->override_redirect) {
			initclient(win, g);
		}
		PROP(APPEND, root, netatom[NET_CLIENTS], XCB_ATOM_WINDOW, 32, 1, &win);
		setwmwinstate(win, XCB_ICCCM_WM_STATE_NORMAL);
		needsrefresh = 1;
	}
}

int mono(Workspace *ws)
{
	int g;
	Client *c;

	g = globalcfg[GLB_SMART_GAP] ? 0 : ws->gappx;
	for (c = nexttiled(ws->clients); c; c = nexttiled(c->next)) {
		int b = globalcfg[GLB_SMART_BORDER] ? 0 : c->bw;
		resizehint(c, ws->mon->wx + ws->padl + g, ws->mon->wy + ws->padt + g,
				ws->mon->ww - ws->padl - ws->padr - (2 * g) - (2 * b),
				ws->mon->wh - ws->padt - ws->padb - (2 * g) - (2 * b),
				globalcfg[GLB_SMART_BORDER] ? 0 : c->bw, 0, 0);
	}
	return 1;
}

void movefocus(int direction)
{
	Client *c;

	if (!selws->sel || !selws->clients->next)
		return;
	while (direction) {
		if (direction > 0) {
			c = selws->sel->next ? selws->sel->next : selws->clients;
			direction--;
		} else {
			FIND_PREV(c, selws->sel, selws->clients);
			direction++;
		}
		if (c) {
			focus(c);
			restack(c->ws);
		}
	}
}

void movestack(int direction)
{
	int i;
	Client *c, *t;

	if (!(c = cmdclient) || FLOATING(c) || !nexttiled(c->ws->clients->next))
		return;
	while (direction) {
		if (direction > 0) {
			detach(c, (t = nexttiled(c->next)) ? 0 : 1);
			if (t)
				ATTACH(c, t->next);
			direction--;
		} else {
			if (c == nexttiled(c->ws->clients)) {
				detach(c, 0);
				attach(c, 0);
			} else {
				for (t = nexttiled(c->ws->clients); t && nexttiled(t->next)
						&& nexttiled(t->next) != c; t = nexttiled(t->next))
					;
				detach(c, (i = (t == nexttiled(c->ws->clients)) ? 1 : 0));
				if (!i) {
					c->next = t;
					FIND_PREV(t, c->next, c->ws->clients);
					t->next = c;
				}
			}
			direction++;
		}
	}
	needsrefresh = 1;
}

Monitor *nextmon(Monitor *m)
{
	while (m && !m->connected)
		m = m->next;
	return m;
}

Client *nexttiled(Client *c)
{
	while (c && FLOATING(c))
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

void popfloat(Client *c)
{
	int x, y, w, h;

	c->state |= STATE_FLOATING;
	x = c->x;
	y = c->y;
	w = CLAMP(c->w, c->ws->mon->ww / 8, c->ws->mon->ww / 3);
	h = CLAMP(c->h, c->ws->mon->wh / 8, c->ws->mon->wh / 3);
	quadrant(c, &x, &y, &w, &h);
	setstackmode(c->win, XCB_STACK_MODE_ABOVE);
	resizehint(c, x, y, w, h, c->bw, 0, 0);
}

void pushstatus(void)
{
	FILE *f;
	Rule *r;
	Client *c;
	Monitor *m;
	Workspace *ws;

	if (!(f = fopen(status, "w"))) {
		warn("unable to open status file: %s", status);
		return;
	}
	fprintf(f, "# globals - key: value ...\nnumws: %d\nsmart_border: %d\n"
			"smart_gap: %d\nfocus_urgent: %d\nfocus_mouse: %d\n"
			"tile_hints: %d\nwin_minxy: %d\nwin_minwh: %d",
			globalcfg[GLB_NUMWS], globalcfg[GLB_SMART_BORDER],
			globalcfg[GLB_SMART_GAP], globalcfg[GLB_FOCUS_URGENT],
			globalcfg[GLB_FOCUS_MOUSE], globalcfg[GLB_TILEHINTS],
			globalcfg[GLB_MIN_XY], globalcfg[GLB_MIN_WH]);
	fprintf(f, "\n\n# width outer_width focus urgent unfocus "
			"outer_focus outer_urgent outer_unfocus\n"
			"border: %u %u #%08x #%08x #%08x #%08x #%08x #%08x",
			border[BORD_WIDTH], border[BORD_O_WIDTH],
			border[BORD_FOCUS], border[BORD_URGENT],
			border[BORD_UNFOCUS], border[BORD_O_FOCUS],
			border[BORD_O_URGENT], border[BORD_O_UNFOCUS]);
	fprintf(f, "\n\n# number:name:layout ...\nworkspaces:");
	FOR_EACH(ws, workspaces)
		fprintf(f, " %s%d:%s:%s", ws == selws ? "*" : "", ws->num + 1, ws->name, ws->layout->name);
	fprintf(f, "\n\t# number:name active_window nmaster "
			"nstack msplit ssplit gappx padl padr padt padb");
	FOR_EACH(ws, workspaces)
		fprintf(f, "\n\t%d:%s #%08x %d %d %0.2f %0.2f %d %d %d %d %d",
				ws->num + 1, ws->name, ws->sel ? ws->sel->win : 0, ws->nmaster, ws->nstack,
				ws->msplit, ws->ssplit, ws->gappx, ws->padl, ws->padr, ws->padt, ws->padb);
	fprintf(f, "\n\n# number:name:workspace ...\nmonitors:");
	FOR_EACH(m, monitors)
		if (m->connected)
			fprintf(f, " %s%d:%s:%d", m->ws == selws ? "*" : "", m->num + 1, m->name, m->ws->num + 1);
	fprintf(f, "\n\t# number:name active_window x y width height wx wy wwidth wheight");
	FOR_EACH(m, monitors)
		if (m->connected)
			fprintf(f, "\n\t%d:%s #%08x %d %d %d %d %d %d %d %d",
					m->num + 1, m->name, m->ws->sel ? m->ws->sel->win : 0,
					m->x, m->y, m->w, m->h, m->wx, m->wy, m->ww, m->wh);
	fprintf(f, "\n\n# id:workspace ...\nwindows:");
	FOR_CLIENTS(c, ws)
		fprintf(f, " %s#%08x:%d", c == selws->sel ? "*" : "", c->win, c->ws->num + 1);
	fprintf(f, "\n\t# id title class instance x y width height bw hoff "
			"float full fakefull fixed stick urgent callback trans_id");
	FOR_CLIENTS(c, ws)
		fprintf(f, "\n\t#%08x \"%s\" \"%s\" \"%s\" %d %d %d %d %d %d %d %d %d %d %d %d %s #%08x",
				c->win, c->title, c->class, c->inst, c->x, c->y, c->w, c->h, c->bw,
				c->hoff, FLOATING(c), (c->state & STATE_FULLSCREEN) != 0,
				(c->state & STATE_FAKEFULL) != 0, (c->state & STATE_FIXED) != 0,
				(c->state & STATE_STICKY) != 0, (c->state & STATE_URGENT) != 0,
				c->cb ? c->cb->name : "", c->trans ? c->trans->win : 0);
	fprintf(f, "\n\n# title class instance workspace monitor float "
			"stick focus callback x y width height xgrav ygrav");
	FOR_EACH(r, rules)
		fprintf(f, "\nrule: \"%s\" \"%s\" \"%s\" %d %s %d %d %d %s %d %d %d %d %s %s",
				r->title, r->class, r->inst, r->ws, r->mon, (r->state & STATE_FLOATING) != 0,
				(r->state & STATE_STICKY) != 0, r->focus, r->cb ? r->cb->name : "",
				r->x, r->y, r->w, r->h, gravities[r->xgrav], gravities[r->ygrav]);
	fprintf(f, "\n");
	fflush(f);
	fclose(f);
}

void quadrant(Client *c, int *x, int *y, int *w, int *h)
{
	Client *t;
	Monitor *m = c->ws->mon;
	static int index = 0;
	static Workspace *ws = NULL;
	unsigned int i = 0;
	int thirdw = m->ww / 3;
	int thirdh = m->wh / 3;
	int quadrants[][3] = {
		{ 1, m->wx + thirdw,       m->wy + thirdh       },
		{ 1, m->wx + (thirdw * 2), m->wy + thirdh       },
		{ 1, m->wx,                m->wy + thirdh       },
		{ 1, m->wx + thirdw,       m->wy,               },
		{ 1, m->wx + (thirdw * 2), m->wy,               },
		{ 1, m->wx,                m->wy,               },
		{ 1, m->wx + thirdw,       m->wy + (2 * thirdh) },
		{ 1, m->wx + (thirdw * 2), m->wy + (2 * thirdh) },
		{ 1, m->wx,                m->wy + (2 * thirdh) }
	};

	if (ws != c->ws) {
		ws = c->ws;
		index = 0;
	}
	FOR_EACH(t, c->ws->clients)
		if (FLOATING(t) && t != c)
			for (i = 0; i < LEN(quadrants); i++)
				if (quadrants[i][0] && (t->x >= quadrants[i][1] && t->y >= quadrants[i][2]
							&& t->x < quadrants[i][1] + thirdw && t->y < quadrants[i][2] + thirdh))
				{
					quadrants[i][0] = 0;
					break;
				}
	for (i = 0; i < LEN(quadrants); i++)
		if (quadrants[i][0])
			break;
	if (i == LEN(quadrants)) {
		i = index;
		index = (index + 1) % LEN(quadrants);
	}
	*x = quadrants[i][1] + (((*w - thirdw) * -1) / 2);
	*y = quadrants[i][2] + (((*h - thirdh) * -1) / 2);
}

void refresh(void)
{
	Desk *d;
	Panel *p;
	Client *c;
	Monitor *m;
	Workspace *ws;

#define MAP(v, list)                         \
	do {                                     \
		FOR_EACH(v, list)                    \
			if (v->state & STATE_NEEDSMAP) { \
				v->state &= ~STATE_NEEDSMAP; \
				xcb_map_window(con, v->win); \
			}                                \
	} while (0)

	MAP(p, panels);
	MAP(d, desks);
	FOR_EACH(ws, workspaces) {
		showhide(ws->stack);
		if (ws == ws->mon->ws && ws->layout->func)
			ws->layout->func(ws);
		MAP(c, ws->clients);
	}
	for (m = nextmon(monitors); m; m = nextmon(m->next))
		restack(m->ws);
	focus(NULL);
	eventignore(XCB_ENTER_NOTIFY);
	pushstatus();

#undef MAP
}

void relocate(Client *c, Monitor *new, Monitor *old)
{
#define RELOC(val, opposed, offset, min, max, wmin, wmax, oldmin, oldmax, oldwmin, oldwmax) \
		if (val - oldwmin > 0 && (offset = oldwmax / (val - oldwmin)) != 0.0) {             \
			if (val + (opposed) == oldmin + oldmax) {                                       \
				val = min + max - (opposed);                                                \
			} else if (val + ((opposed) / 2) == oldmin + (oldmax / 2)) {                    \
				val = (min + max - (opposed)) / 2;                                          \
			} else {                                                                        \
				val = CLAMP(min + (max / offset), min - ((opposed) - globalcfg[GLB_MIN_XY]),\
						min + max - globalcfg[GLB_MIN_XY]);                                 \
			}                                                                               \
		} else {                                                                            \
			val = CLAMP(val, min - ((opposed) - globalcfg[GLB_MIN_XY]),                     \
					wmin + wmax - globalcfg[GLB_MIN_XY]);                                   \
		}

	if (!FLOATING(c))
		return;
	DBG("relocate: 0x%08x - current geom: %d,%d %dx%d", c->win, c->x, c->y, c->w, c->h)
	if (c->state & STATE_FULLSCREEN && c->w == old->w && c->h == old->h) {
		c->x = new->x, c->y = new->y, c->w = new->w, c->h = new->h;
	} else {
		float f;
		RELOC(c->x, W(c), f, new->wx, new->ww, new->x, new->w, old->wx, old->ww, old->x, old->w)
		RELOC(c->y, H(c), f, new->wy, new->wh, new->y, new->h, old->wy, old->wh, old->y, old->h)
	}
	DBG("relocate: 0x%08x - new geom: %d,%d %dx%d", c->win, c->x, c->y, c->w, c->h)

#undef RELOC
}

void relocatews(Workspace *ws, Monitor *old)
{
	Client *c;
	Monitor *new;

	if (!(new = ws->mon) || new == old)
		return;
	DBG("relocatews: %d:%s -> %d:%s", old->ws->num, old->name, new->ws->num, new->name)
	FOR_EACH(c, ws->clients)
		relocate(c, new, old);
}

void resize(Client *c, int x, int y, int w, int h, int bw)
{
	SAVEOLD(c);
	c->x = x, c->y = y, c->w = w, c->h = h;
	CMOVERESIZE(c, x, y, w, h, bw);
}

void resizehint(Client *c, int x, int y, int w, int h, int bw, int confine, int mouse)
{
	if (applysizehints(c, &x, &y, &w, &h, bw, confine, mouse))
		resize(c, x, y, w, h, bw);
}

void restack(Workspace *ws)
{
	Desk *d;
	Panel *p;
	Client *c;

	if (!ws || !(ws = selws) || !(c = ws->sel))
		return;
	FOR_EACH(p, panels)
		if (p->mon == ws->mon)
			setstackmode(p->win, XCB_STACK_MODE_ABOVE);
	if (FLOATING(c))
		setstackmode(c->win, XCB_STACK_MODE_ABOVE);
	if (ws->layout->func)
		for (c = ws->stack; c; c = c->snext)
			if (!FLOATING(c) && ws == ws->mon->ws)
				setstackmode(c->win, XCB_STACK_MODE_BELOW);
	FOR_EACH(d, desks)
		if (d->mon == ws->mon)
			setstackmode(d->win, XCB_STACK_MODE_BELOW);
}

int rulecmp(Client *c, Rule *r)
{
	DBG("rulecmp: %s - %s, %s - %s, %s - %s", r->class, c->class, r->inst, c->inst, r->title, c->title)
	return !((r->class && regexec(&(r->classreg), c->class, 0, NULL, 0))
			|| (r->inst && regexec(&(r->instreg), c->inst, 0, NULL, 0))
			|| (r->title && regexec(&(r->titlereg), c->title, 0, NULL, 0)));
}

void sendconfigure(Client *c)
{
	xcb_configure_notify_event_t ce;

	DBG("sendconfigure: sending 0x%08x configure notify event", c->win)
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
	xcb_send_event(con, 0, c->win, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (char *)&ce);
}

int sendwmproto(Client *c, int wmproto)
{
	int exists = 0;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t rpc;
	xcb_client_message_event_t cme;
	xcb_icccm_get_wm_protocols_reply_t proto;

	DBG("sendwmproto: checking support for %s - 0x%08x", wmatoms[wmproto], c->win)
	rpc = xcb_icccm_get_wm_protocols(con, c->win, wmatom[WM_PROTO]);
	if (xcb_icccm_get_wm_protocols_reply(con, rpc, &proto, &e)) {
		int n = proto.atoms_len;
		while (!exists && n--)
			exists = proto.atoms[n] == wmatom[wmproto];
		xcb_icccm_get_wm_protocols_reply_wipe(&proto);
	} else {
		iferr(0, "unable to get requested wm protocol", e);
	}
	if (exists) {
		DBG("sendwmproto: %s client message event -> 0x%08x", wmatoms[wmproto], c->win)
		cme.response_type = XCB_CLIENT_MESSAGE;
		cme.window = c->win;
		cme.type = wmatom[WM_PROTO];
		cme.format = 32;
		cme.data.data32[0] = wmatom[wmproto];
		cme.data.data32[1] = XCB_TIME_CURRENT_TIME;
		iferr(0, "unable to send client message event", xcb_request_check(con,
					xcb_send_event_checked(con, 0, c->win, XCB_EVENT_MASK_NO_EVENT, (char *)&cme)));
	}
	return exists;
}

void setfullscreen(Client *c, int fullscreen)
{
	Monitor *m;

	if (!c->ws || !(m = c->ws->mon))
		m = selws->mon;
	if (fullscreen && !(c->state & STATE_FULLSCREEN)) {
		PROP(REPLACE, c->win, netatom[NET_WM_STATE], XCB_ATOM_ATOM, 32, 1, &netatom[NET_STATE_FULL]);
		c->old_state = c->state;
		c->state |= STATE_FULLSCREEN | STATE_FLOATING;
		resize(c, m->x, m->y, m->w, m->h, 0);
		setstackmode(c->win, XCB_STACK_MODE_ABOVE);
	} else if (!fullscreen && (c->state & STATE_FULLSCREEN)) {
		PROP(REPLACE, c->win, netatom[NET_WM_STATE], XCB_ATOM_ATOM, 32, 0, (unsigned char *)0);
		c->state = c->old_state;
		resize(c, c->old_x, c->old_y, c->old_w, c->old_h, c->bw);
		needsrefresh = 1;
	}
}

void setinputfocus(Client *c)
{
	if (!(c->state & STATE_NOINPUT)) {
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, c->win, XCB_CURRENT_TIME);
		PROP(REPLACE, root, netatom[NET_ACTIVE], XCB_ATOM_WINDOW, 32, 1, &c->win);
	}
	sendwmproto(c, WM_FOCUS);
}

void setstackmode(xcb_window_t win, unsigned int mode)
{
	xcb_configure_window(con, win, XCB_CONFIG_WINDOW_STACK_MODE, &mode);
}

void setwmwinstate(xcb_window_t win, long state)
{
	long data[] = { state, XCB_ATOM_NONE };
	PROP(REPLACE, win, wmatom[WM_STATE], wmatom[WM_STATE], 32, 2, (unsigned char *)data);
}

void setnetwsnames(void)
{
	unsigned int i;
	char *names;
	Workspace *ws;
	size_t len = 1;

	FOR_EACH(ws, workspaces)
		len += strlen(ws->name) + 1;
	names = ecalloc(1, len);
	len = 0;
	FOR_EACH(ws, workspaces)
		for (i = 0; (names[len++] = ws->name[i]); i++)
			;
	PROP(REPLACE, root, netatom[NET_DESK_NAMES], wmatom[WM_UTF8STR], 8, --len, names);
	free(names);
}

void setworkspace(Client *c, int num, int stacktail)
{
	Workspace *ws;
	Client *tail = NULL;

	if (!(ws = itows(num)) || ws == c->ws)
		return;
	DBG("setworkspace: 0x%08x -> %d", c->win, num)
	if (c->ws) {
		detach(c, 0);
		detachstack(c);
	}
	c->ws = ws;
	PROP(REPLACE, c->win, netatom[NET_WM_DESK], XCB_ATOM_CARDINAL, 32, 1, &c->ws->num);
	attach(c, globalcfg[GLB_TILETOHEAD]);

	if (stacktail)
		for (tail = ws->stack; tail && tail->snext; tail = tail->snext)
			;
	if (tail) {
		tail->snext = c;
		c->snext = NULL;
	} else {
		c->snext = c->ws->stack;
		c->ws->stack = c;
	}
}

void seturgent(Client *c, int urg)
{
	xcb_generic_error_t *e;
	xcb_icccm_wm_hints_t wmh;
	xcb_get_property_cookie_t pc;

	DBG("seturgent: 0x%08x -> %d", c->win, urg)
	pc = xcb_icccm_get_wm_hints(con, c->win);
	if (c != selws->sel && urg) {
		c->state |= STATE_URGENT;
		drawborder(c, 0);
	}
	if (xcb_icccm_get_wm_hints_reply(con, pc, &wmh, &e)) {
		wmh.flags = urg
			? (wmh.flags | XCB_ICCCM_WM_HINT_X_URGENCY)
			: (wmh.flags & ~XCB_ICCCM_WM_HINT_X_URGENCY);
		xcb_icccm_set_wm_hints(con, c->win, &wmh);
	} else {
		iferr(0, "unable to get wm window hints", e);
	}
}

void showhide(Client *c)
{
	Monitor *m;

	if (!c)
		return;
	m = c->ws->mon;
	if (c->ws == m->ws) {
		if (c->state & STATE_NEEDSRESIZE) {
			MOVERESIZE(c->win, c->x, c->y, c->w, c->h, FULLSCREEN(c) ? 0 : c->bw);
			c->state &= ~STATE_NEEDSRESIZE;
		} else {
			MOVE(c->win, c->x, c->y);
		}
		if (FLOATING(c)) {
			if (c->state & STATE_FULLSCREEN && c->w == m->w && c->h == m->h) {
				DBG("showhide: moving fullscreen client - %d,%d %dx%d -> %d,%d %dx%d",
						c->x, c->y, c->w, c->h, m->x, m->y, m->w, m->h)
				resize(c, m->x, m->y, m->w, m->h, 0);
			} else {
				DBG("showhide: moving floating client - %d,%d %dx%d",
						c->x, c->y, c->w, c->h)
				resize(c, c->x, c->y, c->w, c->h, FULLSCREEN(c) ? 0 : c->bw);
			}
		}
		showhide(c->snext);
	} else {
		showhide(c->snext);
		if (!(c->state & STATE_STICKY)) {
			MOVE(c->win, W(c) * -2, c->y);
		} else if (c->ws != selws && m == selws->mon) {
			Client *sel = lastws->sel == c ? c : selws->sel;
			setworkspace(c, selws->num, 0);
			focus(sel);
		}
	}
}

void sighandle(int sig)
{
	switch (sig) {
	case SIGINT: /* FALLTHROUGH */
	case SIGTERM: /* FALLTHROUGH */
	case SIGHUP:
		exit(1);
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
	DBG("sizehints: getting size hints - 0x%08x", c->win)
	c->inc_w = c->inc_h = 0;
	c->max_aspect = c->min_aspect = 0.0;
	c->min_w = c->min_h = c->max_w = c->max_h = c->base_w = c->base_h = 0;
	if (xcb_icccm_get_wm_normal_hints_reply(con, pc, &s, &e)) {
		if (uss && s.flags & XCB_ICCCM_SIZE_HINT_US_SIZE) {
			c->w = s.width;
			c->h = s.height;
		}
		if (uss && s.flags & XCB_ICCCM_SIZE_HINT_US_POSITION) {
			c->x = s.x;
			c->y = s.y;
		}
		if (s.flags & XCB_ICCCM_SIZE_HINT_P_ASPECT) {
			c->min_aspect = (float)s.min_aspect_den / s.min_aspect_num;
			c->max_aspect = (float)s.max_aspect_num / s.max_aspect_den;
		}
		if (s.flags & XCB_ICCCM_SIZE_HINT_P_MAX_SIZE)
			c->max_w = s.max_width, c->max_h = s.max_height;
		if (s.flags & XCB_ICCCM_SIZE_HINT_P_RESIZE_INC)
			c->inc_w = s.width_inc, c->inc_h = s.height_inc;
		if (s.flags & XCB_ICCCM_SIZE_HINT_BASE_SIZE)
			c->base_w = s.base_width, c->base_h = s.base_height;
		else if (s.flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE)
			c->base_w = s.min_width, c->base_h = s.min_height;
		if (s.flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE)
			c->min_w = s.min_width, c->min_h = s.min_height;
		else if (s.flags & XCB_ICCCM_SIZE_HINT_BASE_SIZE)
			c->min_w = s.base_width, c->min_h = s.base_height;
	} else {
		iferr(0, "unable to get wm normal hints", e);
	}
	if (c->max_w && c->max_h && c->max_w == c->min_w && c->max_h == c->min_h)
		c->state |= STATE_FIXED | STATE_FLOATING;
}

int spiral(Workspace *ws)
{
	return fib(ws, 0);
}

int tile(Workspace *ws)
{
	Monitor *m = ws->mon;
	Client *c, *prev = NULL;
	int x, *y, wx, wy, ww, wh, mw, ss, sw, ssw, ns = 1;
	int i, n, remaining, my, sy, ssy, g, ret = 1;

	for (n = 0, c = nexttiled(ws->clients); c; c = nexttiled(c->next), n++)
		;
	if (!n) return 1;

	mw = ss = sw = ssw = 0;
	wx = m->wx + ws->padl;
	wy = m->wy + ws->padt;
	ww = m->ww - ws->padl - ws->padr;
	wh = m->wh - ws->padt - ws->padb;
	g = !globalcfg[GLB_SMART_GAP] || n > 1 ? ws->gappx : 0;

	if (n <= ws->nmaster)
		mw = ww, ss = 1;
	else if (ws->nmaster)
		ns = 2, mw = ww * ws->msplit;

	if (n - ws->nmaster <= ws->nstack)
		sw = ww - mw;
	else if (ws->nstack)
		sw = (ww - mw) * ws->ssplit;

	if (n - ws->nmaster > ws->nstack)
		ss = 1, ssw = ww - mw - sw;

	DBG("tile: ws: %d - h: %d - mw: %d - sw: %d - ssw: %d", ws->num, m->ww, mw, sw, ssw)

	for (i = 0, my = sy = ssy = g, c = nexttiled(ws->clients); c; c = nexttiled(c->next), ++i) {
		SAVEOLD(c);
		if (i < ws->nmaster) {
			remaining = MIN(n, ws->nmaster) - i;
			x = g;
			y = &my;
			c->w = mw - g * (5 - ns) / 2;
		} else if (i - ws->nmaster < ws->nstack) {
			remaining = MIN(n - ws->nmaster, ws->nstack) - (i - ws->nmaster);
			x = mw + (g / ns);
			y = &sy;
			c->w = sw - g * (5 - ns - ss) / 2;
		} else {
			remaining = n - i;
			x = mw + sw + (g / ns);
			y = &ssy;
			c->w = ssw - g * (5 - ns) / 2;
		}

		int bw = !globalcfg[GLB_SMART_BORDER] || n > 1 ? c->bw : 0;
		int available = wh - (*y + c->h + g);
		int minh = MAX(globalcfg[GLB_MIN_WH], c->min_h);
		c->x = wx + x, c->y = wy + *y;
		c->h = ((wh - *y) / MAX(1, remaining)) - g + c->hoff;

		if (!c->hoff && c->h < minh) {
			popfloat(c);
		} else if (remaining > 1 && (remaining - 1) * (minh + g) > available) {
			c->h += available - ((remaining - 1) * (minh + g));
			ret = -1;
		} else if (remaining == 1 && *y + (c->h - g) != wh - (2 * g)) {
			if (prev) {
				prev->old_h = prev->h;
				minh = MAX(globalcfg[GLB_MIN_WH], prev->min_h);
				if (prev->h + available < minh) {
					ret = -1;
					prev->h = minh;
					c->y = prev->y + minh + g;
					c->h = (wh - (2 * g)) - (prev->y + prev->h);
				} else if (c->h < minh) {
					ret = -1;
					prev->h += available - (minh - c->h - (2 * bw));
					c->y = prev->y + prev->h + (2 * bw) + g;
					c->h = minh - (2 * bw);
				} else {
					prev->h += available;
					c->y += available;
				}
				CMOVERESIZE(prev, prev->x, prev->y, prev->w, prev->h, prev->bw);
			} else {
				c->h = wh - (2 * g);
				ret = -1;
			}
		} else if (c->h < minh) {
			ret = -1;
			c->h = minh;
		}
		*y += c->h + g;
		c->w -= 2 * bw;
		c->h -= 2 * bw;
		CMOVERESIZE(c, c->x, c->y, c->w, c->h, bw);
		prev = (remaining == 1 && n - i != 0) ? NULL : c;
	}
	return ret;
}

void unfocus(Client *c, int focusroot)
{
	if (c) {
		grabbuttons(c, 0);
		drawborder(c, 0);
	}
	if (focusroot) {
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
		xcb_delete_property(con, root, netatom[NET_ACTIVE]);
	}
}

void unmanage(xcb_window_t win, int destroyed)
{
	void *ptr;
	Client *c;
	Desk *d, **dd;
	Panel *p, **pp;
	Workspace *ws;

	if ((ptr = c = wintoclient(win))) {
		needsrefresh = 1;
		if (c->cb && running)
			c->cb->func(c, 1);
		detach(c, 0);
		detachstack(c);
	} else if ((ptr = p = wintopanel(win))) {
		pp = &panels;
		DETACH(p, pp);
		updstruts(p, 0);
		needsrefresh = 1;
	} else if ((ptr = d = wintodesk(win))) {
		dd = &desks;
		DETACH(d, dd);
	}

	if (!destroyed) {
		xcb_grab_server(con);
		if (c) {
			xcb_configure_window(con, c->win, XCB_CONFIG_WINDOW_BORDER_WIDTH, &c->old_bw);
			xcb_ungrab_button(con, XCB_BUTTON_INDEX_ANY, c->win, XCB_MOD_MASK_ANY);
			if (running) {
				xcb_delete_property(con, c->win, netatom[NET_WM_STATE]);
				xcb_delete_property(con, c->win, netatom[NET_WM_DESK]);
			}
		}
		setwmwinstate(win, XCB_ICCCM_WM_STATE_WITHDRAWN);
		xcb_aux_sync(con);
		xcb_ungrab_server(con);
	} else {
		xcb_flush(con);
	}

	if (ptr) {
		free(ptr);
		xcb_delete_property(con, root, netatom[NET_CLIENTS]);
		FOR_CLIENTS(c, ws) // NOLINT
			PROP(APPEND, root, netatom[NET_CLIENTS], XCB_ATOM_WINDOW, 32, 1, &c->win);
		FOR_EACH(p, panels)
			PROP(APPEND, root, netatom[NET_CLIENTS], XCB_ATOM_WINDOW, 32, 1, &p->win);
		FOR_EACH(d, desks)
			PROP(APPEND, root, netatom[NET_CLIENTS], XCB_ATOM_WINDOW, 32, 1, &d->win);
	}
}

int updoutputs(xcb_randr_output_t *outs, int nouts, xcb_timestamp_t t)
{
	Monitor *m;
	unsigned int n;
	char name[64];
	int i, nmons, changed = 0;
	xcb_generic_error_t *e;
	xcb_randr_get_crtc_info_cookie_t ck;
	xcb_randr_get_crtc_info_reply_t *crtc;
	xcb_randr_get_output_info_reply_t *o;
	xcb_randr_get_output_info_cookie_t oc[nouts];
	xcb_randr_get_output_primary_reply_t *po = NULL;


	DBG("updoutputs: checking %d outputs for changes", nouts)
	for (i = 0; i < nouts; i++)
		oc[i] = xcb_randr_get_output_info(con, outs[i], t);
	for (i = 0, nmons = 0; i < nouts; i++) {
		if (!(o = xcb_randr_get_output_info_reply(con, oc[i], &e)) || o->crtc == XCB_NONE) {
			iferr(0, "unable to get output info or output has no crtc", e);
		} else if (o->connection == XCB_RANDR_CONNECTION_CONNECTED) {
			ck = xcb_randr_get_crtc_info(con, o->crtc, t);
			crtc = xcb_randr_get_crtc_info_reply(con, ck, &e);
			if (!crtc || !xcb_randr_get_crtc_info_outputs_length(crtc)) {
				iferr(0, "unable to get crtc info reply", e);
				goto out;
			}
			n = xcb_randr_get_output_info_name_length(o) + 1;
			strlcpy(name, (char *)xcb_randr_get_output_info_name(o), MIN(sizeof(name), n));
			FOR_EACH(m, monitors) {
				if (outs[i] != m->id && m->x == crtc->x && m->y == crtc->y) {
					DBG("updoutput: %s is a clone of %s", name, m->name)
					goto out;
				}
			}
			if ((m = outputtomon(outs[i]))) {
				if (!m->connected || crtc->x != m->x || crtc->y != m->y
						|| crtc->width != m->w || crtc->height != m->h)
					changed = 1;
				m->num = nmons++;
				m->x = m->wx = crtc->x;
				m->y = m->wy = crtc->y;
				m->w = m->ww = crtc->width;
				m->h = m->wh = crtc->height;
				m->connected = 1;
			} else {
				initmon(nmons++, name, outs[i], crtc->x, crtc->y, crtc->width, crtc->height);
				changed = 1;
			}
			DBG("updoutputs: %s - %d,%d @ %dx%d - changed: %d", name,
					crtc->x, crtc->y, crtc->width, crtc->height, changed)
out:
			free(crtc);
		} else if (o->connection == XCB_RANDR_CONNECTION_DISCONNECTED
				&& (m = outputtomon(outs[i])))
		{
			if (m->connected)
				changed = 1, m->connected = 0, m->num = -1;
		}
		free(o);
	}

	if (changed) {
		po = xcb_randr_get_output_primary_reply(con, xcb_randr_get_output_primary(con, root), NULL);
		if (!po || !(primary = outputtomon(po->output)))
			primary = nextmon(monitors);
		free(po);
	}
	return changed;
}

int updrandr(void)
{
	int changed = 0;
	xcb_generic_error_t *e;
	xcb_randr_get_screen_resources_reply_t *r;
	xcb_randr_get_screen_resources_cookie_t rc;

	rc = xcb_randr_get_screen_resources(con, root);
	if ((r = xcb_randr_get_screen_resources_reply(con, rc, &e))) {
		int n;
		if ((n = xcb_randr_get_screen_resources_outputs_length(r)) <= 0)
			warnx("no monitors available");
		else
			changed = updoutputs(xcb_randr_get_screen_resources_outputs(r), n, r->config_timestamp);
		free(r);
	} else {
		iferr(0, "unable to get screen resources", e);
	}
	return changed;
}

void updstruts(Panel *p, int apply)
{
	Panel *n;
	Monitor *m;

	FOR_EACH(m, monitors)
		m->wx = m->x, m->wy = m->y, m->ww = m->w, m->wh = m->h;
	if (p) {
		if (apply && !panels)
			applypanelstrut(p);
		FOR_EACH(n, panels)
			if ((apply || n != p) && (n->l || n->r || n->t || n->b))
				applypanelstrut(p);
	}
	updnetworkspaces();
}

void updnetworkspaces(void)
{
	int v[4];
	Workspace *ws;

	xcb_delete_property(con, root, netatom[NET_DESK_VP]);
	xcb_delete_property(con, root, netatom[NET_DESK_WA]);
	v[0] = scr_w, v[1] = scr_h;
	PROP(REPLACE, root, netatom[NET_DESK_GEOM], XCB_ATOM_CARDINAL, 32, 2, &v);
	PROP(REPLACE, root, netatom[NET_DESK_NUM], XCB_ATOM_CARDINAL, 32, 1, &globalcfg[GLB_NUMWS]);
	FOR_EACH(ws, workspaces) {
		if (!ws->mon)
			ws->mon = primary;
		v[0] = ws->mon->x, v[1] = ws->mon->y;
		PROP(APPEND, root, netatom[NET_DESK_VP], XCB_ATOM_CARDINAL, 32, 2, &v);
		v[0] = ws->mon->wx, v[1] = ws->mon->wy, v[2] = ws->mon->ww, v[3] = ws->mon->wh;
		PROP(APPEND, root, netatom[NET_DESK_WA], XCB_ATOM_CARDINAL, 32, 4, &v);
	}
}

void updworkspaces(int needed)
{
	int n;
	Desk *d;
	Panel *p;
	Client *c;
	Monitor *m;
	Workspace *ws;

	for (n = 0, m = nextmon(monitors); m; m = nextmon(m->next), n++)
		;
	if (n < 1 || n > 99 || needed > 99) {
		warnx(n < 1 ? "no connected monitors" : "allocating too many workspaces: max 99");
		return;
	} else while (n > globalcfg[GLB_NUMWS] || needed > globalcfg[GLB_NUMWS]) {
		initws(globalcfg[GLB_NUMWS]);
		globalcfg[GLB_NUMWS]++;
	}

	m = nextmon(monitors);
	FOR_EACH(ws, workspaces) {
		m->ws = m->ws ? m->ws : ws;
		ws->mon = m;
		DBG("updworkspaces: %d:%s -> %s - visible: %d", ws->num, ws->name, m->name, ws == m->ws)
		if (!(m = nextmon(m->next)))
			m = nextmon(monitors);
	}

	FOR_CLIENTS(c, ws)
		if (c->state & STATE_FULLSCREEN && c->w == c->ws->mon->w && c->h == c->ws->mon->h)
			resize(c, ws->mon->x, ws->mon->y, ws->mon->w, ws->mon->h, c->bw);
	if (panels) {
		FOR_EACH(p, panels)
			updstruts(p, 1);
	} else {
		updnetworkspaces();
	}
	FOR_EACH(d, desks)
		if (d->x != d->mon->wx || d->y != d->mon->wy || d->w != d->mon->ww || d->h != d->mon->wh) {
			d->x = d->mon->wx, d->y = d->mon->wy, d->w = d->mon->ww, d->h = d->mon->wh;
			MOVERESIZE(d->win, d->x, d->y, d->w, d->h, 0);
		}
	setnetwsnames();
	needsrefresh = 1;
}

int usage(int e, char flag)
{
	switch (flag) {
	case 'h': fprintf(stderr, "usage: yaxwm [-hv] [-s SOCKET_FD]\n"); break;
	case 'v': fprintf(stderr, "yaxwm "VERSION"\n"); break;
	}
	return e;
}

xcb_get_window_attributes_reply_t *winattr(xcb_window_t win)
{
	xcb_get_window_attributes_reply_t *wa = NULL;
	xcb_generic_error_t *e;
	xcb_get_window_attributes_cookie_t c;

	if (win && win != root) {
		c = xcb_get_window_attributes(con, win);
		DBG("winattr: getting window attributes - 0x%08x", win)
		if (!(wa = xcb_get_window_attributes_reply(con, c, &e)))
			iferr(0, "unable to get window attributes reply", e);
	}
	return wa;
}

xcb_get_geometry_reply_t *wingeom(xcb_window_t win)
{
	xcb_get_geometry_reply_t *g = NULL;
	xcb_generic_error_t *e;
	xcb_get_geometry_cookie_t gc;

	if (win && win != root) {
		gc = xcb_get_geometry(con, win);
		DBG("wingeom: getting window geometry - 0x%08x", win)
		if (!(g = xcb_get_geometry_reply(con, gc, &e)))
			iferr(0, "unable to get window geometry reply", e);
	}
	return g;
}

int winprop(xcb_window_t win, xcb_atom_t prop, xcb_atom_t *ret)
{
	int i = 0;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t c;
	xcb_get_property_reply_t *r = NULL;

	c = xcb_get_property(con, 0, win, prop, XCB_ATOM_ANY, 0, 1);
	DBG("winprop: getting window property atom: %d - 0x%08x", prop, win)
	if ((r = xcb_get_property_reply(con, c, &e)) && xcb_get_property_value_length(r)) {
		i = 1;
		*ret = *(xcb_atom_t *)xcb_get_property_value(r);
		DBG("winprop: property reply value: %d", *ret)
	} else {
		iferr(0, "unable to get window property reply", e);
	}
	free(r);
	return i;
}

Client *wintoclient(xcb_window_t win)
{
	Workspace *ws;
	Client *c = NULL;

	if (win != XCB_WINDOW_NONE && win != root)
		FOR_CLIENTS(c, ws)
			if (c->win == win)
				return c;
	return c;
}

Panel *wintopanel(xcb_window_t win)
{
	Panel *p = NULL;

	if (win != XCB_WINDOW_NONE && win != root)
		FOR_EACH(p, panels)
			if (p->win == win)
				return p;
	return p;
}

Desk *wintodesk(xcb_window_t win)
{
	Desk *d = NULL;

	if (win != XCB_WINDOW_NONE && win != root)
		FOR_EACH(d, desks)
			if (d->win == win)
				return d;
	return d;
}

xcb_window_t wintrans(xcb_window_t win)
{
	xcb_get_property_cookie_t pc;
	xcb_generic_error_t *e = NULL;
	xcb_window_t t = XCB_WINDOW_NONE;

	pc = xcb_icccm_get_wm_transient_for(con, win);
	DBG("wintrans: getting wm transient for hint - 0x%08x", win)
	if (!xcb_icccm_get_wm_transient_for_reply(con, pc, &t, &e))
		iferr(0, "unable to get wm transient for hint", e);
	return t;
}
