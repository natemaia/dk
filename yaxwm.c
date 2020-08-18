/* yet another X window manager
* see license file for copyright and license details
* vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
*/

#define _XOPEN_SOURCE 700

#include <sys/un.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <err.h>
#include <poll.h>
#include <errno.h>
#include <stdio.h>
#include <regex.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <limits.h>
#include <locale.h>
#include <string.h>

#include <xcb/randr.h>
#include <xcb/xproto.h>
#include <xcb/xcb_util.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_cursor.h>
#include <xcb/xcb_keysyms.h>

#ifdef DEBUG
#define DBG(fmt, ...) warnx("%d: " fmt, __LINE__, ##__VA_ARGS__);
#else
#define DBG(fmt, ...)
#endif

#ifndef VERSION
#define VERSION "0.82"
#endif

#define W(c) ((c)->w + (2 * (c)->bw))
#define H(c) ((c)->h + (2 * (c)->bw))
#define LEN(x) (sizeof(x) / sizeof(x[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(x, min, max) (MIN(MAX((x), (min)), (max)))
#define CLNMOD(mod) (mod & ~(lockmask | XCB_MOD_MASK_LOCK))
#define FLOATING(c) ((c)->state & STATE_FLOATING || !(c)->ws->layout->fn.layout)
#define FULLSCREEN(c)                                                          \
	((c)->state & STATE_FULLSCREEN && !((c)->state & STATE_FAKEFULL))

#define FOR_EACH(v, list) for ((v) = (list); (v); (v) = (v)->next)
#define FOR_STACK(v, list) for ((v) = (list); (v); (v) = (v)->snext)
#define FOR_CLIENTS(c, ws)                                                     \
	FOR_EACH((ws), workspaces) FOR_EACH((c), (ws)->clients)

#define FIND_TAIL(v, list) for ((v) = (list); (v) && (v)->next; (v) = (v)->next)
#define FIND_PREV(v, cur, list)                                                \
	for ((v) = (list); (v) && (v)->next && (v)->next != (cur); (v) = (v)->next)

#define DETACH(v, listptr)                                                     \
	while (*(listptr) && *(listptr) != (v))                                    \
		(listptr) = &(*(listptr))->next;                                       \
	*(listptr) = (v)->next

#define MOVE(win, x, y)                                                        \
	xcb_configure_window(con, (win), XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y,\
			(unsigned int[]){(x), (y)})
#define MOVERESIZE(win, x, y, w, h, bw)                                        \
	xcb_configure_window(                                                      \
			con, win,                                                          \
			XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y                          \
			| XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT               \
			| XCB_CONFIG_WINDOW_BORDER_WIDTH,                                  \
			(unsigned int[]){(x), (y), (w), (h), (bw)});
#define PROP_APPEND(win, atom, type, membsize, nmemb, value)                   \
	xcb_change_property(con, XCB_PROP_MODE_APPEND, (win), (atom), (type),      \
			(membsize), (nmemb), (value))
#define PROP_REPLACE(win, atom, type, membsize, nmemb, value)                  \
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, (win), (atom), (type),     \
			(membsize), (nmemb), (value))


enum States {
	STATE_NONE       = 0,
	STATE_FAKEFULL   = 1 << 0,
	STATE_FIXED      = 1 << 1,
	STATE_FLOATING   = 1 << 2,
	STATE_FULLSCREEN = 1 << 3,
	STATE_NOBORDER   = 1 << 4,
	STATE_NOINPUT    = 1 << 5,
	STATE_STICKY     = 1 << 6,
	STATE_URGENT     = 1 << 7,
	STATE_NEEDSMAP   = 1 << 8,
};

enum Cursors {
	CURS_MOVE   = 0,
	CURS_NORMAL = 1,
	CURS_RESIZE = 2,
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
	DIR_NEXT    = 0,
	DIR_PREV    = 1,
	DIR_LAST    = 2,
	DIR_NEXT_NE = 3, /* non-empty */
	DIR_PREV_NE = 4, /* non-empty */
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
	GLB_SIZEHINT     = 6,
	GLB_SMART_BORDER = 7,
	GLB_SMART_GAP    = 8,
	GLB_TILETOHEAD   = 9,
};


static char *opts[] = {
	[DIR_NEXT] = "next",      [DIR_PREV] = "prev",      [DIR_LAST] = "last",
	[DIR_NEXT_NE] = "nextne", [DIR_PREV_NE] = "prevne", NULL
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


typedef struct Set Set;
typedef struct Rule Rule;
typedef struct Desk Desk;
typedef struct Panel Panel;
typedef struct Client Client;
typedef struct Monitor Monitor;
typedef struct Workspace Workspace;


struct Set {
	const char *name;
	const union {
		int  (*layout)(Workspace *);
		void (*keyword)(char **);
		void (*command)(int);
		void (*callback)(Client *, int);
	} fn;
};

struct Desk {
	int x, y, w, h;
	unsigned int state;
	xcb_window_t win;
	Desk *next;
	Monitor *mon;
};

struct Rule {
	int x, y, w, h, bw;
	int xgrav, ygrav;
	int ws, focus;
	unsigned int state;
	char *title, *class, *inst, *mon;
	const Set *cb;
	regex_t titlereg, classreg, instreg;
	Rule *next;
};

struct Panel {
	int x, y, w, h;
	int strut_l, strut_r, strut_t, strut_b;
	unsigned int state;
	xcb_window_t win;
	Panel *next;
	Monitor *mon;
};

struct Client {
	char title[NAME_MAX], class[64], inst[64];
	int x, y, w, h, bw, hoff, depth;
	int old_x, old_y, old_w, old_h, old_bw;
	int max_w, max_h, min_w, min_h;
	int base_w, base_h, inc_w, inc_h;
	float min_aspect, max_aspect;
	unsigned int state, old_state;
	xcb_window_t win;
	Client *trans, *next, *snext;
	Workspace *ws;
	const Set *cb;
};

struct Monitor {
	char name[64];
	int num, connected;
	int x, y, w, h;
	int wx, wy, ww, wh;
	xcb_randr_output_t id;
	Monitor *next;
	Workspace *ws;
};

struct Workspace {
	int nmaster, nstack, gappx;
	int padr, padl, padt, padb;
	float msplit, ssplit;
	const Set *layout;
	int num;
	char name[64];
	Monitor *mon;
	Workspace *next;
	Client *sel, *stack, *clients;
};


static void clienthints(Client *);
static int clientname(Client *);
static void clientrule(Client *, Rule *);
static void clienttype(Client *);
static void cmdborder(char **);
static void cmdcycle(char **);
static void cmdfakefull(char **);
static void cmdfloat(char **);
static void cmdfocus(char **);
static void cmdfollow(int);
static void cmdfull(char **);
static void cmdgappx(char **);
static void cmdkill(char **);
static void cmdlayout(char **);
static void cmdmon(char **);
static void cmdmouse(char **);
static void cmdmsplit(char **);
static void cmdnmaster(char **);
static void cmdnstack(char **);
static void cmdpad(char **);
static void cmdresize(char **);
static void cmdrule(char **);
static void cmdsend(int);
static void cmdset(char **);
static void cmdssplit(char **);
static void cmdstick(char **);
static void cmdswap(char **);
static void cmdview(int);
static void cmdwin(char **);
static void cmdwm(char **);
static void cmdws(char **);
static void cmdwsdef(char **);
static void detach(Client *, int);
static void drawborder(Client *, int);
static int dwindle(Workspace *);
static void *ecalloc(size_t, size_t);
static void eventhandle(xcb_generic_event_t *);
static void eventignore(uint8_t);
static void eventmouse(xcb_generic_event_t *);
static void execcfg(void);
static void fib(Workspace *, int);
static void focus(Client *);
static void freerule(Rule *r);
static void freewm(void);
static void freews(Workspace *);
static void grabbuttons(Client *, int);
static void gravitate(Client *, int, int, int);
static int grid(Workspace *);
static int iferr(int, char *, xcb_generic_error_t *);
static void initclient(xcb_window_t, xcb_get_geometry_reply_t *);
static void initdesk(xcb_window_t, xcb_get_geometry_reply_t *);
static void initmon(int, char *, xcb_randr_output_t, int, int, int, int);
static void initpanel(xcb_window_t, xcb_get_geometry_reply_t *);
static Rule *initrule(Rule *);
static void initscan(void);
static void initsock(int);
static void initwm(void);
static Workspace *initws(int);
static char *itoa(int n, char *);
static Monitor *itomon(int);
static Workspace *itows(int);
static void manage(xcb_window_t, xcb_get_geometry_reply_t *, xcb_get_window_attributes_reply_t *);
static int mono(Workspace *);
static void movefocus(int);
static void movestack(int);
static Monitor *nextmon(Monitor *m);
static Client *nexttiled(Client *);
static void offsetfloat(Client *, int *, int *, int *, int *);
static char **parsebool(char **, int *);
static int parseclient(char **, Client **);
static void parsecmd(char *);
static char **parsecolour(char **, unsigned int *);
static char **parsefloat(char **, float *, int *);
static char **parsegeom(char **, char, int *, int *, int *);
static char **parseint(char **, int *, int *, int);
static char **parseintclamp(char **, int *, int *, int, int);
static int parseopt(char **, char **);
static char *parsetoken(char **);
static void pushstatus(void);
static int querypointer(int *, int *);
static void refresh(void);
static void relocate(Workspace *, Monitor *);
static void resize(Client *, int, int, int, int, int);
static void resizehint(Client *, int, int, int, int, int, int, int);
static void restack(Workspace *);
static int rulecmp(Rule *, char *, char *, char *);
static void sendconfigure(Client *);
static int sendwmproto(Client *, int);
static void setfullscreen(Client *, int);
static void setinputfocus(Client *);
static void setnetwsnames(void);
static void setstackmode(xcb_window_t, unsigned int);
static void seturgent(Client *, int);
static void setwmwinstate(xcb_window_t, long);
static void setworkspace(Client *, int);
static void showhide(Client *);
static void sighandle(int);
static void sizehints(Client *, int);
static int spiral(Workspace *);
static size_t strlcat(char *, const char *, size_t);
static size_t strlcpy(char *, const char *, size_t);
static int tile(Workspace *);
static void unfocus(Client *, int);
static void unmanage(xcb_window_t, int);
static void updnetworkspaces(void);
static int updrandr(void);
static void updstruts(Panel *, int);
static void updworkspaces(int);
static void usage(int, char);
static xcb_get_window_attributes_reply_t *winattr(xcb_window_t);
static xcb_get_geometry_reply_t *wingeom(xcb_window_t);
static int winprop(xcb_window_t, xcb_atom_t, xcb_atom_t *);
static Client *wintoclient(xcb_window_t);
static Desk *wintodesk(xcb_window_t);
static Panel *wintopanel(xcb_window_t);
static xcb_window_t wintrans(xcb_window_t);
static int writecmd(int, char *[]);


/* config header needs the functions to be defined */
#include "yaxwm.h"


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
static xcb_cursor_t cursor[LEN(cursors)];
static xcb_atom_t wmatom[LEN(wmatoms)], netatom[LEN(netatoms)];


int main(int argc, char *argv[])
{
	ssize_t n;
	Monitor *p;
	Client *c = NULL;
	fd_set read_fds;
	xcb_window_t sel;
	struct timeval tv;
	xcb_generic_event_t *ev;
	char *end, buf[PIPE_BUF];
	int i, x, y, confd, nfds, cmdfd;

	argv0 = argv[0];
	randrbase = -1;
	running = needsrefresh = 1;
	sockfd = restart = cmdusemon = 0;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "-h")) {
			usage(0, argv[i][1]);
		} else if (!strcmp(argv[i], "-c")) {
			i++;
			writecmd(argc - i, &argv[i]);
		} else if (!strcmp(argv[i], "-s")) {
			if (!(sockfd = strtol(argv[++i], &end, 0)) || *end != '\0') {
				warnx("invalid socket file descriptor: %s", argv[i]);
				sockfd = 0;
			}
		} else {
			usage(1, 'h');
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
	iferr(1, "is another window manager running?",
			xcb_request_check(con,
				xcb_change_window_attributes_checked(con, root, XCB_CW_EVENT_MASK,
					(unsigned int[]){XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT})));

	DBG("main: sizeof(Set): %lu bytes", sizeof(Set))
	DBG("main: sizeof(Rule): %lu bytes", sizeof(Rule))
	DBG("main: sizeof(Desk): %lu bytes", sizeof(Desk))
	DBG("main: sizeof(Panel): %lu bytes", sizeof(Panel))
	DBG("main: sizeof(Client): %lu bytes", sizeof(Client))
	DBG("main: sizeof(Monitor): %lu bytes", sizeof(Monitor))
	DBG("main: sizeof(Workspace): %lu bytes", sizeof(Workspace))

	initwm();
	initsock(0);
	initscan();
	execcfg();

	if (winprop(root, netatom[NET_ACTIVE], &sel) && (c = wintoclient(sel))) {
		focus(c);
		xcb_warp_pointer(con, root, root, 0, 0, 0, 0, c->x + (c->w / 2), c->y + (c->h / 2));
	} else if (monitors->next && querypointer(&x, &y) && (p = primary)) {
		xcb_warp_pointer(con, root, root, 0, 0, 0, 0, p->x + (p->w / 2), p->y + (p->h / 2));
	}

	confd = xcb_get_file_descriptor(con);
	nfds = MAX(confd, sockfd) + 1;
	while (running) {
		if (xcb_connection_has_error(con))
			break;
		tv.tv_sec = 2; /* 2 sec timeout on select() */
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

	if (f == 0.0 || !setws->layout->fn.layout || (!relative && !(f -= *setting)))
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
	int opt, r;
	unsigned int i;
	void (*fn)(int);
	Monitor *m = NULL, *cm;
	Workspace *ws = NULL, *cws, *save;

	fn = cmdview;
	cws = selws;
	cmdclient = selws->sel;
	cm = cws->mon;

	if (*argv) {
		for (i = 0; i < LEN(wsmoncmds); i++)
			if (!strcmp(wsmoncmds[i].name, *argv)) {
				fn = wsmoncmds[i].fn.command;
				argv++;
				break;
			}
		if (fn != cmdview && (r = parseclient(argv, &cmdclient))) {
			if (r == -1)
				return;
			cws = cmdclient->ws;
			cm = cws->mon;
			argv++;
		} else {
			cmdclient = selws->sel;
		}
	}

	if (!*argv) {
		fprintf(cmdresp, "!%s %s\n", cmdusemon ? "mon" : "ws", enoargs);
		return;
	}

	if ((opt = parseopt(argv, opts)) >= 0) { /* word option (next, prev, nextne, etc.) */
		if (opt == DIR_LAST) {
			if (cmdusemon)
				ws = lastmon && lastmon->connected ? lastmon->ws : cws;
			else
				ws = lastws ? lastws : cws;
		} else if (opt == DIR_NEXT && cmdusemon) {
			if (!(m = nextmon(cm->next)))
				m = nextmon(monitors);
			ws = m->ws;
		} else if (opt == DIR_NEXT) {
			ws = cws->next ? cws->next : workspaces;
		} else if (cmdusemon && opt == DIR_PREV) {
			for (m = nextmon(monitors); m && nextmon(m->next)
					&& nextmon(m->next) != cm; m = nextmon(m->next))
				;
			ws = m ? m->ws : selws;
		} else if (opt == DIR_PREV) {
			FIND_PREV(ws, cws, workspaces);
		} else {
			r = 0;
			save = cws;
			while (!ws && r < globalcfg[GLB_NUMWS]) {
				if (opt == DIR_NEXT_NE) {
					if (cmdusemon) {
						if (!(m = nextmon(cm)))
							m = nextmon(monitors);
						ws = m->ws;
					} else
						ws = cws->next ? cws->next : workspaces;
				} else if (cmdusemon) {
					for (m = nextmon(monitors); m && nextmon(m->next)
							&& nextmon(m->next) != cm; m = nextmon(m->next))
						;
					ws = m ? m->ws : selws;
				} else {
					FIND_PREV(ws, cws, workspaces);
				}
				cws = ws;
				cm = ws->mon;
				if (!ws->clients && ws != save)
					ws = NULL;
				r++;
			}
		}
	} else { /* index (1 - numws) */
		parseintclamp(argv, &opt, NULL, 1, globalcfg[GLB_NUMWS]);
		if (!cmdusemon)
			ws = itows(opt - 1);
		else
			ws = (m = itomon(opt - 1)) && m->connected ? m->ws : cws;
	}

	if (ws)
		fn(ws->num);
	else
		fprintf(cmdresp, "!unable to locate %s\n", cmdusemon ? "monitor" : "workspace");
	return;
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
			p->mon->wx, p->mon->wy, p->mon->ww, p->mon->wh)
}

int applysizehints(Client *c, int *x, int *y, int *w, int *h, int bw, int usermotion, int mouse)
{
	int baseismin;
	Monitor *m = c->ws->mon;

	*w = MAX(*w, MIN(globalcfg[GLB_MIN_WH], c->min_w));
	*h = MAX(*h, MIN(globalcfg[GLB_MIN_WH], c->min_h));
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
	if (FLOATING(c) || globalcfg[GLB_SIZEHINT]) {
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
	DBG("applysizehints: 0x%08x - %d,%d @ %dx%d -> %d,%d @ %dx%d - usermotion: %d, mouse: %d",
			c->win, c->x, c->y, c->w, c->h, *x, *y, *w, *h, usermotion, mouse)
	return *x != c->x || *y != c->y || *w != c->w || *h != c->h || bw != c->bw;
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

	if (!ws || !nextmon(monitors))
		return;
	DBG("changews: %d:%s -> %d:%s - allowswap: %d - warp: %d", selws->num,
			selws->mon->name, ws->num, ws->mon->name, allowswap, diffmon)
	lastws = selws;
	lastmon = selmon;
	m = selws->mon;
	if (selws->sel)
		unfocus(selws->sel, 1);
	if (allowswap && m != ws->mon) {
		oldmon = ws->mon;
		selws->mon = ws->mon;
		if (ws->mon->ws == ws)
			ws->mon->ws = selws;
		ws->mon = m;
		m->ws = ws;
		updnetworkspaces();
		relocate(ws, oldmon);
		relocate(lastws, selmon);
	}
	selws = ws;
	selmon = ws->mon;
	selws->mon->ws = ws;
	if (!allowswap && diffmon) {
		xcb_warp_pointer(con, root, root, 0, 0, 0, 0,
				selws->sel ? ws->sel->x + (ws->sel->w / 2) : ws->mon->x + (ws->mon->w / 2),
				selws->sel ? ws->sel->y + (ws->sel->h / 2) : ws->mon->y + (ws->mon->h / 2));
	}
	PROP_REPLACE(root, netatom[NET_DESK_CUR], XCB_ATOM_CARDINAL, 32, 1, &ws->num);
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
	DBG("clientname: window title property reply: %s", r.name)
	strlcpy(c->title, r.name, sizeof(c->title));
	xcb_icccm_get_text_property_reply_wipe(&r);
	return 1;
}

void clientrule(Client *c, Rule *wr)
{
	Monitor *m;
	Rule *r = wr;
	int num, ws, focus = 0;
	xcb_atom_t cur = selws->num;

	DBG("clientrule: 0x%08x", c->win)
	if (c->trans)
		cur = c->trans->ws->num;
	else if (!winprop(c->win, netatom[NET_WM_DESK], &cur) || cur > 99)
		cur = selws->num;
	ws = cur;

	if (r && !rulecmp(r, c->title, c->class, c->inst)) {
		r = NULL;
	} else if (!r) {
		for (r = rules; r; r = r->next)
			if (rulecmp(r, c->title, c->class, c->inst))
				break;
	}
	if (r) {
		DBG("clientrule: matched: %s, %s, %s", r->class, r->inst, r->title)
		c->cb = r->cb;
		focus = r->focus;
		c->state |= r->state;
		c->x = r->x != -1 ? r->x : c->x;
		c->y = r->y != -1 ? r->y : c->y;
		c->w = r->w != -1 ? r->w : c->w;
		c->h = r->h != -1 ? r->h : c->h;
		c->bw = r->bw != -1 && !(c->state & STATE_NOBORDER) ? r->bw : c->bw;
		if (!c->trans) {
			if ((cmdusemon = r->mon != NULL)) {
				if ((num = strtol(r->mon, NULL, 0)) > 0 && (m = itomon(num))) {
					ws = m->ws->num;
				} else for (m = monitors; m; m = m->next)
					if (!strcmp(r->mon, m->name)) {
						ws = m->ws->num;
						break;
					}
			} else if (r->ws > 0 && r->ws <= globalcfg[GLB_NUMWS])
				ws = r->ws - 1;
		}
	}

	if (ws + 1 > globalcfg[GLB_NUMWS] && ws <= 99)
		updworkspaces(ws + 1);
	setworkspace(c, MIN(ws, globalcfg[GLB_NUMWS]));
	if (focus && c->ws != selws)
		cmdview(c->ws->num);
	if (r) /* handle gravity last as it may be on another workspace */
		gravitate(c, r->xgrav, r->ygrav, 1);
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
	int incol = 0, start = 0;
	int i, old, bw, ow, rel, outer;
	unsigned int focus, unfocus, urgent;
	unsigned int ofocus, ounfocus, ourgent;

	bw = border[BORD_WIDTH];
	ow = border[BORD_O_WIDTH];
	focus = border[BORD_FOCUS];
	urgent = border[BORD_URGENT];
	unfocus = border[BORD_UNFOCUS];
	ofocus = border[BORD_O_FOCUS];
	ourgent = border[BORD_O_URGENT];
	ounfocus = border[BORD_O_UNFOCUS];
	while (*argv) {
		if ((outer = !strcmp("outer", *argv) || !strcmp("outer_width", *argv))
				|| !strcmp(*argv, "width"))
		{
			incol = 0;
			argv++;
			if (!*argv) {
				fprintf(cmdresp, "!border %s %s\n", *(argv - 1), enoargs);
			} else {
				argv = parseint(argv, &i, &rel, 1);
				if (outer)
					adjustisetting(i, rel, &ow, selws->gappx + bw, 1);
				else
					adjustisetting(i, rel, &bw, selws->gappx, 1);
			}
		} else if (incol || (start = !strcmp(*argv, "colour") || !strcmp(*argv, "color"))) {
			if (!incol) {
				incol = 1;
				argv++;
			}
			if (!strcmp(*argv, "focus")) {
				argv = parsecolour(argv + 1, &focus);
			} else if (!strcmp(*argv, "urgent")) {
				argv = parsecolour(argv + 1, &urgent);
			} else if (!strcmp(*argv, "unfocus")) {
				argv = parsecolour(argv + 1, &unfocus);
			} else if (!strcmp(*argv, "outer_focus")) {
				argv = parsecolour(argv + 1, &ofocus);
			} else if (!strcmp(*argv, "outer_urgent")) {
				argv = parsecolour(argv + 1, &ourgent);
			} else if (!strcmp(*argv, "outer_unfocus")) {
				argv = parsecolour(argv + 1, &ounfocus);
			} else if (start) {
				fprintf(cmdresp, "!%s border colour: %s\n", ebadarg, *argv);
				break;
			} else {
				incol = 0;
				start = 0;
				continue; /* maybe more args after so don't increment argv */
			}
			start = 0;
		} else {
			fprintf(cmdresp, "!%s border: %s\n", ebadarg, *argv);
			break;
		}
		if (*argv)
			argv++;
	}

	old = border[BORD_WIDTH];
	if (bw - ow < 1) {
		if ((unsigned int)ow != border[BORD_O_WIDTH])
			fprintf(cmdresp, "!border outer exceeds limit: %d - maximum: %d\n", ow, bw - 1);
	} else {
		border[BORD_O_WIDTH] = ow;
	}
	border[BORD_WIDTH] = bw;
	border[BORD_FOCUS] = focus;
	border[BORD_UNFOCUS] = unfocus;
	border[BORD_URGENT] = urgent;
	border[BORD_O_FOCUS] = ofocus;
	border[BORD_O_UNFOCUS] = ounfocus;
	border[BORD_O_URGENT] = ourgent;
	FOR_CLIENTS(c, ws) {
		if (!(c->state & STATE_NOBORDER)) {
			if (c->bw == old)
				c->bw = bw;
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

	if (!(c = cmdclient) || !c->ws->layout->fn.layout)
		return;
	if (FULLSCREEN(c) || c->state & (STATE_STICKY | STATE_FIXED))
		return;
	if ((c->state ^= STATE_FLOATING) & STATE_FLOATING) {
		c->x = c->old_x;
		c->y = c->old_y;
		c->w = c->old_w;
		c->h = c->old_h;
		if (c->x + c->y == c->ws->mon->wx + c->ws->mon->wy)
			offsetfloat(c, &c->x, &c->y, &c->w, &c->h);
		resizehint(c, c->x, c->y, c->w, c->h, c->bw, 0, 1);
	} else {
		c->old_x = c->x;
		c->old_y = c->y;
		c->old_w = c->w;
		c->old_h = c->h;
	}
	needsrefresh = 1;
	(void)(argv);
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
	if ((opt = parseopt(argv, opts)) < 0) {
		parseint(argv, &i, NULL, 0);
		if (!i)
			fprintf(cmdresp, "!%s focus: %s\n", ebadarg, *argv);
		return;
	}
	if (opt == DIR_LAST)
		focus(cmdclient->snext);
	else
		movefocus(opt == -1 ? i : opt == DIR_NEXT ? 1 : -1);
}

void cmdfollow(int num)
{
	if (!cmdclient || num == cmdclient->ws->num || !itows(num))
		return;
	unfocus(cmdclient, 1);
	setworkspace(cmdclient, num);
	cmdview(num);
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
	int i = INT_MAX, ng, rel;

	if (!strcmp(*argv, "width"))
		argv++;
	ng = setws->gappx;

	if (!*argv) {
		fprintf(cmdresp, "!gap %s\n", enoargs);
	} else {
		parseint(argv, &i, &rel, 1);
		adjustisetting(i, rel, &ng, border[BORD_WIDTH], 1);
	}

	if (ng != setws->gappx)
		setws->gappx = ng;
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
	if (monitors && nextmon(monitors))
		adjustwsormon(argv);
}

void cmdmouse(char **argv)
{
	int arg;
	xcb_button_t *btn;

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
			btn = arg ? &mousemove : &mouseresize;
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
	int i = INT_MAX, rel = 1;

	parseint(argv, &i, &rel, 1);
	adjustisetting(i, rel, &setws->nmaster, 0, 0);
}

void cmdnstack(char **argv)
{
	int i = INT_MAX, rel = 1;

	parseint(argv, &i, &rel, 1);
	adjustisetting(i, rel, &setws->nstack, 0, 0);
}

void cmdpad(char **argv)
{
	int i, rel;

#define ASSIGN(v, o)                                                           \
	argv = parseintclamp(argv + 1, &i, &rel, v * -1, o);                       \
	if (i != INT_MAX)                                                          \
		v = CLAMP(rel ? v + i : i, 0, o);

	while (*argv) {
		i = INT_MAX;
		if (!strcmp("l", *argv) || !strcmp("left", *argv)) {
			ASSIGN(setws->padl, setws->mon->w / 3)
		} else if (!strcmp("r", *argv) || !strcmp("right", *argv)) {
			ASSIGN(setws->padr, setws->mon->w / 3)
		} else if (!strcmp("t", *argv) || !strcmp("top", *argv)) {
			ASSIGN(setws->padt, setws->mon->h / 3)
		} else if (!strcmp("b", *argv) || !strcmp("bottom", *argv)) {
			ASSIGN(setws->padb, setws->mon->h / 3)
		} else {
			fprintf(cmdresp, "!%s pad: %s", ebadarg, *argv);
			break;
		}
		if (*argv)
			argv++;
	}
#undef ASSIGN
}

void cmdresize(char **argv)
{
	Client *c, *t;
	int i, ohoff;
	float f, *sf;
	int xgrav = GRAV_NONE, ygrav = GRAV_NONE;
	int x = scr_w, y = scr_h, w = 0, h = 0, bw = -1;
	int relx = 0, rely = 0, relw = 0, relh = 0, relbw = 0;

	if (!(c = cmdclient) || FULLSCREEN(c))
		return;
	while (*argv) {
		if (!strcmp("x", *argv))
			argv = parsegeom(argv + 1, 'x', &x, &relx, &xgrav);
		else if (!strcmp("y", *argv))
			argv = parsegeom(argv + 1, 'y', &y, &rely, &ygrav);
		else if (!strcmp("w", *argv) || !strcmp("width", *argv))
			argv = parseint(argv + 1, &w, &relw, 0);
		else if (!strcmp("h", *argv) || !strcmp("height", *argv))
			argv = parseint(argv + 1, &h, &relh, 0);
		else if (!strcmp(*argv, "bw") || !strcmp(*argv, "border_width"))
			argv = parseint(argv + 1, &bw, &relbw, 1);
		else {
			fprintf(cmdresp, "!%s resize: %s\n", ebadarg, *argv);
			break;
		}
		if (*argv)
			argv++;
	}
	if (x == scr_w && y == scr_h && w == 0 && h == 0
			&& xgrav == GRAV_NONE && ygrav == GRAV_NONE && bw == -1)
		return;
	if (FLOATING(c)) {
		x = x == scr_w || xgrav != GRAV_NONE ? c->x : (relx ? c->x + x : x);
		y = y == scr_h || ygrav != GRAV_NONE ? c->y : (rely ? c->y + y : y);
		w = w == 0 ? c->w : (relw ? c->w + w : w);
		h = h == 0 ? c->h : (relh ? c->h + h : h);
		bw = bw == -1 ? c->bw : (relbw ? c->bw + bw : bw);
		resizehint(c, x, y, w, h, bw, 1, 0);
		gravitate(c, xgrav, ygrav, 1);
	} else if (c->ws->layout->fn.layout == tile) {
		if (bw != -1) {
			c->bw = relbw ? c->bw + bw : bw;
			if (y == scr_h && !w && !h)
				drawborder(c, c == selws->sel);
		}
		if (y != scr_h)
			movestack(y > 0 || ygrav == GRAV_BOTTOM ? 1 : -1);
		if (w) {
			sf = &c->ws->ssplit;
			for (i = 0, t = nexttiled(c->ws->clients); t; t = nexttiled(t->next), i++)
				if (t == c) {
					if (c->ws->nmaster && i < c->ws->nmaster + c->ws->nstack)
						sf = &c->ws->msplit;
					f = relw ? ((c->ws->mon->ww * *sf) + w) / c->ws->mon->ww : w / c->ws->mon->ww;
					if (f < 0.05 || f > 0.95) {
						fprintf(cmdresp, "!window width exceeded limit: %f - f: %f\n",
								c->ws->mon->ww * f, f);
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
			if (tile(c->ws) == -1) {
				fprintf(cmdresp, "!height adjustment for window exceeded limit: %d\n", c->hoff);
				c->hoff = ohoff;
				needsrefresh = 1;
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
	Client *c;
	Rule *wr, *nr;
	Workspace *ws;
	int j;
	unsigned int i, delete, apply = 0;
	Rule r = {
		.x = -1, .y = -1, .w = -1, .h = -1, .ws = -1, .bw = -1,
		.focus = 0, .state = 0, .xgrav = GRAV_NONE, .ygrav = GRAV_NONE,
		.cb = NULL, .mon = NULL, .inst = NULL, .class = NULL, .title = NULL,
	};

	if ((apply = !strcmp("apply", *argv))) {
		argv++;
		if (!strcmp("all", *argv)) {
			FOR_CLIENTS(c, ws) {
				clientrule(c, NULL);
				if (c->cb)
					c->cb->fn.callback(c, 0);
			}
			return;
		}
	} else if ((delete = !strcmp("remove", *argv) || !strcmp("delete", *argv))) {
		argv++;
		if (!strcmp("all", *argv)) {
			while (rules)
				freerule(rules);
			return;
		}
	}
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
			argv = parseintclamp(argv + 1, &r.ws, NULL, 1, 99);
			if (!r.ws && *argv) {
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
		} else if (!strcmp(*argv, "x"))
			argv = parsegeom(argv + 1, 'x', &r.x, NULL, &r.xgrav);
		else if (!strcmp(*argv, "y"))
			argv = parsegeom(argv + 1, 'y', &r.y, NULL, &r.ygrav);
		else if (!strcmp(*argv, "w") || !strcmp("width", *argv))
			argv = parseint(argv + 1, &r.w, NULL, 0);
		else if (!strcmp(*argv, "h") || !strcmp("height", *argv))
			argv = parseint(argv + 1, &r.h, NULL, 0);
		else if (!strcmp(*argv, "bw")) {
			argv = parseintclamp(argv + 1, &r.bw, NULL, 0, scr_h / 6);
			if (r.bw == 0 && border[BORD_WIDTH])
				r.state |= STATE_NOBORDER;
		} else if (!strcmp(*argv, "float")) {
			argv = parsebool(argv + 1, &j);
			r.state |= j ? STATE_FLOATING : STATE_NONE;
		} else if (!strcmp(*argv, "stick")) {
			argv = parsebool(argv + 1, &j);
			r.state |= j ? STATE_STICKY | STATE_FLOATING : STATE_NONE;
		} else if (!strcmp(*argv, "focus"))
			argv = parsebool(argv + 1, &r.focus);
		else {
			fprintf(cmdresp, "!%s rule: %s\n", ebadarg, *argv);
			break;
		}
		if (*argv)
			argv++;
	}

	if ((r.class || r.inst || r.title)
			&& (r.ws || r.mon || r.focus || r.cb || r.state & STATE_FLOATING
				|| r.x != -1 || r.y != -1 || r.w != -1 || r.h != -1 || r.bw != -1
				|| r.xgrav != GRAV_NONE || r.ygrav != GRAV_NONE))
	{
		FOR_EACH(wr, rules) {
			if ((r.class == NULL || (wr->class && !strcmp(r.class, wr->class)))
					&& (r.inst == NULL || (wr->inst && !strcmp(r.inst, wr->inst)))
					&& (r.title == NULL || (wr->title && !strcmp(r.title, wr->title))))
			{
				freerule(wr);
				break;
			}
		}
		if (!delete) {
			if ((nr = initrule(&r)) && apply) {
				FOR_CLIENTS(c, ws) {
					clientrule(c, nr);
					if (c->cb)
						c->cb->fn.callback(c, 0);
				}
			}
		}
	}
}

void cmdsend(int num)
{
	if (!cmdclient || num == cmdclient->ws->num || !itows(num))
		return;
	unfocus(cmdclient, 1);
	setworkspace(cmdclient, num);
	needsrefresh = 1;
}

void cmdset(char **argv)
{
	Workspace *ws;
	unsigned int j;
	int i, names = 0;

	setws = selws;
	if (!*argv) {
		fprintf(cmdresp, "!set %s\n", enoargs);
		return;
	}
	while (*argv) {
		i = -1;
		if (!strcmp("ws", *argv)) {
			if (!strcmp("default", *(argv + 1))) {
				cmdwsdef(argv + 2);
				break;
			}
			argv = parseintclamp(argv + 1, &i, NULL, 1, globalcfg[GLB_NUMWS]);
			if (!(ws = itows(i - 1))) {
				fprintf(cmdresp, "!invalid workspace index: %s\n", *argv);
				break;
			}
			setws = ws;
		} else if (!strcmp("numws", *argv)) {
			argv = parseintclamp(argv + 1, &i, NULL, 1, 99);
			if (i > globalcfg[GLB_NUMWS])
				updworkspaces(i);
		} else if (!strcmp("name", *argv)) {
			argv++;
			if (!*argv) {
				fprintf(cmdresp, "!set ws name %s\n", enoargs);
				goto finish;
			}
			strlcpy(setws->name, *argv, sizeof(setws->name));
			names = 1;
		} else if (!strcmp("tile_tohead", *argv)) {
			argv = parsebool(argv + 1, &globalcfg[GLB_TILETOHEAD]);
		} else if (!strcmp("smart_border", *argv)) {
			argv = parsebool(argv + 1, &globalcfg[GLB_SMART_BORDER]);
		} else if (!strcmp("smart_gap", *argv)) {
			argv = parsebool(argv + 1, &globalcfg[GLB_SMART_GAP]);
		} else if (!strcmp("focus_urgent", *argv)) {
			argv = parsebool(argv + 1, &globalcfg[GLB_FOCUS_URGENT]);
		} else if (!strcmp("tile_hints", *argv)) {
			argv = parsebool(argv + 1, &globalcfg[GLB_SIZEHINT]);
		} else if (!strcmp("focus_mouse", *argv)) {
			argv = parsebool(argv + 1, &globalcfg[GLB_FOCUS_MOUSE]);
		} else if (!strcmp("win_minxy", *argv)) {
			argv = parseintclamp(argv + 1, &globalcfg[GLB_MIN_XY], NULL, 10, 1000);
		} else if (!strcmp("win_minwh", *argv)) {
			argv = parseintclamp(argv + 1, &globalcfg[GLB_MIN_WH], NULL, 10, 1000);
		} else {
			for (j = 0; j < LEN(setcmds); j++)
				if (!strcmp(setcmds[j].name, *argv)) {
					setcmds[j].fn.keyword(argv + 1);
					goto finish;
				}
			fprintf(cmdresp, "!%s set: %s\n", ebadarg, *argv);
		}
		if (*argv)
			argv++;
	}

finish:
	needsrefresh = 1;
	if (names)
		setnetwsnames();
}

void cmdmsplit(char **argv)
{
	int rel = 1;
	float f = 0.0;

	parsefloat(argv, &f, &rel);
	adjustfsetting(f, rel, &setws->msplit);
}

void cmdssplit(char **argv)
{
	int rel = 1;
	float f = 0.0;

	parsefloat(argv, &f, &rel);
	adjustfsetting(f, rel, &setws->ssplit);
}

void cmdstick(char **argv)
{
	unsigned int all = 0xffffffff;

	if (!cmdclient || FULLSCREEN(cmdclient))
		return;
	if ((cmdclient->state ^= STATE_STICKY) & STATE_STICKY) {
		cmdclient->state &= ~STATE_STICKY;
		PROP_REPLACE(cmdclient->win, netatom[NET_WM_DESK], XCB_ATOM_CARDINAL, 32, 1,
				&cmdclient->ws->num);
	} else {
		cmdfloat(NULL);
		cmdclient->state |= STATE_STICKY | STATE_FLOATING;
		PROP_REPLACE(cmdclient->win, netatom[NET_WM_DESK], XCB_ATOM_CARDINAL, 32, 1, &all);
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
			if (!c || !(c = nexttiled(c->next)))
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
			old->next = cur->next;
			cur->next = old;
		}
	}
	needsrefresh = 1;
	(void)(argv);
}

void cmdwin(char **argv)
{
	int i;
	unsigned int ui;
	cmdclient = selws->sel;

	if ((i = parseclient(argv, &cmdclient))) {
		if (i == -1)
			return;
		argv++;
	}
	if (!*argv) {
		fprintf(cmdresp, "!win %s\n", enoargs);
		return;
	}
	for (ui = 0; ui < LEN(wincmds); ui++)
		if (!strcmp(wincmds[ui].name, *argv)) {
			wincmds[ui].fn.keyword(argv + 1);
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
	Workspace *ws;
	unsigned int i;
	int inpad = 0, start = 0, apply = 0;

	while (*argv) {
		if (!strcmp(*argv, "apply")) {
			apply = 1;
		} else if (!strcmp(*argv, "layout")) {
			argv++;
			inpad = 0;
			for (i = 0; i < LEN(layouts); i++)
				if (!strcmp(layouts[i].name, *argv)) {
					wsdef.layout = &layouts[i];
					break;
				}
		} else if (!strcmp(*argv, "master")) {
			inpad = 0;
			argv = parseintclamp(argv + 1, &wsdef.nmaster, NULL, 0, INT_MAX - 1);
		} else if (!strcmp(*argv, "stack")) {
			inpad = 0;
			argv = parseintclamp(argv + 1, &wsdef.nstack, NULL, 0, INT_MAX - 1);
		} else if (!strcmp(*argv, "msplit")) {
			inpad = 0;
			argv = parsefloat(argv + 1, &wsdef.msplit, NULL);
		} else if (!strcmp(*argv, "ssplit")) {
			inpad = 0;
			argv = parsefloat(argv + 1, &wsdef.ssplit, NULL);
		} else if (!strcmp(*argv, "gap")) {
			inpad = 0;
			argv = parseintclamp(argv + 1, &wsdef.gappx, NULL, 0, scr_h / 6);
		} else if (inpad || (start = !strcmp(*argv, "pad"))) {
			if (!inpad) {
				inpad = 1;
				argv++;
			}
			if (!strcmp(*argv, "l") || !strcmp(*argv, "left"))
				argv = parseintclamp(argv + 1, &wsdef.padl, NULL, 0, scr_h / 3);
			else if (!strcmp(*argv, "r") || !strcmp(*argv, "right"))
				argv = parseintclamp(argv + 1, &wsdef.padr, NULL, 0, scr_h / 3);
			else if (!strcmp(*argv, "t") || !strcmp(*argv, "top"))
				argv = parseintclamp(argv + 1, &wsdef.padt, NULL, 0, scr_h / 3);
			else if (!strcmp(*argv, "b") || !strcmp(*argv, "bottom"))
				argv = parseintclamp(argv + 1, &wsdef.padb, NULL, 0, scr_h / 3);
			else if (start) {
				fprintf(cmdresp, "!%s pad: %s\n", ebadarg, *argv);
				return;
			} else {
				inpad = 0;
				start = 0;
				continue; /* maybe more args after pad so don't increment argv */
			}
			start = 0;
		} else {
			fprintf(cmdresp, "!%s workspace default: %s\n", ebadarg, *argv);
			return;
		}
		if (*argv)
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
}

void cmdview(int num)
{
	Workspace *ws;

	DBG("cmdview: workspace %d", num)
	if (num != selws->num && (ws = itows(num))) {
		changews(ws, !cmdusemon, cmdusemon);
		needsrefresh = 1;
	}
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
	in = border[focused ? BORD_FOCUS
		: (c->state & STATE_URGENT ? BORD_URGENT : BORD_UNFOCUS)];
	out = border[focused ? BORD_O_FOCUS
		: (c->state & STATE_URGENT ? BORD_O_URGENT : BORD_O_UNFOCUS)];
	xcb_rectangle_t inner[] = {
		/* x            y             width         height */
		{ c->w,         0,            b - o,        c->h + b - o }, /* right */
		{ c->w + b + o, 0,            b - o,        c->h + b - o }, /* left */
		{ 0,            c->h,         c->w + b - o, b - o        }, /* bottom */
		{ 0,            c->h + b + o, c->w + b - o, b - o        }, /* top */
		{ c->w + b + o, c->h + b + o, b,            b            }  /* top left corner fill */
	};
	xcb_rectangle_t outer[] = {
		/* x            y             width         height */
		{ c->w + b - o, 0,            o,            c->h + b * 2 }, /* right */
		{ c->w + b,     0,            o,            c->h + b * 2 }, /* left */
		{ 0,            c->h + b - o, c->w + b * 2, o            }, /* bottom */
		{ 0,            c->h + b,     c->w + b * 2, o            }, /* top */
		{ 1,            1,            1,            1            }  /* top left corner fill */
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
		attach(c, 1);
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
	fib(ws, 1);
	return 1;
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

	switch (ev->response_type & 0x7f) {
	case XCB_BUTTON_PRESS:   /* FALLTHROUGH */
	case XCB_BUTTON_RELEASE: /* FALLTHROUGH */
	case XCB_MOTION_NOTIFY: {
		eventmouse(ev);
		return;
	}
	case XCB_PROPERTY_NOTIFY: {
		Panel *p;
		xcb_property_notify_event_t *e = (xcb_property_notify_event_t *)ev;
		static Client *lastc = NULL;
		static xcb_timestamp_t lastt = 0;

#ifdef DEBUG
		if (e->window != root) {
			for (unsigned int i = 0; i < LEN(netatom); i++)
				if (netatom[i] == e->atom) {
					DBG("eventhandle: PROPERTY_NOTIFY - atom: %s - 0x%08x", netatoms[i], e->window)
				}
			for (unsigned int i = 0; i < LEN(wmatom); i++)
				if (wmatom[i] == e->atom) {
					DBG("eventhandle: PROPERTY_NOTIFY - atom: %s - 0x%08x", wmatoms[i], e->window)
				}
		}
#endif
		if (e->state == XCB_PROPERTY_DELETE) {
			return;
		} else if ((c = wintoclient(e->window))) {
			if (lastc != c)
				lastc = c;
			else if (e->time - lastt < 1000)
				return;
			lastt = e->time;

			switch (e->atom) {
			case XCB_ATOM_WM_HINTS:
				clienthints(c); return;
			case XCB_ATOM_WM_NORMAL_HINTS:
				sizehints(c, 0); return;
			case XCB_ATOM_WM_TRANSIENT_FOR:
				if ((c->trans = wintoclient(wintrans(c->win))) && !FLOATING(c))
					c->state |= STATE_FLOATING, needsrefresh = 1;
				return;
			default:
				if ((e->atom == XCB_ATOM_WM_NAME || e->atom == netatom[NET_WM_NAME]) && clientname(c))
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
	case XCB_CONFIGURE_REQUEST: {
		xcb_configure_request_event_t *e = (xcb_configure_request_event_t *)ev;

		if ((c = wintoclient(e->window))) {
			DBG("eventhandle: CONFIGURE_REQUEST - managed %s window 0x%08x",
					FLOATING(c) ? "floating" : "tiled", e->window)
			if (e->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH)
				c->bw = e->border_width;
			else if (FLOATING(c)) {
				m = c->ws->mon;
				if (e->value_mask & XCB_CONFIG_WINDOW_X) {
					if (e->x == W(c) * -2)
						return;
					c->old_x = c->x;
					c->x = m->x + e->x - c->bw;
				}
				if (e->value_mask & XCB_CONFIG_WINDOW_Y) {
					c->old_y = c->y;
					c->y = m->y + e->y - c->bw;
				}
				if (e->value_mask & XCB_CONFIG_WINDOW_WIDTH) {
					c->old_w = c->w;
					c->w = e->width;
				}
				if (e->value_mask & XCB_CONFIG_WINDOW_HEIGHT) {
					c->old_h = c->h;
					c->h = e->height;
				}
				if (c->x + c->w > m->wx + m->ww)
					c->x = m->wx + ((m->ww - W(c)) / 2);
				if (c->y + c->h > m->wy + m->wh)
					c->y = m->wy + ((m->wh - H(c)) / 2);
				if (e->value_mask & (XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y)
						&& !(e->value_mask & (XCB_CONFIG_WINDOW_WIDTH
								| XCB_CONFIG_WINDOW_HEIGHT)))
				{
					sendconfigure(c);
				}
				if (c->ws == m->ws)
					MOVERESIZE(c->win, c->x, c->y, c->w, c->h, c->bw);
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
		if (c && c != selws->sel && globalcfg[GLB_FOCUS_MOUSE])
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
		DBG("eventhandle: FOCUS_IN - 0x%08x", e->event)
		if (selws->sel && e->event != selws->sel->win)
			setinputfocus(selws->sel);
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

		DBG("eventhandle: UNMAP_NOTIFY - 0x%08x", e->window)
		if (e->response_type & ~0x7f)
			setwmwinstate(e->window, XCB_ICCCM_WM_STATE_WITHDRAWN);
		else
			unmanage(e->window, 0);
		return;
	}
	case XCB_CLIENT_MESSAGE: {
		xcb_client_message_event_t *e = (xcb_client_message_event_t *)ev;
		unsigned int *d = e->data.data32;

		DBG("eventhandle: CLIENT_MESSAGE - 0x%08x", e->window)
		cmdusemon = 0;
		if (e->window == root && e->type == netatom[NET_DESK_CUR]) {
			unfocus(selws->sel, 1);
			cmdview(d[0]);
		} else if (e->type == netatom[NET_CLOSE]) {
			unmanage(e->window, 1);
		} else if ((c = wintoclient(e->window))) {
			if (e->type == netatom[NET_WM_DESK]) {
				if (!itows(d[0])) {
					warnx("invalid workspace index: %d", d[0]);
					return;
				}
				setworkspace(c, d[0]);
				needsrefresh = 1;
			} else if (e->type == netatom[NET_WM_STATE]
					&& (d[1] == netatom[NET_STATE_FULL] || d[2] == netatom[NET_STATE_FULL]))
			{
				setfullscreen(c, (d[0] == 1 || (d[0] == 2 && !(c->state & STATE_FULLSCREEN))));
			} else if (e->type == netatom[NET_ACTIVE] && c != selws->sel) {
				if (globalcfg[GLB_FOCUS_URGENT]) {
					if (c->ws != selws) {
						unfocus(selws->sel, 1);
						cmdview(c->ws->num);
					}
					focus(c);
				} else {
					seturgent(c, 1);
				}
			}
		}
		return;
	}
	case 0: { /* ERROR */
		break;
	}
	default: { /* RANDR */
		if (ev->response_type == randrbase + XCB_RANDR_SCREEN_CHANGE_NOTIFY)
			if (((xcb_randr_screen_change_notify_event_t *)ev)->root == root && updrandr())
				updworkspaces(globalcfg[GLB_NUMWS]);
	}
	}
}

void eventmouse(xcb_generic_event_t *ev)
{
	Client *c;
	Monitor *m;
	static Client *grab = NULL;
	static xcb_timestamp_t last = 0;
	static int grabbing = 0, grabmove = 0;
	static int mx, my, ox, oy, ow, oh, nw, nh, nx, ny;

	switch (ev->response_type & 0x7f) {
	case XCB_BUTTON_PRESS:
	{
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
		if (!grabbing && CLNMOD(e->state) == CLNMOD(mousemod)
				&& (e->detail == mousemove || e->detail == mouseresize))
		{
			DBG("eventhandle: BUTTON_PRESS - grabbing pointer - 0x%08x", e->event)
			grabmove = e->detail == mousemove;
			if (!(grab = selws->sel) || FULLSCREEN(c)
					|| ((grab->state & STATE_FIXED) && !grabmove))
				return;
			if (!querypointer(&mx, &my))
				return;
			pc = xcb_grab_pointer(con, 0, root, XCB_EVENT_MASK_BUTTON_RELEASE
					| XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_POINTER_MOTION,
					XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, root,
					cursor[grabmove ? CURS_MOVE : CURS_RESIZE], XCB_CURRENT_TIME);
			if ((ptr = xcb_grab_pointer_reply(con, pc, &err))
					&& ptr->status == XCB_GRAB_STATUS_SUCCESS)
			{
				last = 0;
				grabbing = 1;
				ox = nx = grab->x;
				oy = ny = grab->y;
				ow = nw = grab->w;
				oh = nh = grab->h;
			} else {
				iferr(0, "unable to grab pointer", err);
			}
			free(ptr);
		}
		return;
	}
	case XCB_BUTTON_RELEASE:
	{
		if (grabbing) {
			DBG("eventhandle: BUTTON_RELEASE - ungrabbing pointer - 0x%08x", grab->win)
			iferr(1, "failed to ungrab pointer",
					xcb_request_check(con, xcb_ungrab_pointer_checked(con, XCB_CURRENT_TIME)));
			if (!grabmove)
				eventignore(XCB_ENTER_NOTIFY);
			grab = NULL;
			grabbing = 0;
		}
		break;
	}
	case XCB_MOTION_NOTIFY:
	{
		xcb_motion_notify_event_t *e = (xcb_motion_notify_event_t *)ev;

		if (grabbing) {
			if ((e->time - last) < (1000 / 120))
				return;
			last = e->time;
			DBG("eventhandle: MOTION_NOTIFY - pointer grabbed - 0x%08x - grabmove: %d",
					grab->win, grabmove)
			if (grabmove) {
				nx = ox + (e->root_x - mx);
				ny = oy + (e->root_y - my);
			} else {
				nw = ow + (e->root_x - mx);
				nh = oh + (e->root_y - my);
			}
			if ((nw != grab->w || nh != grab->h || nx != grab->x || ny != grab->y)) {
				if (!FLOATING(grab) || (grab->state & STATE_FULLSCREEN
							&& grab->state & STATE_FAKEFULL
							&& !(grab->old_state & STATE_FLOATING)))
				{
					grab->state |= STATE_FLOATING;
					grab->old_state |= STATE_FLOATING;
					if (grab->max_w)
						grab->w = MIN(grab->w, grab->max_w);
					if (grab->max_h)
						grab->h = MIN(grab->h, grab->max_h);
					grab->x = CLAMP(grab->x, grab->ws->mon->wx,
							grab->ws->mon->wx + grab->ws->mon->ww - W(grab));
					grab->y = CLAMP(grab->y, grab->ws->mon->wy,
							grab->ws->mon->wy + grab->ws->mon->wh - H(grab));
					resizehint(grab, grab->x, grab->y, grab->w, grab->h, grab->bw, 1, 1);
					if (grab->ws->layout->fn.layout)
						grab->ws->layout->fn.layout(grab->ws);
					restack(grab->ws);
				}
				if (grabmove && (m = coordtomon(e->root_x, e->root_y)) && m->ws != grab->ws) {
					setworkspace(grab, m->ws->num);
					changews(m->ws, 0, 0);
					focus(grab);
				}
				resizehint(grab, nx, ny, nw, nh, grab->bw, 1, 1);
			}
		} else if (e->event == root && (m = coordtomon(e->root_x, e->root_y)) && m->ws != selws) {
			DBG("eventhandle: MOTION_NOTIFY - updating active monitor - 0x%08x", e->event)
			changews(m->ws, 0, 0);
			focus(NULL);
		}
		return;
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
	char *cfg, *home;
	char path[PATH_MAX];

	if (!(cfg = getenv("YAXWMRC"))) {
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

void fib(Workspace *ws, int s)
{
	int b;
	Client *c;
	Monitor *m = ws->mon;
	unsigned int i, n, nx, ny, nw, nh;

	for (n = 0, c = nexttiled(ws->clients); c; c = nexttiled(c->next), n++)
		;
	if (!n)
		return;

	nx = m->wx + ws->padl;
	ny = m->wy + ws->padt;
	nw = m->ww - ws->padl - ws->padr;
	nh = m->wh - ws->padt - ws->padb;
	/* int g; */
	/* g = globalcfg[GLB_SMART_GAP] ? 0 : ws->gappx; */

	for (i = 0, c = nexttiled(ws->clients); c; c = nexttiled(c->next), i++) {
		b = globalcfg[GLB_SMART_BORDER] && n == 1 ? 0 : c->bw;
		if (i < n - 1) {
			if (i % 2)
				nh /= 2;
			else
				nw /= 2;
			if (!s && (i % 4) == 2)
				nx += nw;
			else if (!s && (i % 4) == 3)
				ny += nh;
		}
		switch (i % 4) {
		case 0: ny += s ? nh : nh * -1; break;
		case 1: nx += nw; break;
		case 2: ny += nh; break;
		case 3: nx += s ? nw : nw * -1; break;
		}
		if (i == 0) {
			if (n > 1)
				nw = (m->ww - ws->padl - ws->padr) * ws->msplit;
			ny = m->wy - ws->padt;
		} else if (i == 1) {
			nw = (m->ww - ws->padl - ws->padr) - nw;
		}
		resizehint(c, nx, ny, nw - (2 * b), nh - (2 * b), b, 0, 0);
	}
}

void focus(Client *c)
{
	if (!c || c->ws != c->ws->mon->ws)
		c = selws->stack;
	if (selws->sel && selws->sel != c)
		unfocus(selws->sel, 0);
	if (c) {
		if (c->state & STATE_URGENT)
			seturgent(c, 0);
		detachstack(c);
		attachstack(c);
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
		if (mon)
			mon->next = m->next;
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
	unsigned int i, j, mods[] = { 0, XCB_MOD_MASK_LOCK, 0, XCB_MOD_MASK_LOCK };

	lockmask = 0;
	if ((m = xcb_get_modifier_mapping_reply(con, xcb_get_modifier_mapping(con), &e))) {
		if ((t = xcb_key_symbols_get_keycode(keysyms, nlock))
				&& (kc = xcb_get_modifier_mapping_keycodes(m)))
		{
			for (i = 0; i < 8; i++)
				for (j = 0; j < m->keycodes_per_modifier; j++)
					if (kc[i * m->keycodes_per_modifier + j] == *t)
						lockmask = (1 << i);
		}
	} else {
		iferr(0, "unable to get modifier mapping for numlock", e);
	}
	free(t);
	free(m);

	mods[2] |= lockmask, mods[3] |= lockmask;
	xcb_ungrab_button(con, XCB_BUTTON_INDEX_ANY, c->win, XCB_BUTTON_MASK_ANY);
	if (!focused)
		xcb_grab_button(con, 0, c->win,
				XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE,
				XCB_GRAB_MODE_SYNC, XCB_GRAB_MODE_SYNC, XCB_NONE, XCB_NONE,
				XCB_BUTTON_INDEX_ANY, XCB_BUTTON_MASK_ANY);
	for (i = 0; i < LEN(mods); i++) {
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

void gravitate(Client *c, int horz, int vert, int matchgap)
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
	switch (horz) {
	case GRAV_LEFT:   x = mx + gap; break;
	case GRAV_RIGHT:  x = mx + mw - W(c) - gap; break;
	case GRAV_CENTER: x = (mx + mw - W(c)) / 2; break;
	}
	switch (vert) {
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
	unsigned int cx, cy, cw, ch;
	unsigned int wx, wy, ww, wh;
	unsigned int i, n, b, g, cols, rows, col, row;

	for (n = 0, c = nexttiled(ws->clients); c; c = nexttiled(c->next), n++)
		;
	if (!n)
		return 1;

	/* grid dimensions */
	for (cols = 0; cols <= n / 2; cols++)
		if (cols * cols >= n)
			break;

	/* set layout against the general calculation: not 1:2:2, but 2:3 */
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
		b = globalcfg[GLB_SMART_BORDER] && n == 1 ? 0 : c->bw;
		ch = rows ? (wh - g) / rows : wh;
		cw = cols ? (ww - g) / cols : ww;
		cx = (wx + g) + col * cw;
		cy = (wy + g) + row * ch;
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

	clientrule(c, NULL);
	c->w = CLAMP(c->w, globalcfg[GLB_MIN_WH], c->ws->mon->ww);
	c->h = CLAMP(c->h, globalcfg[GLB_MIN_WH], c->ws->mon->wh);
	if (c->trans) {
		c->state |= STATE_FLOATING;
		c->x = c->trans->x + ((W(c->trans) - W(c)) / 2);
		c->y = c->trans->y + ((H(c->trans) - H(c)) / 2);
	}
	xcb_configure_window(con, c->win, XCB_CONFIG_WINDOW_BORDER_WIDTH, &c->bw);
	sendconfigure(c);
	clienttype(c);
	sizehints(c, 1);
	clienthints(c);
	xcb_change_window_attributes(con, c->win, XCB_CW_EVENT_MASK,
			(unsigned int[]){XCB_EVENT_MASK_ENTER_WINDOW
							| XCB_EVENT_MASK_FOCUS_CHANGE
							| XCB_EVENT_MASK_PROPERTY_CHANGE
							| XCB_EVENT_MASK_STRUCTURE_NOTIFY});
	drawborder(c, 0);
	grabbuttons(c, 0);
	if (FLOATING(c)) {
		c->x = CLAMP(c->x, c->ws->mon->wx, c->ws->mon->wx + c->ws->mon->ww - W(c));
		c->y = CLAMP(c->y, c->ws->mon->wy, c->ws->mon->wy + c->ws->mon->wh - H(c));
		if (c->x == c->ws->mon->wx && c->y == c->ws->mon->wy)
			offsetfloat(c, &c->x, &c->y, &c->w, &c->h);
		setstackmode(c->win, XCB_STACK_MODE_ABOVE);
	}
	PROP_APPEND(root, netatom[NET_CLIENTS], XCB_ATOM_WINDOW, 32, 1, &c->win);
	MOVE(c->win, c->x + 2 * scr_w, c->y);
	setwmwinstate(c->win, XCB_ICCCM_WM_STATE_NORMAL);
	if (c->ws == c->ws->mon->ws)
		unfocus(selws->sel, 0);
	c->ws->sel = c;
	if (c->cb)
		c->cb->fn.callback(c, 0);
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
			globalcfg[GLB_FOCUS_MOUSE], globalcfg[GLB_SIZEHINT],
			globalcfg[GLB_MIN_XY], globalcfg[GLB_MIN_WH]);

	fprintf(f, "\n\n# width outer_width focus urgent unfocus "
			"outer_focus outer_urgent outer_unfocus\n"
			"border: %d %d #%08x #%08x #%08x #%08x #%08x #%08x",
			border[BORD_WIDTH], border[BORD_O_WIDTH],
			border[BORD_FOCUS], border[BORD_URGENT],
			border[BORD_UNFOCUS], border[BORD_O_FOCUS],
			border[BORD_O_URGENT], border[BORD_O_UNFOCUS]);

	fprintf(f, "\n\n# number:name:layout ...\nworkspaces:");
	FOR_EACH(ws, workspaces)
		fprintf(f, " %s%d:%s:%s", ws == selws ? "*" : "", ws->num + 1, ws->name, ws->layout->name);
	fprintf(f, "\n\t# number:name active_window nmaster "
			"nstack msplit ssplit gappx padl padr padt padb");
	FOR_EACH(ws, workspaces) {
		fprintf(f, "\n\t%d:%s #%08x %d %d %0.2f %0.2f %d %d %d %d %d",
				ws->num + 1, ws->name, ws->sel ? ws->sel->win : 0, ws->nmaster, ws->nstack,
				ws->msplit, ws->ssplit, ws->gappx, ws->padl, ws->padr, ws->padt, ws->padb);
	}

	fprintf(f, "\n\n# number:name:workspace ...\nmonitors:");
	FOR_EACH(m, monitors)
		if (m->connected)
			fprintf(f, " %s%d:%s:%d", m->ws == selws ? "*" : "", m->num + 1, m->name, m->ws->num);
	fprintf(f, "\n\t# number:name active_window x y width height wx wy wwidth wheight");
	FOR_EACH(m, monitors) {
		if (m->connected) {
			fprintf(f, "\n\t%d:%s #%08x %d %d %d %d %d %d %d %d",
					m->num + 1, m->name, m->ws->sel ? m->ws->sel->win : 0,
					m->x, m->y, m->w, m->h, m->wx, m->wy, m->ww, m->wh);
		}
	}

	fprintf(f, "\n\n# id:workspace ...\nwindows:");
	FOR_CLIENTS(c, ws)
		fprintf(f, " %s#%08x:%d", c == selws->sel ? "*" : "", c->win, c->ws->num);
	fprintf(f, "\n\t# id title class instance x y width height bw hoff "
			"float full fakefull fixed stick urgent callback trans_id");
	FOR_CLIENTS(c, ws) {
		fprintf(f, "\n\t#%08x \"%s\" \"%s\" \"%s\" %d %d %d %d %d %d %d %d %d %d %d %d %s #%08x",
				c->win, c->title, c->class, c->inst, c->x, c->y, c->w, c->h, c->bw,
				c->hoff, FLOATING(c), (c->state & STATE_FULLSCREEN) != 0,
				(c->state & STATE_FAKEFULL) != 0, (c->state & STATE_FIXED) != 0,
				(c->state & STATE_STICKY) != 0, (c->state & STATE_URGENT) != 0,
				c->cb ? c->cb->name : "", c->trans ? c->trans->win : 0);
	}

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
	d->next = desks;
	desks = d;
	xcb_change_window_attributes(con, d->win, XCB_CW_EVENT_MASK,
			(unsigned int[]){XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY});
	MOVERESIZE(d->win, d->x, d->y, d->w, d->h, 0);
	setwmwinstate(d->win, XCB_ICCCM_WM_STATE_NORMAL);
	setstackmode(d->win, XCB_STACK_MODE_BELOW);
	PROP_APPEND(root, netatom[NET_CLIENTS], XCB_ATOM_WINDOW, 32, 1, &d->win);
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
		p->strut_l = s[0];
		p->strut_r = s[1];
		p->strut_t = s[2];
		p->strut_b = s[3];
		updstruts(p, 1);
	}
	free(prop);
	p->next = panels;
	panels = p;
	xcb_change_window_attributes(con, p->win, XCB_CW_EVENT_MASK,
			(unsigned int[]){XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY});
	setwmwinstate(p->win, XCB_ICCCM_WM_STATE_NORMAL);
	PROP_APPEND(root, netatom[NET_CLIENTS], XCB_ATOM_WINDOW, 32, 1, &p->win);
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
	r->next = rules;
	rules = r;
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

void initsock(int send)
{
	int r = 0;
	char *hostname = NULL, *s;
	int display = 0, screen = 0;
	static struct sockaddr_un sockaddr;

	if (sockfd > 0) {
		if (!send)
			goto status_setup;
		return;
	}
	if (!(s = getenv("YAXWM_SOCK"))) {
		if ((r = xcb_parse_display(NULL, &hostname, &display, &screen)))
			snprintf(sock, sizeof(sock), "/tmp/yaxwm_%s_%i_%i.socket", // NOLINT
					hostname, display, screen);
		else
			strlcpy(sock, "/tmp/yaxwm.socket", sizeof(sock));
		if (!send && setenv("YAXWM_SOCK", sock, 0) < 0)
			err(1, "unable to export socket path to environment: %s", sock);
	} else {
		strlcpy(sock, s, sizeof(sock));
	}

	sockaddr.sun_family = AF_UNIX;
	strlcpy(sockaddr.sun_path, sock, sizeof(sockaddr.sun_path));
	if (sockaddr.sun_path[0] == '\0')
		err(1, "unable to write socket path: %s", sock);
	if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		err(1, "unable to create socket: %s", sock);

	if (send) {
		if (connect(sockfd, (struct sockaddr *)&sockaddr, sizeof(sockaddr)) < 0)
			err(1, "unable to connect to socket: %s", sock);
	} else {
		unlink(sock);
		if (bind(sockfd, (struct sockaddr *)&sockaddr, sizeof(sockaddr)) < 0)
			err(1, "unable to bind socket: %s", sock);
		if (listen(sockfd, SOMAXCONN) < 0)
			err(1, "unable to listen to socket: %s", sock);

status_setup:
		if (!(s = getenv("YAXWM_STATUS"))) {
			if (r || xcb_parse_display(NULL, &hostname, &display, &screen))
				snprintf(status, sizeof(status), "/tmp/yaxwm_%s_%i_%i.status", // NOLINT
						hostname, display, screen);
			else
				strlcpy(status, "/tmp/yaxwm.status", sizeof(status));
			if (setenv("YAXWM_STATUS", status, 0) < 0)
				warn("unable to export status file path to environment");
		} else {
			strlcpy(status, s, sizeof(status));
		}
	}
	free(hostname);
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

	/* signal handlers */
	sa.sa_handler = sighandle;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	for (i = 0; i < LEN(sigs); i++)
		if (sigaction(sigs[i], &sa, NULL) < 0)
			err(1, "unable to setup handler for signal: %d", sigs[i]);

	/* mouse cursors */
	if (xcb_cursor_context_new(con, scr, &ctx) < 0)
		err(1, "unable to create cursor context");
	for (i = 0; i < LEN(cursors); i++)
		cursor[i] = xcb_cursor_load_cursor(ctx, cursors[i]);
	xcb_cursor_context_free(ctx);

	/* init atoms first so we can use them in updworkspaces */
	initatoms(wmatom, wmatoms, LEN(wmatoms));
	initatoms(netatom, netatoms, LEN(netatoms));

	/* init RANDR and monitors */
	if ((ext = xcb_get_extension_data(con, &xcb_randr_id)) && ext->present) {
		randrbase = ext->first_event;
		xcb_randr_select_input(con, root, XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE);
		updrandr();
	} else {
		warnx("unable to get randr extension data");
	}
	if (randrbase < 0 || !nextmon(monitors))
		initmon(0, "default", 0, 0, 0, scr_w, scr_h);

	/* init workspaces */
	cws = winprop(root, netatom[NET_DESK_CUR], &r) && r < 100 ? r : 0;
	updworkspaces(MAX(cws + 1, globalcfg[GLB_NUMWS]));
	selws = workspaces;
	selmon = selws->mon;
	changews((ws = itows(cws)) ? ws : workspaces, 1, 0);

	/* init root window atoms */
	wmcheck = xcb_generate_id(con);
	xcb_create_window(con, XCB_COPY_FROM_PARENT, wmcheck, root, -1, -1, 1, 1, 0,
			XCB_WINDOW_CLASS_INPUT_ONLY, scr->root_visual, 0, NULL);
	PROP_REPLACE(wmcheck, netatom[NET_WM_CHECK], XCB_ATOM_WINDOW, 32, 1, &wmcheck);
	PROP_REPLACE(wmcheck, netatom[NET_WM_NAME], wmatom[WM_UTF8STR], 8, 5, "yaxwm");
	PROP_REPLACE(root, netatom[NET_WM_CHECK], XCB_ATOM_WINDOW, 32, 1, &wmcheck);
	PROP_REPLACE(root, netatom[NET_SUPPORTED], XCB_ATOM_ATOM, 32, LEN(netatom), netatom);
	xcb_delete_property(con, root, netatom[NET_CLIENTS]);

	/* root window event mask and default mouse cursor */
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

	/* still need this due to mouse binds */
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

void manage(xcb_window_t win, xcb_get_geometry_reply_t *g, xcb_get_window_attributes_reply_t *wa)
{
	xcb_atom_t type;

	DBG("manage: 0x%08x - %d,%d @ %dx%d", win, g->x, g->y, g->width, g->height)
	if (!(wintoclient(win) || wintopanel(win) || wintodesk(win))) {
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
		needsrefresh = 1;
	}
}

int mono(Workspace *ws)
{
	int g, b;
	Client *c;

	g = globalcfg[GLB_SMART_GAP] ? 0 : ws->gappx;
	for (c = nexttiled(ws->clients); c; c = nexttiled(c->next)) {
		b = globalcfg[GLB_SMART_BORDER] ? 0 : c->bw;
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
	int i = 0;
	Client *c, *t;

	if (!(c = cmdclient) || FLOATING(c) || !nexttiled(c->ws->clients->next))
		return;
	while (direction) {
		if (direction > 0) {
			detach(c, (t = nexttiled(c->next)) ? 0 : 1);
			if (t) {
				c->next = t->next;
				t->next = c;
			}
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

void offsetfloat(Client *c, int *x, int *y, int *w, int *h)
{
	Monitor *m = c->ws->mon;
	static int offset = 0;
	static Workspace *ws = NULL;
	if (ws != c->ws) {
		ws = c->ws;
		offset = 0;
	}
	*x = MIN(m->wx + m->ww - (*w + (2 * c->bw)), m->wx + (m->ww / 8) + offset);
	*y = MIN(m->wy + m->wh - (*h + (2 * c->bw)), m->wy + (m->wh / 8) + offset);
	if (*x + *w + (2 * c->bw) < m->wx + m->ww && *y + *h + (2 * c->bw) < m->wy + m->wh)
		offset += globalcfg[GLB_MIN_WH];
	else
		offset += (offset * -1) + rand() % 200;
}

Monitor *outputtomon(xcb_randr_output_t id)
{
	Monitor *m;

	FOR_EACH(m, monitors)
		if (m->id == id)
			return m;
	return m;
}

char **parsebool(char **argv, int *setting)
{
	int i;
	char *end;

	if (!argv || !*argv)
		return argv;
	if (((i = !strcmp("true", *argv)) || !strcmp("false", *argv))
			|| (((i = strtoul(*argv, &end, 0)) > 0 || !strcmp("0", *argv)) && *end == '\0'))
		*setting = i ? 1 : 0;
	else
		fprintf(cmdresp, "!invalid boolean argument: %s - expected true, false, 1, 0\n", *argv);
	return argv;
}

int parseclient(char **argv, Client **c)
{
	char *end;
	unsigned int i;

	if (!argv || !*argv)
		return 0;
	if ((*argv[0] == '#' || (*argv[0] == '0' && *argv[0] == 'x'))
			&& (i = strtoul(**argv == '#' ? *argv + 1 : *argv, &end, 16)) > 0 && *end == '\0')
	{
		if ((*c = wintoclient(i)))
			return 1;
		fprintf(cmdresp, "!invalid window id argument: %s\n", *argv);
		return -1;
	}
	return 0;
}

void parsecmd(char *buf)
{
	int n = 0, max = 32;
	char **argv, **new, **save, *tok, *key;

	DBG("parsecmd: tokenizing buffer: %s", buf)
	if (!(key = parsetoken(&buf)))
		return;
	for (unsigned int i = 0; i < LEN(keywords); i++) {
		if (!strcmp(keywords[i].name, key)) {
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
			save = argv;
#ifdef DEBUG
			DBG("parsecmd: key = %s", key)
			for (int j = 0; j < n; j++) {
				DBG("parsecmd: argv[%d] = %s", j, argv[j])
			}
#endif
			if (n) {
				cmdusemon = keywords[i].fn.keyword == cmdmon;
				keywords[i].fn.keyword(argv);
			} else {
				fprintf(cmdresp, "!%s %s\n", key, enoargs);
			}
			free(save);
			goto finish;
		}
	}
	fprintf(cmdresp, "!invalid or unknown command: %s\n", key);

finish:
	fflush(cmdresp);
	fclose(cmdresp);
}

char **parsecolour(char **argv, unsigned int *setting)
{
	char *end;
	unsigned short a, r, g, b;
	unsigned int argb, len;

	if (!argv || !*argv)
		return argv;
	if ((len = strlen(*argv)) >= 6 && len <= 10) {
		if (**argv == '#' && len >= 7 && len <= 9)
			len--;
		else if (**argv == '0' && *(*argv + 1) == 'x')
			len -= 2;
		argb = strtoul(**argv == '#' ? *argv + 1 : *argv, &end, 16);
		if (argb <= 0xffffffff && *end == '\0') {
			if (len == 6) {
				*setting = (argb | 0xff000000);
			} else if ((a = ((argb & 0xff000000) >> 24)) && a != 0xff) {
				r = (((argb & 0xff0000) >> 16) * a) / 255;
				g = (((argb & 0xff00) >> 8) * a) / 255;
				b = (((argb & 0xff) >> 0) * a) / 255;
				*setting = (a << 24 | r << 16 | g << 8 | b);
			} else {
				*setting = argb;
			}
			return argv;
		}
	}
	fprintf(cmdresp, "!invalid colour argument: %s - expected (#/0x)(AA)RRGGBB\n", *argv);
	return argv;
}

char **parsefloat(char **argv, float *setting, int *rel)
{
	float f;
	char *end;

	if (!argv || !*argv)
		return argv;
	if ((f = strtof(*argv, &end)) != 0.0 && *end == '\0') {
		if (f < -0.95 || f > 0.95) {
			fprintf(cmdresp, "!float argument out of range: %s - min: -0.95, max: 0.95\n", *argv);
		} else {
			if (rel) /* check if it's a relative number (has a sign) */
				*rel = **argv == '-' || **argv == '+';
			*setting = f;
		}
		return argv;
	}
	fprintf(cmdresp, "!invalid or incomplete float argument: %s - expected (-/+)0.N\n", *argv);
	return argv;
}

char **parsegeom(char **argv, char type, int *i, int *rel, int *grav)
{
	if (!argv || !*argv)
		return argv;
	if (!grav) {
		argv = parseint(argv, i, rel, type == 'x' || type == 'y' ? 1 : 0);
	} else if (grav && !strcmp("center", *argv)) {
		*grav = GRAV_CENTER;
	} else {
		switch (type) {
		case 'x':
			if (grav && !strcmp("left", *argv)) *grav = GRAV_LEFT;
			else if (grav && !strcmp("right", *argv)) *grav = GRAV_RIGHT;
			else argv = parseint(argv, i, rel, 1);
			break;
		case 'y':
			if (grav && !strcmp("top", *argv)) *grav = GRAV_TOP;
			else if (grav && !strcmp("bottom", *argv)) *grav = GRAV_BOTTOM;
			else argv = parseint(argv, i, rel, 1);
			break;
		case 'w': /* FALLTHROUGH */
		case 'h':
			argv = parseint(argv, i, rel, 0);
			break;
		}
	}
	return argv;
}

char **parseint(char **argv, int *setting, int *rel, int allowzero)
{
	int i;
	char *end;

	if (rel)
		*rel = 0;
	if (!argv || !*argv)
		return argv;
	if (((i = strtol(*argv, &end, 0)) || (allowzero && !strcmp("0", *argv))) && *end == '\0') {
		if (i && rel) /* check if it's a relative number (non-zero, has a sign) */
			*rel = **argv == '-' || **argv == '+';
		*setting = i;
	} else {
		fprintf(cmdresp, "!invalid integer argument: %s - expected (-/+)N\n", *argv);
	}
	return argv;
}

char **parseintclamp(char **argv, int *setting, int *rel, int min, int max)
{
	int i = min - 1;

	if (!argv || !*argv)
		return argv;
	parseint(argv, &i, rel, 1);
	if (i >= min && i <= max)
		*setting = i;
	else if (i != min - 1)
		fprintf(cmdresp, "!int argument out of range: %s - min: %d, max: %d\n", *argv, min, max);
	return argv;
}

int parseopt(char **argv, char **opts)
{
	int i;
	char **s;

	if (!argv || !*argv)
		return -1;
	for (s = opts, i = 0; s && *s; s++, i++)
		if (!strcmp(*s, *argv))
			return i;
	return -1;
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

int querypointer(int *x, int *y)
{
	xcb_generic_error_t *e;
	xcb_query_pointer_reply_t *p;

	if ((p = xcb_query_pointer_reply(con, xcb_query_pointer(con, root), &e))) {
		*x = p->root_x, *y = p->root_y;
		free(p);
		return 1;
	} else {
		iferr(0, "unable to query pointer", e);
	}
	return 0;
}

void refresh(void)
{
	Desk *d;
	Panel *p;
	Client *c;
	Workspace *ws;

#define DOMAPS(v, list)                                                        \
	do {                                                                       \
		FOR_EACH((v), (list))                                                  \
			if ((v)->state & STATE_NEEDSMAP) {                                 \
				(v)->state &= ~STATE_NEEDSMAP;                                 \
				xcb_map_window(con, (v)->win);                                 \
			}                                                                  \
	} while (0)

	DOMAPS(p, panels);
	DOMAPS(d, desks);
	FOR_EACH(ws, workspaces) {
		showhide(ws->stack);
		if (ws == ws->mon->ws && ws->layout->fn.layout)
			ws->layout->fn.layout(ws);
		DOMAPS(c, ws->clients);
		restack(ws);
	}
	focus(NULL);
	eventignore(XCB_ENTER_NOTIFY);
	pushstatus();
#undef DOMAPS
}

void relocate(Workspace *ws, Monitor *old)
{
	Client *c;
	Monitor *m;
	float xoff, yoff;
	float xdiv, ydiv;

	DBG("relocate: moving clients from %s to %s", old->name, ws->mon->name)
	if (!(m = ws->mon) || m == old)
		return;
	FOR_EACH(c, ws->clients)
		if (FLOATING(c)) { /* TODO: simplify this mess */
			if ((xoff = c->x - old->x) && (xdiv = old->w / xoff) != 0.0) {
				if (c->x + W(c) == old->wx + old->ww) /* edge */
					c->x = m->wx + m->ww - W(c);
				else if (c->x + (W(c) / 2) == old->wx + (old->ww / 2)) /* center */
					c->x = (m->wx + m->ww - W(c)) / 2;
				else
					c->x = CLAMP(m->wx + (m->ww / xdiv), m->wx - (W(c) - globalcfg[GLB_MIN_XY]),
							m->wx + m->ww - globalcfg[GLB_MIN_XY]);
			} else
				c->x = CLAMP(c->x, m->wx - (W(c) - globalcfg[GLB_MIN_XY]),
						m->x + m->w - globalcfg[GLB_MIN_XY]);
			if ((yoff = c->y - old->y) && (ydiv = old->h / yoff) != 0.0) {
				if (c->y + H(c) == old->wy + old->wh) /* edge */
					c->y = m->wy + m->wh - H(c);
				else if (c->y + (H(c) / 2) == old->wy + (old->wh / 2)) /* center */
					c->y = (m->wy + m->wh - H(c)) / 2;
				else
					c->y = CLAMP(m->wy + (m->wh / ydiv), m->wy - (H(c) - globalcfg[GLB_MIN_XY]),
							m->wy + m->wh - globalcfg[GLB_MIN_XY]);
			} else
				c->y = CLAMP(c->y, m->wy - (H(c) - globalcfg[GLB_MIN_XY]),
						m->wy + m->wh - globalcfg[GLB_MIN_XY]);
		}
}

void resize(Client *c, int x, int y, int w, int h, int bw)
{
	c->old_x = c->x, c->old_y = c->y, c->old_w = c->w, c->old_h = c->h;
	c->x = x, c->y = y, c->w = w, c->h = h;
	MOVERESIZE(c->win, x, y, w, h, bw);
	drawborder(c, c == selws->sel);
	sendconfigure(c);
}

void resizehint(Client *c, int x, int y, int w, int h, int bw, int usermotion, int mouse)
{
	if (applysizehints(c, &x, &y, &w, &h, bw, usermotion, mouse))
		resize(c, x, y, w, h, bw);
}

int resizetiled(Client *c, Client *p, int x, int y, int w, int h, int bw, int gap,
		int ww, int wh, int *newy, int nrem, int havail)
{
	int ret = 1;
	int b = bw ? c->bw : bw;
	int min = MAX(globalcfg[GLB_MIN_WH], c->min_h);

	DBG("resizetiled: 0x%08x - %d,%d @ %dx%d - newy: %d, nrem: %d, havail; %d",
			c->win, x, y, w, h, *newy, nrem, havail)
	if (!c->hoff && h < min) {
		c->state |= STATE_FLOATING;
		h = MAX(wh / 4, 240);
		w = MAX(ww / 5, 360);
		offsetfloat(c, &x, &y, &w, &h);
		setstackmode(c->win, XCB_STACK_MODE_ABOVE);
		resizehint(c, x, y, w, h, c->bw, 0, 0);
		return ret;
	} else if (nrem > 1 && (nrem - 1) * (min + gap) > havail) {
		h += havail - ((nrem - 1) * (min + gap));
		ret = -1;
	} else if (nrem == 1 && *newy + (h - gap) != wh) {
		if (p) {
			p->old_h = p->h;
			if (p->h + havail < (min = MAX(globalcfg[GLB_MIN_WH], p->min_h))) {
				ret = -1;
				p->h = min;
				y = p->y + min + gap;
				h = wh - (p->y + p->h);
			} else if (h < min) {
				ret = -1;
				p->h += havail - (min - h - (2 * b));
				y = p->y + p->h + (2 * b) + gap;
				h = min - (2 * b);
			} else {
				p->h += havail;
				y += havail;
			}
		} else {
			h = wh;
			ret = -1;
		}
	} else if (h < min) {
		ret = -1;
		h = min;
	}
	c->old_x = c->x, c->old_y = c->y, c->old_w = c->w, c->old_h = c->h;
	c->x = x, c->y = y, c->w = w - (2 * b), c->h = h - (2 * b);
	*newy += h + gap;
	return ret;
}

void restack(Workspace *ws)
{
	Desk *d;
	Panel *p;
	Client *c;
	uint32_t v[] = { XCB_WINDOW_NONE, XCB_STACK_MODE_ABOVE };

	if (!ws || !(ws = selws) || !(c = ws->sel))
		return;
	FOR_EACH(p, panels)
		if (p->mon == ws->mon)
			setstackmode(c->win, XCB_STACK_MODE_ABOVE);
	if (FLOATING(c))
		setstackmode(c->win, XCB_STACK_MODE_ABOVE);
	FOR_STACK(c, ws->stack) {
		if (c->trans && FLOATING(c)) {
			v[0] = c->trans->win;
			xcb_configure_window(con, c->win,
					XCB_CONFIG_WINDOW_SIBLING | XCB_CONFIG_WINDOW_STACK_MODE, v);
		} else if (!FLOATING(c) && ws == ws->mon->ws) {
			setstackmode(c->win, XCB_STACK_MODE_BELOW);
		}
	}
	FOR_EACH(d, desks)
		if (d->mon == ws->mon)
			setstackmode(c->win, XCB_STACK_MODE_BELOW);
}

int rulecmp(Rule *r, char *title, char *class, char *inst)
{
	DBG("rulecmp: %s - %s, %s - %s, %s - %s", r->class, class, r->inst, inst, r->title, title)
	return !((r->class && regexec(&(r->classreg), class, 0, NULL, 0))
			|| (r->inst && regexec(&(r->instreg), inst, 0, NULL, 0))
			|| (r->title && regexec(&(r->titlereg), title, 0, NULL, 0)));
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
	int n, exists = 0;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t rpc;
	xcb_client_message_event_t cme;
	xcb_icccm_get_wm_protocols_reply_t proto;

	DBG("sendwmproto: checking support for %s", wmatoms[wmproto])
		rpc = xcb_icccm_get_wm_protocols(con, c->win, wmatom[WM_PROTO]);
	if (xcb_icccm_get_wm_protocols_reply(con, rpc, &proto, &e)) {
		n = proto.atoms_len;
		while (!exists && n--)
			exists = proto.atoms[n] == wmatom[wmproto];
		xcb_icccm_get_wm_protocols_reply_wipe(&proto);
	} else {
		iferr(0, "unable to get requested wm protocol", e);
		xcb_aux_sync(con);
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
		PROP_REPLACE(c->win, netatom[NET_WM_STATE], XCB_ATOM_ATOM, 32, 1, &netatom[NET_STATE_FULL]);
		c->old_state = c->state;
		c->state |= STATE_FULLSCREEN | STATE_FLOATING;
		resize(c, m->x, m->y, m->w, m->h, 0);
		setstackmode(c->win, XCB_STACK_MODE_ABOVE);
	} else if (!fullscreen && (c->state & STATE_FULLSCREEN)) {
		PROP_REPLACE(c->win, netatom[NET_WM_STATE], XCB_ATOM_ATOM, 32, 0, (unsigned char *)0);
		c->state = c->old_state;
		resize(c, c->old_x, c->old_y, c->old_w, c->old_h, c->bw);
		needsrefresh = 1;
	}
}

void setinputfocus(Client *c)
{
	if (!(c->state & STATE_NOINPUT)) {
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, c->win, XCB_CURRENT_TIME);
		PROP_REPLACE(root, netatom[NET_ACTIVE], XCB_ATOM_WINDOW, 32, 1, &c->win);
	}
	sendwmproto(c, WM_FOCUS);
}

void setstackmode(xcb_window_t win, unsigned int mode)
{
	xcb_configure_window(con, win, XCB_CONFIG_WINDOW_STACK_MODE, &mode);
}

void setwmwinstate(xcb_window_t win, long state)
{
	long data[] = { state, GRAV_NONE };
	PROP_REPLACE(win, wmatom[WM_STATE], wmatom[WM_STATE], 32, 2, (unsigned char *)data);
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
	PROP_REPLACE(root, netatom[NET_DESK_NAMES], wmatom[WM_UTF8STR], 8, --len, names);
	free(names);
}

void setworkspace(Client *c, int num)
{
	Workspace *ws;

	if (!(ws = itows(num)) || ws == c->ws)
		return;
	DBG("setworkspace: 0x%08x -> %d", c->win, num)
	if (c->ws) {
		detach(c, 0);
		detachstack(c);
	}
	if (!(c->ws = itows(num)))
		c->ws = selws;
	PROP_REPLACE(c->win, netatom[NET_WM_DESK], XCB_ATOM_CARDINAL, 32, 1, &c->ws->num);
	attach(c, globalcfg[GLB_TILETOHEAD]);
	attachstack(c);
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
	Client *sel;

	if (!c)
		return;
	m = c->ws->mon;
	if (c->ws == m->ws) {
		MOVE(c->win, c->x, c->y);
		if (FLOATING(c)) {
			if (c->state & STATE_FULLSCREEN && c->w == c->ws->mon->w && c->h == c->ws->mon->h)
				resize(c, m->x, m->y, m->w, m->h, 0);
			else
				resize(c, c->x, c->y, c->w, c->h, c->bw);
		}
		showhide(c->snext);
	} else {
		showhide(c->snext);
		if (c->state & STATE_STICKY) {
			sel = lastws->sel == c ? c : selws->sel;
			setworkspace(c, selws->num);
			focus(sel);
		} else if (c->ws != selws && m == selws->mon) {
			MOVE(c->win, W(c) * -2, c->y);
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
		if (uss && s.flags & XCB_ICCCM_SIZE_HINT_US_SIZE)
			c->w = s.width, c->h = s.height;
		if (uss && s.flags & XCB_ICCCM_SIZE_HINT_US_POSITION)
			c->x = s.x, c->y = s.y;
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
	fib(ws, 0);
	return 1;
}

size_t strlcat(char *dst, const char *src, size_t siz)
{
	char *d = dst;
	const char *s = src;
	size_t n = siz;
	size_t dlen;

	while (n-- != 0 && *d != '\0')
		d++;
	dlen = d - dst;
	n = siz - dlen;
	if (n == 0)
		return dlen + strlen(s);
	while (*s != '\0') {
		if (n != 1) {
			*d++ = *s;
			n--;
		}
		s++;
	}
	*d = '\0';
	return dlen + (s - src);
}

size_t strlcpy(char *dst, const char *src, size_t dsize)
{
	const char *osrc = src;
	size_t nleft = dsize;

	if (nleft != 0) {
		while (--nleft != 0)
			if ((*dst++ = *src++) == '\0')
				break;
	}
	if (nleft == 0) {
		if (dsize != 0)
			*dst = '\0';
		while (*src++)
			;
	}
	return (src - osrc - 1);
}

int tile(Workspace *ws)
{
	int ret = 1;
	Monitor *m = ws->mon;
	Client *c, *prev = NULL;
	int i, n, nr, my, sy, ssy, w, h, b, g;
	int wx, wy, ww, wh, mw, ss, sw, ssw, ns;

	for (n = 0, c = nexttiled(ws->clients); c; c = nexttiled(c->next), n++)
		;
	if (!n)
		return 1;

	wx = m->wx + ws->padl;
	wy = m->wy + ws->padt;
	ww = m->ww - ws->padl - ws->padr;
	wh = m->wh - ws->padt - ws->padb;
	mw = 0, ss = 0, sw = 0, ssw = 0, ns = 1;
	g = globalcfg[GLB_SMART_GAP] && n == 1 ? 0 : ws->gappx;
	b = globalcfg[GLB_SMART_BORDER] && n == 1 ? 0 : 1;

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

	/* calculate sizes and update our internal values for the clients first */
	for (i = 0, my = sy = ssy = g, c = nexttiled(ws->clients); c; c = nexttiled(c->next), ++i) {
		if (i < ws->nmaster) {
			nr = MIN(n, ws->nmaster) - i;
			h = ((wh - my) / MAX(1, nr)) - g + c->hoff;
			w = mw - g * (5 - ns) / 2;
			if (resizetiled(c, prev, wx + g, wy + my, w, h, b, g,
						ww - (2 * g), wh - (2 * g), &my, nr, wh - (my + h + g)) < 0)
				ret = -1;
		} else if (i - ws->nmaster < ws->nstack) {
			nr = MIN(n - ws->nmaster, ws->nstack) - (i - ws->nmaster);
			h = ((wh - sy) / MAX(1, nr)) - g + c->hoff;
			w = sw - g * (5 - ns - ss) / 2;
			if (resizetiled(c, prev, wx + mw + (g / ns), wy + sy, w, h, b, g,
						ww - (2 * g), wh - (2 * g), &sy, nr, wh - (sy + h + g)) < 0)
				ret = -1;
		} else {
			nr = n - i;
			h = ((wh - ssy) / MAX(1, nr)) - g + c->hoff;
			w = ssw - g * (5 - ns) / 2;
			if (resizetiled(c, prev, wx + mw + sw + (g / ns), wy + ssy, w, h, b, g,
						ww - (2 * g), wh - (2 * g), &ssy, nr, wh - (ssy + h + g)) < 0)
				ret = -1;
		}
		prev = (nr == 1 && n - i != 0) ? NULL : c;
	}

	/* now do the resizing, this reduces the flicker of resizing previous
	 * clients when accommodating height offsets on the last client in each stack */
	for (c = nexttiled(ws->clients); c; c = nexttiled(c->next))
		if (applysizehints(c, &c->x, &c->y, &c->w, &c->h, b ? c->bw : 0, 0, 0)
				|| c->x != c->old_x || c->y != c->old_y || c->w != c->old_w
				|| c->h != c->old_h || (b ? c->bw : 0) != c->old_bw)
		{
			MOVERESIZE(c->win, c->x, c->y, c->w, c->h, b ? c->bw : 0);
			drawborder(c, c == selws->sel);
			sendconfigure(c);
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
		if (c->cb && running)
			c->cb->fn.callback(c, 1);
		detach(c, 0);
		detachstack(c);
	} else if ((ptr = p = wintopanel(win))) {
		pp = &panels;
		DETACH(p, pp);
		updstruts(p, 0);
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
				/* spec says these should be removed on withdraw but not on wm shutdown */
				xcb_delete_property(con, c->win, netatom[NET_WM_STATE]);
				xcb_delete_property(con, c->win, netatom[NET_WM_DESK]);
			}
		}
		setwmwinstate(win, XCB_ICCCM_WM_STATE_WITHDRAWN);
		xcb_aux_sync(con);
		xcb_ungrab_server(con);
	}

	if (ptr) {
		free(ptr);
		xcb_delete_property(con, root, netatom[NET_CLIENTS]);
		FOR_CLIENTS(c, ws) // NOLINT
			PROP_APPEND(root, netatom[NET_CLIENTS], XCB_ATOM_WINDOW, 32, 1, &c->win);
		FOR_EACH(p, panels)
			PROP_APPEND(root, netatom[NET_CLIENTS], XCB_ATOM_WINDOW, 32, 1, &p->win);
		FOR_EACH(d, desks)
			PROP_APPEND(root, netatom[NET_CLIENTS], XCB_ATOM_WINDOW, 32, 1, &d->win);
		needsrefresh = 1;
	} else {
		focus(NULL);
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
	int n, changed = 0;
	xcb_generic_error_t *e;
	xcb_randr_get_screen_resources_reply_t *r;
	xcb_randr_get_screen_resources_cookie_t rc;

	rc = xcb_randr_get_screen_resources(con, root);
	if ((r = xcb_randr_get_screen_resources_reply(con, rc, &e))) {
		if ((n = xcb_randr_get_screen_resources_outputs_length(r)) <= 0) {
			warnx("no monitors available");
		} else {
			changed = updoutputs(xcb_randr_get_screen_resources_outputs(r), n, r->config_timestamp);
		}
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
			if ((apply || n != p) && (n->strut_l || n->strut_r || n->strut_t || n->strut_b))
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
	PROP_REPLACE(root, netatom[NET_DESK_GEOM], XCB_ATOM_CARDINAL, 32, 2, &v);
	PROP_REPLACE(root, netatom[NET_DESK_NUM], XCB_ATOM_CARDINAL, 32, 1, &globalcfg[GLB_NUMWS]);
	FOR_EACH(ws, workspaces) {
		if (!ws->mon)
			ws->mon = primary;
		v[0] = ws->mon->x, v[1] = ws->mon->y;
		PROP_APPEND(root, netatom[NET_DESK_VP], XCB_ATOM_CARDINAL, 32, 2, &v);
		v[0] = ws->mon->wx, v[1] = ws->mon->wy, v[2] = ws->mon->ww, v[3] = ws->mon->wh;
		PROP_APPEND(root, netatom[NET_DESK_WA], XCB_ATOM_CARDINAL, 32, 4, &v);
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

	/* allocate the needed workspaces for monitors or user specified */
	for (n = 0, m = nextmon(monitors); m; m = nextmon(m->next), n++)
		;
	if (n < 1 || n > 99 || needed > 99) {
		warnx(n < 1 ? "no connected monitors" : "allocating too many workspaces: max 99");
		return;
	} else while (n > globalcfg[GLB_NUMWS] || needed > globalcfg[GLB_NUMWS]) {
		initws(globalcfg[GLB_NUMWS]);
		globalcfg[GLB_NUMWS]++;
	}

	/* attach at least one workspace to each monitor, round robin style */
	m = nextmon(monitors);
	FOR_EACH(ws, workspaces) {
		m->ws = m->ws ? m->ws : ws;
		ws->mon = m;
		DBG("updworkspaces: %d:%s -> %s - visible: %d", ws->num, ws->name, m->name, ws == m->ws)
		if (!(m = nextmon(m->next)))
			m = nextmon(monitors);
	}

	/* relocate and refresh */
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

void usage(int e, char flag)
{
	switch (flag) {
	case 'h': fprintf(stderr, "usage: yaxwm [-hv] [-s SOCKET_FD] [-c COMMAND]\n"); break;
	case 'v': fprintf(stderr, "yaxwm "VERSION"\n"); break;
	}
	exit(e);
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
	DBG("winprop: getting window property atom - 0x%08x", win)
	if ((r = xcb_get_property_reply(con, c, &e)) && xcb_get_property_value_length(r)) {
		i = 1;
		*ret = *(xcb_atom_t *)xcb_get_property_value(r);
		DBG("winprop: property reply value: %d", *ret)
	} else
		iferr(0, "unable to get window property reply", e);
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

int writecmd(int argc, char *argv[])
{
	ssize_t s;
	size_t j, n = 0;
	int i, r = 0, offs;
	char *eq = NULL, *sp = NULL, buf[BUFSIZ], resp[BUFSIZ];

	if (!argc)
		errx(1, "%s", enoargs);

	initsock(1);
	struct pollfd fds[] = {
		{ sockfd,        POLLIN,  0 },
		{ STDOUT_FILENO, POLLHUP, 0 },
	};

	/* wrap arguments containing whitespace or other parser delimiters in double quotes */
	for (i = 0, j = 0, offs = 1; n + 1 < sizeof(buf) && i < argc; i++, j = 0, offs = 1) {
		if ((sp = strchr(argv[i], ' ')) || (sp = strchr(argv[i], '\t'))) {
			if (!(eq = strchr(argv[i], '=')) || sp < eq)
				buf[n++] = '"';
			offs++;
		}
		while (n + offs < sizeof(buf) && argv[i][j]) {
			buf[n++] = argv[i][j++];
			if (eq && sp > eq && buf[n - 1] == '=') {
				buf[n++] = '"';
				eq = NULL;
			}
		}
		if (offs > 1)
			buf[n++] = '"';
		buf[n++] = ' ';
	}
	buf[n - 1] = '\0';
	if (send(sockfd, buf, n, 0) < 0)
		err(1, "unable to send the command");
	while (poll(fds, 2, 1000) > 0) {
		if (fds[1].revents & (POLLERR | POLLHUP))
			break;
		if (fds[0].revents & POLLIN) {
			if ((s = recv(sockfd, resp, sizeof(resp) - 1, 0)) > 0) {
				resp[s] = '\0';
				if (*resp == '!') {
					fprintf(stderr, "yaxwm: error: %s", resp + 1);
					fflush(stderr);
				} else {
					fprintf(stdout, "%s", resp);
					fflush(stdout);
				}
			} else {
				break;
			}
		}
	}
	close(sockfd);
	exit(r);
}
