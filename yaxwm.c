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

#include <xcb/dpms.h>
#include <xcb/randr.h>
#include <xcb/xproto.h>
#include <xcb/xcb_util.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_cursor.h>
#include <xcb/xcb_keysyms.h>


#ifdef DEBUG
#define DBG(fmt, ...); warnx("%d: "fmt, __LINE__, ##__VA_ARGS__);
#else
#define DBG(fmt, ...);
#endif

#ifndef VERSION
#define VERSION "0.6"
#endif

#define W(c)                ((c)->w + (2 * (c)->bw))
#define H(c)                ((c)->h + (2 * (c)->bw))
#define MIN(a, b)           ((a) < (b) ? (a) : (b))
#define MAX(a, b)           ((a) > (b) ? (a) : (b))
#define CLAMP(x, min, max)  (MIN(MAX((x), (min)), (max)))
#define LEN(x)              (sizeof(x) / sizeof(x[0]))
#define CLNMOD(mod)         (mod & ~(lockmask | XCB_MOD_MASK_LOCK))
#define FLOATING(c)         ((c)->floating || !(c)->ws->layout->fn)
#define FULLSCREEN(c)       ((c)->fullscreen && !(c)->fakefull)
#define EVTYPE(e)           (e->response_type &  0x7f)
#define EVSENT(e)           (e->response_type & ~0x7f)
#define XYMASK              (XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y)
#define WHMASK              (XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT)
#define BUTTONMASK          (XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE)
#define ISFULL(c)           ((c)->fullscreen && (c)->w == (c)->ws->mon->w && (c)->h == (c)->ws->mon->h)

#define FOR_EACH(v, list)\
	for ((v) = (list); (v); (v) = (v)->next)
#define FIND_TAIL(v, list)\
	for ((v) = (list); (v) && (v)->next; (v) = (v)->next)
#define FIND_PREV(v, cur, list)\
	for ((v) = (list); (v) && (v)->next && (v)->next != (cur); (v) = (v)->next)

#define DETACH(v, listptr)\
		while (*(listptr) && *(listptr) != (v))\
			(listptr) = &(*(listptr))->next;\
		*(listptr) = (v)->next;\

#define FOR_STACK(v, list)\
	for ((v) = (list); (v); (v) = (v)->snext)
#define FOR_CLIENTS(c, ws)\
	FOR_EACH((ws), workspaces) FOR_EACH((c), (ws)->clients)

#define FIND_TILETAIL(v, list)\
	for ((v) = nextt((list)); (v) && nextt((v)->next); (v) = nextt((v)->next))

#define FIND_PREVTILED(v, cur, list)\
	for ((v) = nextt((list)); (v) && nextt((v)->next)\
			&& nextt((v)->next) != (cur); (v) = nextt((v)->next))

#define FIND_PREVMON(v, cur, list)\
	for ((v) = nextmon((list)); (v) && nextmon((v)->next)\
			&& nextmon((v)->next) != (cur); (v) = nextmon((v)->next))

#define MOVE(win, x, y)\
	xcb_configure_window(con, (win), XYMASK, (unsigned int []){(x), (y)})
#define MOVERESIZE(win, x, y, w, h, bw)\
	xcb_configure_window(con, win, XYMASK | WHMASK | XCB_CONFIG_WINDOW_BORDER_WIDTH,\
			(unsigned int []){(x), (y), (w), (h), (bw)});
#define PROP_APPEND(win, atom, type, membsize, nmemb, value)\
	xcb_change_property(con, XCB_PROP_MODE_APPEND, (win), (atom),\
			(type), (membsize), (nmemb), (value))
#define PROP_REPLACE(win, atom, type, membsize, nmemb, value)\
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, (win), (atom),\
			(type), (membsize), (nmemb), (value))

/* enums */
enum Cursors {
	Move,
	Normal,
	Resize
};

enum Gravity {
	None,
	Left,
	Right,
	Center,
	Top,
	Bottom
};

enum Borders {
	Width,
	Focus,
	Urgent,
	Unfocus,
	Outer,
	OFocus,
	OUrgent,
	OUnfocus
};

enum DirOpts {
	Next,
	Prev,
	Last,  /* last active */
	NextNE, /* non-empty */
	PrevNE, /* non-empty */
};

enum WMAtoms {
	Delete,
	MotifHints,
	Protocols,
	TakeFocus,
	Utf8Str,
	WMState
};

enum NetAtoms {
	Active,
	Check,
	ClientList,
	Close,
	CurDesktop,
	DemandsAttn,
	Desktop,
	DesktopNames,
	Dialog,
	Dock,
	FrameExtents,
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

enum GlobalCfg {
	SmartGap,
	SmartBorder,
	SizeHints,
	FocusMouse,
	FocusUrgent,
	NumWs,
	MinXY,
	MinWH
};

static char *opts[] = {
	[Next] = "next",
	[Prev] = "prev",
	[Last] = "last",
	[NextNE] = "nextne",
	[PrevNE] = "prevne",
	NULL
};

static const char *gravities[] = {
	[None] = "none",
	[Left] = "left",
	[Right] = "right",
	[Center] = "center",
	[Top] = "top",
	[Bottom] = "bottom",
};

static const char *wmatoms[] = {
	[Delete] = "WM_DELETE_WINDOW",
	[MotifHints] = "_MOTIF_WM_HINTS",
	[Protocols] = "WM_PROTOCOLS",
	[TakeFocus] = "WM_TAKE_FOCUS",
	[Utf8Str] = "UTF8_STRING",
	[WMState] = "WM_STATE",
};

static const char *netatoms[] = {
	[Active] = "_NET_ACTIVE_WINDOW",
	[Check] = "_NET_SUPPORTING_WM_CHECK",
	[ClientList] = "_NET_CLIENT_LIST",
	[Close] = "_NET_CLOSE_WINDOW",
	[CurDesktop] = "_NET_CURRENT_DESKTOP",
	[DemandsAttn] = "_NET_WM_STATE_DEMANDS_ATTENTION",
	[DesktopNames] = "_NET_DESKTOP_NAMES",
	[Desktop] = "_NET_WM_WINDOW_TYPE_DESKTOP",
	[Dialog] = "_NET_WM_WINDOW_TYPE_DIALOG",
	[Dock] = "_NET_WM_WINDOW_TYPE_DOCK",
	[FrameExtents] = "_NET_FRAME_EXTENTS",
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


/* type definitions */
typedef struct Map Map;
typedef struct Rule Rule;
typedef struct Desk Desk;
typedef struct Panel Panel;
typedef struct Client Client;
typedef struct Layout Layout;
typedef struct Monitor Monitor;
typedef struct Keyword Keyword;
typedef struct Command Command;
typedef struct Callback Callback;
typedef struct Workspace Workspace;
typedef struct WsDefault WsDefault;
typedef struct ClientState ClientState;

typedef xcb_get_geometry_reply_t Geometry;
typedef xcb_get_window_attributes_reply_t WindowAttr;


/* structs */
struct Desk {
	int x, y, w, h;
	Desk *next;
	Monitor *mon;
	xcb_window_t win;
};

struct Rule {
	int x, y, w, h, bw;
	int xgrav, ygrav;
	int ws, floating, sticky, focus;
	char *class, *inst, *title, *mon;
	Callback *cb;
	regex_t classreg, instreg, titlereg;
	Rule *next;
};

struct Panel {
	int x, y, w, h;
	int strut_l, strut_r, strut_t, strut_b;
	Panel *next;
	Monitor *mon;
	xcb_window_t win;
};

struct Client {
	int x, y, w, h, bw, hoff, depth;
	int old_x, old_y, old_w, old_h, old_bw;
	int max_w, max_h, min_w, min_h;
	int base_w, base_h, increment_w, increment_h;
	float min_aspect, max_aspect;

	int sticky, fixed, floating, fullscreen; /* TODO: turn toggles into bit fields */
	int fakefull, urgent, noinput, noborder, oldstate;

	/* ClientState *s, *o; */

	Client *trans, *next, *snext;
	Workspace *ws;
	Callback *cb;
	xcb_window_t win;
};

struct Layout {
	char *name;
	int (*fn)(Workspace *);
};

struct Map {
	xcb_window_t win;
	Map *next;
};

struct Monitor {
	char name[NAME_MAX];
	int num;
	xcb_randr_output_t id;
	int x, y, w, h;
	int wx, wy, ww, wh;
	int connected;
	Monitor *next;
	Workspace *ws;
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
	void (*fn)(Client *, int);
};

struct Workspace {
	int num;
	char name[NAME_MAX];
	int nmaster, nstack, gappx, defgap;
	int padr, padl, padt, padb;
	float msplit;
	float ssplit;
	Layout *layout;
	Monitor *mon;
	Workspace *next;
	Client *sel, *stack, *clients, *hidden;
};

struct WsDefault {
	int nmaster, nstack, gappx;
	int padl, padr, padt, padb;
	float msplit;
	float ssplit;
	Layout *layout;
};

struct ClientState {
	unsigned int fakefull:1;
	unsigned int fixed:1;
	unsigned int floating:1;
	unsigned int fullscreen:1;
	unsigned int noborder:1;
	unsigned int noinput:1;
	unsigned int sticky:1;
	unsigned int urgent:1;
	unsigned int :24;
};


/* function prototypes */
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
static void cmdnmaster(char **);
static void cmdnstack(char **);
static void cmdpad(char **);
static void cmdprint(char **);
static void cmdresize(char **);
static void cmdrule(char **);
static void cmdsend(int);
static void cmdset(char **);
static void cmdmsplit(char **);
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
static void eventignore(uint8_t);
static void eventloop(void);
static void eventrandr(xcb_randr_screen_change_notify_event_t *);
static void execcfg(void);
static void focus(Client *);
static void freeclient(Client *, int);
static void freedesk(Desk *, int);
static void freemap(Map *);
static void freepanel(Panel *, int);
static void freerule(Rule *r);
static void freewin(xcb_window_t, int);
static void freewm(void);
static void freews(Workspace *);
static void grabbuttons(Client *, int);
static void gravitate(Client *, int, int, int);
static int iferr(int, char *, xcb_generic_error_t *);
static void initclient(xcb_window_t, Geometry *);
static void initdesk(xcb_window_t, Geometry *);
static void initmap(xcb_window_t win);
static void initmon(int, char *, xcb_randr_output_t, int, int, int, int);
static void initpanel(xcb_window_t, Geometry *);
static int initrulereg(Rule *, Rule *);
static Rule *initrule(Rule *);
static void initscan(void);
static void initsock(int);
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
static Monitor *nextmon(Monitor *m);
static Client *nextt(Client *);
static void offsetfloat(Client *, int, int *, int *, int *, int *);
static char **parsebool(char **, int *);
static void parsecmd(char *);
static int parseclient(char **, Client **);
static char **parsecolour(char **, unsigned int *);
static char **parsefloat(char **, float *, int *);
static char **parsegeom(char **, char, int *, int *, int *);
static char **parseint(char **, int *, int *, int);
static char **parseintclamp(char **, int *, int *, int, int);
static int parseopt(char **, char **);
static int parsetoken(char **, char *, size_t);
static Client *prevc(Client *);
static size_t strlcat(char *, const char *, size_t);
static size_t strlcpy(char *, const char *, size_t);
static void printerror(xcb_generic_error_t *);
static int querypointer(int *, int *);
static void refresh(void);
static void relocate(Workspace *, Monitor *);
static void resize(Client *, int, int, int, int, int);
static void resizehint(Client *, int, int, int, int, int, int, int);
static void restack(Workspace *);
static int rulecmp(Rule *, char *, char *, char *);
static void sendconfigure(Client *);
static void sendevent(xcb_window_t, const char *, unsigned int);
static int sendwmproto(Client *, int);
static void setclientgeom(Client *, int, int, int, int);
static void setclientws(Client *, int);
static void setfullscreen(Client *, int);
static void setinputfocus(Client *);
static void setnetwsnames(void);
static void setstackmode(xcb_window_t, unsigned int);
static void setsticky(Client *, int);
static void seturgent(Client *, int);
static void setwmwinstate(xcb_window_t, unsigned int);
static void showhide(Client *);
static void sighandle(int);
static void sizehints(Client *, int);
static void subscribe(xcb_window_t, unsigned int);
static int tile(Workspace *);
static void unfocus(Client *, int);
static void ungrabpointer(void);
static void updclientlist(void);
static void updnumws(int);
static int updrandr(void);
static void updscreen(int, int, int, int);
static void updstruts(Panel *, int);
static void updviewports(void);
static void updworkspaces(int);
static void usage(int, char);
static void usenetcurdesktop(void);
static WindowAttr *winattr(xcb_window_t);
static int winclassprop(xcb_window_t, char *, char *, size_t, size_t);
static Geometry *wingeom(xcb_window_t);
static void winhints(Client *);
static int winmotifhints(xcb_window_t, int *);
static int winprop(xcb_window_t, xcb_atom_t, xcb_atom_t *);
static int wintextprop(xcb_window_t, xcb_atom_t, char *, size_t);
static Client *wintoclient(xcb_window_t);
static Desk *wintodesk(xcb_window_t);
static Panel *wintopanel(xcb_window_t);
static xcb_window_t wintrans(xcb_window_t);
static void wintype(Client *);
static int writecmd(int, char *[]);


/* config header relies on the functions being prototyped */
#include "yaxwm.h"


extern char **environ;
static char *argv0;
static char *sock;
static FILE *cmdresp;
static int scr_h;
static int scr_w;
static int sockfd;
static int running;
static int restart;
static int randrbase;
static int cmdusemon;
static int needsrefresh;
static unsigned int lockmask = 0;
static unsigned int dborder[LEN(border)];
static const char *ebadarg = "invalid argument for";
static const char *enoargs = "command requires additional arguments but none were given";

static Map *maps;
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
	char *end;
	int i, x, y;
	Monitor *p;
	Client *c = NULL;
	xcb_window_t sel;
	xcb_void_cookie_t ck;
	unsigned int m = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT;

	argv0 = argv[0];
	sockfd = 0;
	running = 1;
	restart = 0;
	cmdusemon = 0;
	randrbase = -1;
	needsrefresh = 1;

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
	ck = xcb_change_window_attributes_checked(con, root, XCB_CW_EVENT_MASK, &m);
	iferr(1, "is another window manager running?", xcb_request_check(con, ck));

	initwm();
	initsock(0);
	initscan();
	execcfg();

	if (winprop(root, netatom[Active], &sel) && (c = wintoclient(sel))) {
		focus(c);
		xcb_warp_pointer(con, root, root, 0, 0, 0, 0, c->x + (c->w / 2), c->y + (c->h / 2));
	} else if (monitors->next && querypointer(&x, &y) && (p = primary)) {
		xcb_warp_pointer(con, root, root, 0, 0, 0, 0, p->x + (p->w / 2), p->y + (p->h / 2));
	}

	eventloop();
	return 0;
}

void adjustfsetting(float f, int relative, float *setting)
{
	float nf;

	if (f == 0.0 || !setws->layout->fn)
		return;
	else if (!relative && !(f -= *setting))
		return;
	else if ((nf = f < 1.0 ? f + *setting : f - 1.0) < 0.05 || nf > 0.95)
		fprintf(cmdresp, "!value argument for split setting exceeds limit: %f", nf);
	else {
		*setting = nf;
		if (setws->clients && setws == setws->mon->ws)
			layoutws(setws);
	}
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
		max /= globalcfg[MinWH];
	if (!relative && !(i -= *setting))
		return;
	if ((n = CLAMP(*setting + i, 0, max)) != *setting)
		*setting = n;
}

void adjustworkspace(char **argv)
{
	int opt, r;
	unsigned int i;
	void (*fn)(int);
	Monitor *m = NULL, *cm;
	Workspace *ws = NULL, *cws, *save;

	fn = cmdview;
	cws = selws;
	cm = cws->sel ? cws->sel->ws->mon : cws->mon;
	if (*argv) {
		for (i = 0; i < LEN(wsmoncmds); i++)
			if (wsmoncmds[i].fn && !strcmp(wsmoncmds[i].name, *argv)) {
				fn = wsmoncmds[i].fn;
				argv++;
				break;
			}
		if (fn != cmdview && (r = parseclient(argv, &cmdclient))) {
			if (r == -1)
				return;
			cws = cmdclient->ws;
			cm = cws->mon;
			argv++;
		}
	}
	if (!*argv)
		goto noargs;
	if ((opt = parseopt(argv, opts)) >= 0) {
		if (opt == Last) {
			if (cmdusemon)
				ws = lastmon && lastmon->connected ? lastmon->ws : cws;
			else
				ws = lastws ? lastws : cws;
		} else if (opt == Next) {
			if (cmdusemon) {
				if (!(m = nextmon(cm)))
					m = nextmon(monitors);
				ws = m->ws;
			} else {
				ws = cws->next ? cws->next : workspaces;
			}
		} else if (cmdusemon && opt == Prev) {
			FIND_PREVMON(m, cm, monitors);
			ws = m->ws;
		} else if (opt == Prev) {
			FIND_PREV(ws, cws, workspaces);
		} else {
			r = 0;
			save = cws;
			while (!ws && r < globalcfg[NumWs]) {
				if (opt == NextNE) {
					if (cmdusemon) {
						if (!(m = nextmon(cm)))
							m = nextmon(monitors);
						ws = m->ws;
					} else
						ws = cws->next ? cws->next : workspaces;
				} else if (cmdusemon) {
					FIND_PREVMON(m, cm, monitors);
					ws = m->ws;
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
	} else {
		parseintclamp(argv, &opt, NULL, 1, globalcfg[NumWs]);
		if (!cmdusemon)
			ws = itows(opt - 1);
		else
			ws = (m = itomon(opt - 1)) && m->connected ? m->ws : cws;
	}

	if (ws)
		fn(ws->num);
	else
		fprintf(cmdresp, "!unable to locate %s", cmdusemon ? "monitor" : "workspace");
	return;

noargs:
	fprintf(cmdresp, "!%s %s", cmdusemon ? "mon" : "ws", enoargs);
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

int applysizehints(Client *c, int *x, int *y, int *w, int *h, int bw, int usermotion, int mouse)
{
	int baseismin;
	Monitor *m = c->ws->mon;

	*w = MAX(*w, MIN(globalcfg[MinWH], c->min_w));
	*h = MAX(*h, MIN(globalcfg[MinWH], c->min_h));
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
		*x = CLAMP(*x, (*w + (2 * bw) - globalcfg[MinXY]) * -1, scr_w - globalcfg[MinXY]);
		*y = CLAMP(*y, (*h + (2 * bw) - globalcfg[MinXY]) * -1, scr_h - globalcfg[MinXY]);
	} else {
		*x = CLAMP(*x, m->wx, m->wx + m->ww - *w + (2 * bw));
		*y = CLAMP(*y, m->wy, m->wy + m->wh - *h + (2 * bw));
	}
	if (FLOATING(c) || globalcfg[SizeHints]) {
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
	DBG("applysizehints: 0x%08x - %d,%d @ %dx%d -> %d,%d @ %dx%d - usermotion: %d, mouse: %d",
			c->win, c->x, c->y, c->w, c->h, *x, *y, *w, *h, usermotion, mouse);
	return *x != c->x || *y != c->y || *w != c->w || *h != c->h || bw != c->bw;
}

void applyrule(Client *c, Rule *wr)
{
	int num;
	Monitor *m;
	Rule *r = wr;
	xcb_atom_t ws;
	int focus = 0, focusmon = 0, decorate = 1;
	char title[NAME_MAX], class[NAME_MAX], inst[NAME_MAX];

	DBG("applyrule: 0x%08x", c->win);
	title[0] = class[0] = inst[0] = '\0';
	if (!wintextprop(c->win, netatom[Name], title, sizeof(title))
			&& !wintextprop(c->win, XCB_ATOM_WM_NAME, title, sizeof(title)))
		title[0] = '\0';
	if (!winclassprop(c->win, class, inst, sizeof(class), sizeof(inst)))
		inst[0] = class[0] = '\0';
	if ((c->floating = c->trans != NULL))
		ws = c->trans->ws->num;
	else if (!winprop(c->win, netatom[WmDesktop], &ws) || ws > 99)
		ws = selws->num;

	if (title[0] != '\0' || class[0] != '\0' || inst[0] != '\0') {
		if (r) {
			if (!rulecmp(r, title, class, inst))
				r = NULL;
		} else for (r = rules; r; r = r->next) {
			if (rulecmp(r, title, class, inst))
				break;
		}
		if (r) {
			DBG("applyrule: matched: %s, %s, %s", r->class, r->inst, r->title);
			c->cb = r->cb;
			focus = r->focus;
			c->sticky = r->sticky;
			c->x = r->x != -1 ? r->x : c->x;
			c->y = r->y != -1 ? r->y : c->y;
			c->w = r->w != -1 ? r->w : c->w;
			c->h = r->h != -1 ? r->h : c->h;
			c->bw = r->bw != -1 ? r->bw : c->bw;
			if (!c->trans) {
				c->floating = r->floating;
				if ((focusmon = r->mon != NULL)) {
					if ((num = strtol(r->mon, NULL, 0)) > 0 && (m = itomon(num))) {
						ws = m->ws->num;
					} else for (m = monitors; m; m = m->next)
						if (!strcmp(r->mon, m->name)) {
							ws = m->ws->num;
							break;
						}
				} else if (r->ws && r->ws <= globalcfg[NumWs])
					ws = r->ws - 1;
			}
		}
	}

	if ((c->bw == 0 && border[Width]) || (winmotifhints(c->win, &decorate) && decorate == 0)) {
		c->bw = 0;
		c->noborder = 1;
	}
	if (c->sticky && !c->floating)
		c->floating = 1;
	if (ws + 1 > (unsigned int)globalcfg[NumWs] && ws <= 99)
		updnumws(ws + 1);
	setclientws(c, ws);
	if (focus && c->ws != selws) {
		cmdusemon = focusmon;
		cmdview(c->ws->num);
	}
	if (r)
		gravitate(c, r->xgrav, r->ygrav, 1);
	DBG("applyrule: ws: %d, mon: %s, float: %d, stick: %d, focus: %d, x: %d, y: %d, w: %d,"
			" h: %d, bw: %d, cb: %s", c->ws->num, c->ws->mon->name, c->floating,
			c->sticky, focus, c->x, c->y, c->w, c->h, c->bw, c->cb ? c->cb->name : "(null)");
}

void applywsdefaults(void)
{
	Workspace *ws;

	FOR_EACH(ws, workspaces) {
		ws->gappx = wsdef.gappx;
		ws->defgap = wsdef.gappx;
		ws->layout = wsdef.layout;
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

int assignws(int needed)
{
	int n = 0;
	Monitor *m;

	for (n = 0, m = nextmon(monitors); m; m = nextmon(m->next), n++)
		;
	if (n < 1) {
		warnx("no connected monitors");
		return 0;
	} else if (n > 99 || needed > 99) {
		warnx("attempting to allocate too many workspaces: max 99");
		return 0;
	} else while (n > globalcfg[NumWs] || needed > globalcfg[NumWs]) {
		initws(globalcfg[NumWs]);
		globalcfg[NumWs]++;
	}
	return 1;
}

void changews(Workspace *ws, int allowswap, int allowwarp)
{
	Monitor *m, *oldmon;
	int diffmon = allowwarp && selws->mon != ws->mon;

	if (!ws || !nextmon(monitors))
		return;
	DBG("changews: %d:%s -> %d:%s - allowswap: %d - warp: %d", selws->num,
			selws->mon->name, ws->num, ws->mon->name, allowswap, diffmon);
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
		updviewports();
		relocate(ws, oldmon);
		relocate(lastws, selmon);
	}
	selws = ws;
	selmon = ws->mon;
	selws->mon->ws = ws;
	if (!allowswap && diffmon) {
		if (selws->sel)
			xcb_warp_pointer(con, root, root, 0, 0, 0, 0, ws->sel->x + (ws->sel->w / 2),
					ws->sel->y + (ws->sel->h / 2));
		else
			xcb_warp_pointer(con, root, root, 0, 0, 0, 0, ws->mon->x + (ws->mon->w / 2),
					ws->mon->y + (ws->mon->h / 2));
	}
	PROP_REPLACE(root, netatom[CurDesktop], XCB_ATOM_CARDINAL, 32, 1, &ws->num);
}

void cmdborder(char **argv)
{
	Client *c;
	Workspace *ws;
	int incol = 0, start = 0;
	int i, old, bw, ow, rel, outer;
	unsigned int focus, unfocus, urgent;
	unsigned int ofocus, ounfocus, ourgent;

	bw = border[Width];
	ow = border[Outer];
	focus = border[Focus];
	urgent = border[Urgent];
	unfocus = border[Unfocus];
	ofocus = border[OFocus];
	ourgent = border[OUrgent];
	ounfocus = border[OUnfocus];
	while (*argv) {
		if ((outer = !strcmp("outer", *argv) || !strcmp("outer_width", *argv)) || !strcmp(*argv, "width")) {
			incol = 0;
			argv++;
			if (!*argv)
				fprintf(cmdresp, "!border %s %s", *(argv - 1), enoargs);
			else if (!strcmp(*argv, "reset")) {
				if (outer)
					ow = dborder[Outer];
				else
					bw = dborder[Width];
			} else {
				argv = parseint(argv, &i, &rel, 1);
				if (outer)
					adjustsetting(i, rel, &ow, selws->gappx + bw, 1);
				else
					adjustsetting(i, rel, &bw, selws->gappx, 1);
			}
		} else if (incol || (start = !strcmp(*argv, "colour") || !strcmp(*argv, "color"))) {
			if (!incol) {
				incol = 1;
				argv++;
			}
			if (!strcmp(*argv, "focus"))
				argv = parsecolour(argv + 1, &focus);
			else if (!strcmp(*argv, "urgent"))
				argv = parsecolour(argv + 1, &urgent);
			else if (!strcmp(*argv, "unfocus"))
				argv = parsecolour(argv + 1, &unfocus);
			else if (!strcmp(*argv, "outer_focus"))
				argv = parsecolour(argv + 1, &ofocus);
			else if (!strcmp(*argv, "outer_urgent"))
				argv = parsecolour(argv + 1, &ourgent);
			else if (!strcmp(*argv, "outer_unfocus"))
				argv = parsecolour(argv + 1, &ounfocus);
			else if (!strcmp(*argv, "reset")) {
				focus = dborder[Focus];
				urgent = dborder[Urgent];
				unfocus = dborder[Unfocus];
				ofocus = dborder[OFocus];
				ourgent = dborder[OUrgent];
				ounfocus = dborder[OUnfocus];
				incol = 0;
			} else if (start) {
				fprintf(cmdresp, "!%s border colour: %s", ebadarg, *argv);
				break;
			} else {
				incol = 0;
				start = 0;
				continue; /* maybe more args after so don't increment argv */
			}
			start = 0;
		} else {
			fprintf(cmdresp, "!%s border: %s", ebadarg, *argv);
			break;
		}
		if (*argv)
			argv++;
	}

	if ((unsigned int)bw == border[Width] && (unsigned int)ow == border[Outer]
			&& focus == border[Focus] && unfocus == border[Unfocus] && urgent == border[Urgent]
			&& ofocus == border[OFocus] && ounfocus == border[OUnfocus] && ourgent == border[OUrgent])
		return;
	old = border[Width];
	border[Width] = bw;
	border[Focus] = focus;
	border[Unfocus] = unfocus;
	border[Urgent] = urgent;
	border[OFocus] = ofocus;
	border[OUnfocus] = ounfocus;
	border[OUrgent] = ourgent;
	if (bw - ow < 1) {
		if ((unsigned int)ow != border[Outer])
			fprintf(cmdresp, "!border outer exceeds limit: %d - maximum: %d", ow, bw - 1);
	} else {
		border[Outer] = ow;
	}
	FOR_CLIENTS(c, ws) {
		if (!c->noborder) {
			if (c->bw == old)
				c->bw = bw;
			drawborder(c, c == selws->sel);
			needsrefresh++;
		}
	}
}

void cmdcycle(char **argv)
{
	Client *c, *first;

	if (!(c = cmdclient) || FLOATING(c) || FULLSCREEN(c) || !c->ws->layout->fn)
		return;
	first = nextt(selws->clients);
	if (c == first && !nextt(c->next))
		return;
	if (!(c = nextt(selws->sel->next)))
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
	c->fakefull = !c->fakefull;
	if (c->fullscreen) {
		c->bw = c->fakefull ? c->old_bw : 0;
		if (!c->fakefull)
			resize(c, c->ws->mon->x, c->ws->mon->y, c->ws->mon->w, c->ws->mon->h, c->bw);
		layoutws(c->ws);
	}
	(void)(argv);
}

void cmdfloat(char **argv)
{
	Client *c;

	if (!(c = cmdclient) || FULLSCREEN(c) || c->sticky || !c->ws->layout->fn)
		return;
	if ((c->floating = !c->floating || c->fixed)) {
		c->x = c->old_x;
		c->y = c->old_y;
		c->w = c->old_w;
		c->h = c->old_h;
		if (c->x + c->y <= c->ws->mon->wx)
			offsetfloat(c, 6, &c->x, &c->y, &c->w, &c->h);
		resizehint(c, c->x, c->y, c->w, c->h, c->bw, 0, 1);
	} else {
		c->old_x = c->x, c->old_y = c->y, c->old_w = c->w, c->old_h = c->h;
	}
	needsrefresh++;
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
			fprintf(cmdresp, "!%s focus: %s", ebadarg, *argv);
		return;
	}
	if (opt == Last)
		focus(cmdclient->snext);
	else
		movefocus(opt == -1 ? i : opt == Next ? 1 : -1);
}

void cmdfollow(int num)
{
	if (!cmdclient || num == cmdclient->ws->num || !itows(num))
		return;
	unfocus(cmdclient, 1);
	setclientws(cmdclient, num);
	cmdview(num);
}

void cmdfull(char **argv)
{
	if (!cmdclient)
		return;
	setfullscreen(cmdclient, !cmdclient->fullscreen);
	(void)(argv);
}

void cmdgappx(char **argv)
{
	int i = INT_MAX, ng, rel;

	if (!strcmp(*argv, "width"))
		argv++;
	ng = setws->gappx;

	if (!*argv)
		fprintf(cmdresp, "!gap %s", enoargs);
	else if (!strcmp(*argv, "reset"))
		ng = setws->defgap;
	else {
		parseint(argv, &i, &rel, 1);
		adjustsetting(i, rel, &ng, border[Width], 1);
	}
	if (ng != setws->gappx) {
		setws->gappx = ng;
		if (setws->clients && setws == setws->mon->ws)
			needsrefresh++;
	}
}

void cmdkill(char **argv)
{
	if (!cmdclient)
		return;
	if (!sendwmproto(cmdclient, Delete)) {
		xcb_grab_server(con);
		xcb_set_close_down_mode(con, XCB_CLOSE_DOWN_DESTROY_ALL);
		xcb_kill_client(con, cmdclient->win);
		xcb_flush(con);
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
			if (&layouts[i] != setws->layout) {
				setws->layout = &layouts[i];
				if (setws->clients && setws == setws->mon->ws)
					needsrefresh++;
			}
			return;
		}
	fprintf(cmdresp, "!invalid layout name: %s", *argv);
}

void cmdmon(char **argv)
{
	if (!monitors || !nextmon(monitors))
		return;
	adjustworkspace(argv);
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
				fprintf(cmdresp, "!invalid modifier: %s", *argv);
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
				fprintf(cmdresp, "!invalid button: %s", *argv);
				break;
			}
		} else {
			fprintf(cmdresp, "!%s mouse: %s", ebadarg, *argv);
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
	adjustsetting(i, rel, &setws->nmaster, 0, 0);
	if (setws->clients && setws == setws->mon->ws)
		layoutws(setws);
}

void cmdnstack(char **argv)
{
	int i = INT_MAX, rel = 1;

	parseint(argv, &i, &rel, 1);
	adjustsetting(i, rel, &setws->nstack, 0, 0);
	if (setws->clients && setws == setws->mon->ws)
		layoutws(setws);
}

void cmdpad(char **argv)
{
	int i = -1, rel;

	while (*argv) {
		i = INT_MAX;
		if (!strcmp("l", *argv) || !strcmp("left", *argv)) {
			argv = parseintclamp(argv + 1, &i, &rel, setws->padl * -1, setws->mon->w / 3);
			if (i != INT_MAX)
				setws->padl = CLAMP(rel ? setws->padl + i : i, 0, setws->mon->w / 3);
		} else if (!strcmp("r", *argv) || !strcmp("right", *argv)) {
			argv = parseintclamp(argv + 1, &i, &rel, setws->padr * -1, setws->mon->w / 3);
			if (i != INT_MAX)
				setws->padr = CLAMP(rel ? setws->padr + i : i, 0, setws->mon->w / 3);
		} else if (!strcmp("t", *argv) || !strcmp("top", *argv)) {
			argv = parseintclamp(argv + 1, &i, &rel, setws->padt * -1, setws->mon->h / 3);
			if (i != INT_MAX)
				setws->padt = CLAMP(rel ? setws->padt + i : i, 0, setws->mon->h / 3);
		} else if (!strcmp("b", *argv) || !strcmp("bottom", *argv)) {
			argv = parseintclamp(argv + 1, &i, &rel, setws->padb * -1, setws->mon->h / 3);
			if (i != INT_MAX)
				setws->padb = CLAMP(rel ? setws->padb + i : i, 0, setws->mon->h / 3);
		} else {
			fprintf(cmdresp, "!%s pad: %s", ebadarg, *argv);
			break;
		}
		if (*argv)
			argv++;
	}
	if (setws->clients && setws == setws->mon->ws)
		layoutws(setws);
}

void cmdprint(char **argv)
{
	Rule *r;
	Client *c;
	Monitor *m;
	char *end;
	unsigned int ui;
	Workspace *ws = selws, *w;
	int i = 0, incol = 0, start = 0, outer;

	if (!strcmp("numws", *argv))
		fprintf(cmdresp, "%d", globalcfg[NumWs]);
	else if (!strcmp("smart_border", *argv))
		fprintf(cmdresp, "%d", globalcfg[SmartBorder]);
	else if (!strcmp("smart_gap", *argv))
		fprintf(cmdresp, "%d", globalcfg[SmartGap]);
	else if (!strcmp("focus_urgent", *argv))
		fprintf(cmdresp, "%d", globalcfg[FocusUrgent]);
	else if (!strcmp("tile_hints", *argv))
		fprintf(cmdresp, "%d", globalcfg[SizeHints]);
	else if (!strcmp("focus_mouse", *argv))
		fprintf(cmdresp, "%d", globalcfg[FocusMouse]);
	else if (!strcmp("win_minxy", *argv))
		fprintf(cmdresp, "%d", globalcfg[MinXY]);
	else if (!strcmp("win_minwh", *argv))
		fprintf(cmdresp, "%d", globalcfg[MinWH]);
	else if (!strcmp("mon", *argv))
		fprintf(cmdresp, "%d:%s %d,%d %dx%d", selws->mon->num + 1, selws->mon->name,
				selws->mon->x, selws->mon->y, selws->mon->w, selws->mon->h);
	else if (!strcmp("rule", *argv)) {
		FOR_EACH(r, rules) {
			fprintf(cmdresp, "class: %s, inst: %s, title: %s, ws: %d, mon: %s, "
					"float: %d, stick: %d, focus: %d, callback: %s, "
					"position: %d,%d %dx%d, xgrav: %s, ygrav: %s%s",
					r->class, r->inst, r->title, r->ws, r->mon, r->floating,
					r->sticky, r->focus, r->cb ? r->cb->name : "(null)", r->x, r->y, r->w, r->h,
					gravities[r->xgrav], gravities[r->ygrav], r->next ? "\n" : "");
		}
	} else if (!strcmp("current", *argv)) {
		argv++;
		if (!strcmp("win", *argv))
			fprintf(cmdresp, "#%08x", selws->sel ? selws->sel->win : 0);
		else if (!strcmp("ws", *argv))
			fprintf(cmdresp, "%d:%s", selws->num + 1, selws->name);
		else if (!strcmp("mon", *argv))
			fprintf(cmdresp, "%d:%s %d,%d %dx%d", selws->mon->num + 1, selws->mon->name,
					selws->mon->x, selws->mon->y, selws->mon->w, selws->mon->h);
		else {
			fprintf(cmdresp, "!unknown current setting: %s", *argv);
			return;
		}
	} else if (!strcmp("all", *argv)) {
		argv++;
		if (!strcmp("win", *argv)) {
			FOR_CLIENTS(c, w)
				fprintf(cmdresp, "#%08x %d:%d%s", c->win, w->num + 1,
						w->mon->num + 1, w->next ? "\n" : "");
		} else if (!strcmp("ws", *argv)) {
			FOR_EACH(w, workspaces)
				fprintf(cmdresp, "%d:%s %d:%s%s", w->num + 1, w->name, w->mon->num + 1,
						w->mon->name, w->next ? "\n" : "");
		} else if (!strcmp("mon", *argv)) {
			FOR_EACH(m, monitors)
				fprintf(cmdresp, "%d:%s %d:%s %d,%d %dx%d%s", m->num + 1, m->name, m->ws->num + 1,
						m->ws->name, m->x, m->y, m->w, m->h, m->next ? "\n" : "");
		} else {
			fprintf(cmdresp, "!unknown all setting: %s", *argv);
			return;
		}
	} else if (!strcmp("win", *argv)) {
		argv++;
		if (!*argv) {
			if (selws->sel)
				fprintf(cmdresp, "#%08x", selws->sel->win);
			return;
		} else if ((ui = strtoul(**argv == '#' ? *argv + 1 : *argv, &end, 16)) > 0 && *end == '\0') {
			if (!(c = wintoclient(ui))) {
				fprintf(cmdresp, "!invalid window id: %s", *argv);
				return;
			}
			argv++;
		} else if (!(c = selws->sel)) {
			fprintf(cmdresp, "!no active window on current workspace");
			return;
		}
		while (*argv) {
			if (!strcmp("float", *argv))
				fprintf(cmdresp, "%d", c->floating);
			else if (!strcmp("stick", *argv))
				fprintf(cmdresp, "%d", c->sticky);
			else if (!strcmp("ws", *argv))
				fprintf(cmdresp, "%d:%s", c->ws->num + 1, c->ws->name);
			else if (!strcmp("mon", *argv))
				fprintf(cmdresp, "%d:%s", c->ws->mon->num + 1, c->ws->mon->name);
			else if (!strcmp("geom", *argv))
				fprintf(cmdresp, "%d,%d %dx%d", c->x, c->y, W(c), H(c));
			else if (!strcmp("full", *argv))
				fprintf(cmdresp, "%d", c->fullscreen);
			else if (!strcmp("fakefull", *argv))
				fprintf(cmdresp, "%d", c->fakefull);
			else {
				fprintf(cmdresp, "!unknown window setting: %s", *argv);
				return;
			}
			argv++;
		}
	} else if (!strcmp("border", *argv)) {
		while (*argv) {
			if ((outer = !strcmp("outer", *argv) || !strcmp("outer_width", *argv))
					|| !strcmp(*argv, "width"))
			{
				incol = 0;
				fprintf(cmdresp, "%d", outer ? border[Outer] : border[Width]);
			} else if (incol || (start = !strcmp(*argv, "colour") || !strcmp(*argv, "color"))) {
				if (!incol) {
					incol = 1;
					argv++;
				}
				if (!strcmp(*argv, "focus")) {
					fprintf(cmdresp, "#%08x", border[Focus]);
				} else if (!strcmp(*argv, "urgent")) {
					fprintf(cmdresp, "#%08x", border[Urgent]);
				} else if (!strcmp(*argv, "unfocus")) {
					fprintf(cmdresp, "#%08x", border[Unfocus]);
				} else if (!strcmp(*argv, "outer_focus")) {
					fprintf(cmdresp, "#%08x", border[OFocus]);
				} else if (!strcmp(*argv, "outer_urgent")) {
					fprintf(cmdresp, "#%08x", border[OUrgent]);
				} else if (!strcmp(*argv, "outer_unfocus")) {
					fprintf(cmdresp, "#%08x", border[OUnfocus]);
				} else if (start) {
					fprintf(cmdresp, "!unknown border colour setting: %s", *argv);
					return;
				} else {
					incol = 0;
					start = 0;
					continue; /* maybe more args after so don't increment argv */
				}
				start = 0;
			} else {
				fprintf(cmdresp, "!unknown border setting: %s", *argv);
				return;
			}
			argv++;
		}
	} else while (*argv) {
		if (!strcmp("ws", *argv)) {
			argv++;
			if (!*argv) {
				fprintf(cmdresp, "%d:%s", selws->num + 1, selws->name);
				return;
			} else if ((i = strtol(*argv, &end, 0)) <= 0 || *end != '\0' || !(ws = itows(i - 1))) {
				fprintf(cmdresp, "!invalid workspace index: %s", *argv);
				return;
			}
		} else if (!strcmp("gap", *argv))
			fprintf(cmdresp, "%d", ws->gappx);
		else if (!strcmp("master", *argv))
			fprintf(cmdresp, "%d", ws->nmaster);
		else if (!strcmp("stack", *argv))
			fprintf(cmdresp, "%d", ws->nstack);
		else if (!strcmp("msplit", *argv))
			fprintf(cmdresp, "%0.2f", ws->msplit);
		else if (!strcmp("ssplit", *argv))
			fprintf(cmdresp, "%0.2f", ws->ssplit);
		else if (!strcmp("layout", *argv))
			fprintf(cmdresp, "%s", ws->layout->name);
		else if (!strcmp("pad", *argv))
			fprintf(cmdresp, "%d %d %d %d", ws->padl, ws->padr, ws->padt, ws->padb);
		else {
			fprintf(cmdresp, "!unknown print setting: %s", *argv);
			return;
		}
		argv++;
	}
}

void cmdresize(char **argv)
{
	Client *c, *t;
	int i, ohoff;
	float f, *sf;
	int xgrav = None, ygrav = None;
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
			fprintf(cmdresp, "!%s resize: %s", ebadarg, *argv);
			break;
		}
		if (*argv)
			argv++;
	}
	if (x == scr_w && y == scr_h && w == 0 && h == 0 && xgrav == None && ygrav == None && bw == -1)
		return;
	if (FLOATING(c)) {
		x = x == scr_w || xgrav != None ? c->x : (relx ? c->x + x : x);
		y = y == scr_h || ygrav != None ? c->y : (rely ? c->y + y : y);
		w = w == 0 ? c->w : (relw ? c->w + w : w);
		h = h == 0 ? c->h : (relh ? c->h + h : h);
		bw = bw == -1 ? c->bw : (relbw ? c->bw + bw : bw);
		resizehint(c, x, y, w, h, bw, 1, 0);
		gravitate(c, xgrav, ygrav, 1);
	} else if (c->ws->layout->fn == tile) {
		if (bw != -1) {
			c->bw = relbw ? c->bw + bw : bw;
			if (y == scr_h && !w && !h)
				drawborder(c, c == selws->sel);
		}
		if (y != scr_h)
			movestack(y > 0 || ygrav == Bottom ? 1 : -1);
		if (w) {
			sf = &c->ws->ssplit;
			for (i = 0, t = nextt(c->ws->clients); t; t = nextt(t->next), i++)
				if (t == c) {
					if (c->ws->nmaster && i < c->ws->nmaster + c->ws->nstack)
						sf = &c->ws->msplit;
					f = relw ? ((c->ws->mon->ww * *sf) + w) / c->ws->mon->ww : w / c->ws->mon->ww;
					if (f < 0.05 || f > 0.95) {
						fprintf(cmdresp, "!window width exceeded limit: %f - f: %f",
								c->ws->mon->ww * f, f);
					} else {
						*sf = f;
						if (!h)
							needsrefresh++;
					}
					break;
				}
		}
		if (h) {
			ohoff = c->hoff;
			c->hoff = relh ? c->hoff + h : h;
			if (layoutws(c->ws) == -1) {
				fprintf(cmdresp, "!height adjustment for window exceeded limit: %d", c->hoff);
				c->hoff = ohoff;
			}
		}
	} else {
		fprintf(cmdresp, "!unable to resize windows in %s layout", c->ws->layout->name);
		return;
	}
	eventignore(XCB_ENTER_NOTIFY);
}

void cmdrule(char **argv)
{
	Client *c;
	Rule *wr, *nr;
	Workspace *ws;
	unsigned int i, delete, apply = 0;
	Rule r = {
		.x = -1, .y = -1, .w = -1, .h = -1, .ws = -1, .bw = -1,
		.focus = 0, .sticky = 0, .floating = 0, .xgrav = None, .ygrav = None,
		.cb = NULL, .mon = NULL, .inst = NULL, .class = NULL, .title = NULL,
	};

	if ((apply = !strcmp("apply", *argv))) {
		argv++;
		if (!strcmp("all", *argv)) {
			FOR_CLIENTS(c, ws) {
				applyrule(c, NULL);
				if (c->cb)
					c->cb->fn(c, 0);
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
		else if (!strcmp(*argv, "bw"))
			argv = parseintclamp(argv + 1, &r.bw, NULL, 0, scr_h / 6);
		else if (!strcmp(*argv, "float"))
			argv = parsebool(argv + 1, &r.floating);
		else if (!strcmp(*argv, "stick"))
			argv = parsebool(argv + 1, &r.sticky);
		else if (!strcmp(*argv, "focus"))
			argv = parsebool(argv + 1, &r.focus);
		else {
			fprintf(cmdresp, "!%s rule: %s", ebadarg, *argv);
			break;
		}
		if (*argv)
			argv++;
	}

	if ((r.class || r.inst || r.title) && (r.cb || r.ws || r.mon || r.focus || r.floating
				|| r.sticky || r.x != -1 || r.y != -1 || r.w != -1 || r.h != -1
				|| r.bw != -1 || r.xgrav != None || r.ygrav != None))
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
					applyrule(c, nr);
					if (c->cb)
						c->cb->fn(c, 0);
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
	setclientws(cmdclient, num);
	needsrefresh++;
}

void cmdset(char **argv)
{
	Workspace *ws;
	unsigned int j;
	int i, names = 0;

	setws = selws;
	if (!*argv) {
		fprintf(cmdresp, "!set %s", enoargs);
		return;
	}
	while (*argv) {
		i = -1;
		if (!strcmp("ws", *argv)) {
			if (!strcmp("default", *(argv + 1))) {
				cmdwsdef(argv + 2);
				goto finish;
			}
			argv = parseintclamp(argv + 1, &i, NULL, 1, globalcfg[NumWs]);
			if (!(ws = itows(i - 1))) {
				fprintf(cmdresp, "!invalid workspace index: %s", *argv);
				goto finish;
			}
			setws = ws;
		} else if (!strcmp("numws", *argv)) {
			argv = parseintclamp(argv + 1, &i, NULL, 1, 99);
			if (i > globalcfg[NumWs])
				updnumws(i);
		} else if (!strcmp("name", *argv)) {
			argv++;
			if (!*argv) {
				fprintf(cmdresp, "!set ws name %s", enoargs);
				goto finish;
			}
			strlcpy(setws->name, *argv, sizeof(setws->name));
			names = 1;
		} else if (!strcmp("smart_border", *argv)) {
			argv = parsebool(argv + 1, &globalcfg[SmartBorder]);
		} else if (!strcmp("smart_gap", *argv)) {
			argv = parsebool(argv + 1, &globalcfg[SmartGap]);
		} else if (!strcmp("focus_urgent", *argv)) {
			argv = parsebool(argv + 1, &globalcfg[FocusUrgent]);
		} else if (!strcmp("tile_hints", *argv)) {
			argv = parsebool(argv + 1, &globalcfg[SizeHints]);
		} else if (!strcmp("focus_mouse", *argv)) {
			argv = parsebool(argv + 1, &globalcfg[FocusMouse]);
		} else if (!strcmp("win_minxy", *argv)) {
			argv = parseintclamp(argv + 1, &globalcfg[MinXY], NULL, 10, 1000);
		} else if (!strcmp("win_minwh", *argv)) {
			argv = parseintclamp(argv + 1, &globalcfg[MinWH], NULL, 10, 1000);
		} else {
			for (j = 0; j < LEN(setcmds); j++)
				if (!strcmp(setcmds[j].name, *argv)) {
					setcmds[j].fn(argv + 1);
					goto finish;
				}
			fprintf(cmdresp, "!%s set: %s", ebadarg, *argv);
		}
		if (*argv)
			argv++;
	}
finish:
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
	if (!cmdclient || FULLSCREEN(cmdclient))
		return;
	setsticky(cmdclient, !cmdclient->sticky);
	(void)(argv);
}

void cmdswap(char **argv)
{
	static Client *last = NULL;
	Client *c, *old, *cur = NULL, *prev = NULL;

	if (!(c = cmdclient) || FULLSCREEN(c) || FLOATING(c))
		return;
	if (c == nextt(c->ws->clients)) {
		if ((cur = prevc(last)))
			prev = nextt(cur->next);
		if (!prev || prev != last) {
			last = NULL;
			if (!c || !(c = nextt(c->next)))
				return;
		} else
			c = prev;
	}
	if (c != (old = nextt(c->ws->clients)) && !cur)
		cur = prevc(c);
	detach(c, 1);
	if (c != old && cur) {
		last = old;
		if (old && cur != old) {
			detach(old, 0);
			old->next = cur->next;
			cur->next = old;
		}
	}
	needsrefresh++;
	(void)(argv);
}

void cmdwin(char **argv)
{
	char *end;
	unsigned int i;

	cmdclient = selws->sel;

	if (*argv && (*argv[0] == '#' || (*argv[0] == '0' && *argv[0] == 'x'))
			&& (i = strtoul(**argv == '#' ? *argv + 1 : *argv, &end, 16)) > 0 && *end == '\0')
	{
		if (!(cmdclient = wintoclient(i))) {
			fprintf(cmdresp, "!invalid window id: %s", *argv);
			return;
		}
		argv++;
	}
	if (!*argv) {
		fprintf(cmdresp, "!win %s", enoargs);
		return;
	}
	for (i = 0; i < LEN(wincmds); i++)
		if (!strcmp(wincmds[i].name, *argv)) {
			wincmds[i].fn(argv + 1);
			return;
		}
	fprintf(cmdresp, "!%s win: %s", ebadarg, *argv);
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
		fprintf(cmdresp, "!%s wm: %s", ebadarg, *argv);
}

void cmdws(char **argv)
{
	if (!workspaces || !workspaces->next)
		return;
	adjustworkspace(argv);
}

void cmdwsdef(char **argv)
{
	unsigned int ui;
	int inpad = 0, start = 0, apply = 0;

	while (*argv) {
		if (!strcmp(*argv, "apply")) {
			apply = 1;
		} else if (!strcmp(*argv, "layout")) {
			argv++;
			inpad = 0;
			for (ui = 0; ui < LEN(layouts); ui++)
				if (!strcmp(layouts[ui].name, *argv)) {
					wsdef.layout = &layouts[ui];
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
				fprintf(cmdresp, "!%s pad: %s", ebadarg, *argv);
				return;
			} else {
				inpad = 0;
				start = 0;
				continue; /* maybe more args after pad so don't increment argv */
			}
			start = 0;
		} else {
			fprintf(cmdresp, "!%s workspace default: %s", ebadarg, *argv);
			return;
		}
		if (*argv)
			argv++;
	}
	DBG("cmdwsdef: layout: %s, nmaster: %d, nstack: %d, gap: %d, msplit: %f,"
			" ssplit: %f, padl: %d, padr: %d, padt: %d, padb: %d", wsdef.layout->name,
			wsdef.nmaster, wsdef.nstack, wsdef.gappx, wsdef.msplit, wsdef.ssplit,
			wsdef.padl, wsdef.padr, wsdef.padt, wsdef.padb);
	if (apply)
		applywsdefaults();
}

void cmdview(int num)
{
	Workspace *ws;

	DBG("cmdview: workspace number %d", num);
	if (num == selws->num || !(ws = itows(num)))
		return;
	if (!cmdusemon)
		changews(ws, 1, 0);
	else
		changews(ws, 0, 1);
	needsrefresh++;
}

void drawborder(Client *c, int focused)
{
	int o, b;
	unsigned int in, out;
	xcb_gcontext_t gc;
	xcb_pixmap_t pmap;

	if (c->noborder || !c->bw)
		return;
	b = c->bw;
	o = border[Outer];
	in = border[focused ? Focus : (c->urgent ? Urgent : Unfocus)];
	out = border[focused ? OFocus : (c->urgent ? OUrgent : OUnfocus)];
	unsigned int frame[] = { c->bw, c->bw, c->bw, c->bw };
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

	PROP_REPLACE(c->win, netatom[FrameExtents], XCB_ATOM_CARDINAL, 32, 4, frame);
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

void clientcfgreq(Client *c, xcb_configure_request_event_t *e)
{
	Monitor *m;
	Geometry *g;

	if (!(g = wingeom(c->win)))
		return;
	c->depth = g->depth;
	free(g);
	if (e->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH)
		c->bw = e->border_width;
	else if (FLOATING(c)) {
		if (e->x == W(c) * -2) {
			DBG("clientcfgreq: ignoring CONFIGURE_REQUEST - 0x%08x is hidden", c->win);
			return;
		}
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
		if ((c->x + c->w) > m->wx + m->ww && c->floating)
			c->x = m->wx + ((m->ww - W(c)) / 2);
		if ((c->y + c->h) > m->wy + m->wh && c->floating)
			c->y = m->wy + ((m->wh - H(c)) / 2);
		if ((e->value_mask & XYMASK) && !(e->value_mask & WHMASK))
			sendconfigure(c);
		if (c->ws == m->ws)
			MOVERESIZE(c->win, c->x, c->y, c->w, c->h, c->bw);
	} else {
		sendconfigure(c);
	}
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

int disablemon(Monitor *m, xcb_randr_get_output_info_reply_t *o, int changed)
{
	xcb_generic_error_t *e;
	xcb_randr_set_crtc_config_cookie_t sc;
	xcb_randr_set_crtc_config_reply_t *sr;

	changed = m->connected ? 1 : changed;
	if (m->connected) {
		DBG("disablemon: %s inactive or disconnected - disabling", m->name);
		/* we need to disconnect the crtc, disabling it's mode and output
		 * otherwise we're unable to update the root screen size later */
		sc = xcb_randr_set_crtc_config(con, o->crtc, XCB_CURRENT_TIME,
				XCB_CURRENT_TIME, 0, 0, XCB_NONE, XCB_RANDR_ROTATION_ROTATE_0, 0, NULL);
		if (!(sr = xcb_randr_set_crtc_config_reply(con, sc, &e))
				|| sr->status != XCB_RANDR_SET_CONFIG_SUCCESS)
			iferr(0, "unable to set crtc config", e);
		m->connected = 0;
		m->num = -1;
		free(sr);
	}
	return changed;
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

	switch (EVTYPE(ev)) {
	case XCB_FOCUS_IN:
	{
		xcb_focus_in_event_t *e = (xcb_focus_in_event_t *)ev;

		if (e->mode == XCB_NOTIFY_MODE_GRAB
				|| e->mode == XCB_NOTIFY_MODE_UNGRAB
				|| e->detail == XCB_NOTIFY_DETAIL_POINTER
				|| e->detail == XCB_NOTIFY_DETAIL_POINTER_ROOT
				|| e->detail == XCB_NOTIFY_DETAIL_NONE)
			return;
		DBG("eventhandle: FOCUS_IN - 0x%08x", e->event);
		if (selws->sel && e->event != selws->sel->win)
			setinputfocus(selws->sel);
		return;
	}
	case XCB_CONFIGURE_NOTIFY:
	{
		xcb_configure_notify_event_t *e = (xcb_configure_notify_event_t *)ev;

		if (e->window == root && (scr_h != e->height || scr_w != e->width)) {
			DBG("eventhandle: CONFIGURE_NOTIFY - screen size changed - %dx%d", e->width, e->height);
			scr_w = e->width;
			scr_h = e->height;
			if (monitors && randrbase < 0) {
				monitors->w = monitors->ww = scr_w;
				monitors->h = monitors->wh = scr_h;
				updworkspaces(globalcfg[NumWs]);
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
		DBG("eventhandle: ENTER_NOTIFY - 0x%08x", e->event);
		ws = selws;
		if ((c = wintoclient(e->event)))
			ws = c->ws;
		else if ((m = coordtomon(e->root_x, e->root_y)))
			ws = m->ws;
		if (ws && ws != selws)
			changews(ws, 0, 0);
		if (c && c != selws->sel && globalcfg[FocusMouse])
			focus(c);
		return;
	}
	case XCB_BUTTON_PRESS:
	{
		xcb_button_press_event_t *e = (xcb_button_press_event_t *)ev;

		if (!(c = wintoclient(e->event)))
			return;
		DBG("eventhandle: BUTTON_PRESS - 0x%08x", e->event);
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

		if (e->event != root)
			return;
		if ((m = coordtomon(e->root_x, e->root_y)) && m->ws != selws) {
			DBG("eventhandle: MOTION_NOTIFY - updating active monitor - 0x%08x", e->event);
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
		mapwin(e->window, g, wa, 1);
		free(wa);
		free(g);
		return;
	}
	case XCB_UNMAP_NOTIFY:
	{
		xcb_unmap_notify_event_t *e = (xcb_unmap_notify_event_t *)ev;

		if (EVSENT(ev))
			setwmwinstate(e->window, XCB_ICCCM_WM_STATE_WITHDRAWN);
		else
			freewin(e->window, 0);
		return;
	}
	case XCB_CLIENT_MESSAGE:
	{
		xcb_client_message_event_t *e = (xcb_client_message_event_t *)ev;
		unsigned int *d = e->data.data32;

		cmdusemon = 0;
		if (e->type == netatom[CurDesktop]) {
			DBG("CLIENT_MESSAGE: %s - data: %d", netatoms[CurDesktop], d[0]);
			cmdview(d[0]);
		} else if (e->type == netatom[Close]) {
			DBG("CLIENT_MESSAGE: %s - 0x%08x", netatoms[Close], e->window);
			freewin(e->window, 1);
		} else if ((c = wintoclient(e->window))) {
			if (e->type == netatom[WmDesktop]) {
				DBG("CLIENT_MESSAGE: %s - 0x%08x - data: %d", netatoms[WmDesktop], c->win, d[0]);
				if (!itows(d[0])) {
					DBG("CLIENT_MESSAGE: not a valid workspace: %d", d[0]);
					return;
				}
				setclientws(c, d[0]);
				needsrefresh++;
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
				if (globalcfg[FocusUrgent]) {
					if (c->ws != selws) {
						unfocus(selws->sel, 1);
						cmdview(c->ws->num);
					}
					focus(c);
				} else
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
			DBG("eventhandle: PROPERTY_NOTIFY - _NET_WM_STRUT_PARTIAL - 0x%08x", e->window);
			updstruts(p, 1);
			FOR_EACH(m, monitors)
				if (p->mon == m && m->connected && m->ws)
					layoutws(m->ws);
		} else if (e->state != XCB_PROPERTY_DELETE && (c = wintoclient(e->window))) {
			switch (e->atom) {
			case XCB_ATOM_WM_TRANSIENT_FOR:
				DBG("eventhandle: PROPERTY_NOTIFY - WM_TRANSIENT_FOR - 0x%08x", e->window);
				if (!c->floating && (trans = wintrans(c->win))
						&& (c->floating = (c->trans = wintoclient(trans)) != NULL))
					layoutws(c->ws);
				break;
			case XCB_ATOM_WM_NORMAL_HINTS:
				DBG("eventhandle: PROPERTY_NOTIFY - WM_NORMAL_HINTS - 0x%08x", e->window);
				sizehints(c, 0);
				break;
			case XCB_ATOM_WM_HINTS:
				DBG("eventhandle: PROPERTY_NOTIFY - WM_HINTS - 0x%08x", e->window);
				winhints(c);
				break;
			}
			if (e->atom == netatom[WindowType]) {
				DBG("eventhandle: PROPERTY_NOTIFY - _NET_WM_WINDOW_TYPE - 0x%08x", e->window);
				wintype(c);
			}
		}
		return;
	}
	default:
	{
		xcb_generic_error_t *e = (xcb_generic_error_t *)ev;

		if (ev->response_type && randrbase != -1
				&& ev->response_type == randrbase + XCB_RANDR_SCREEN_CHANGE_NOTIFY)
		{
			eventrandr((xcb_randr_screen_change_notify_event_t *)ev);
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
	xcb_generic_event_t *ev = NULL;

	xcb_flush(con);
	while (running && (ev = xcb_poll_for_event(con))) {
		if (EVTYPE(ev) != type)
			eventhandle(ev);
		free(ev);
	}
}

void eventloop(void)
{
	ssize_t n;
	fd_set read_fds;
	char buf[PIPE_BUF];
	struct timeval tv;
	xcb_generic_event_t *ev;
	int confd, nfds, cmdfd;

	confd = xcb_get_file_descriptor(con);
	nfds = MAX(confd, sockfd) + 1;
	while (running) {
		if (xcb_connection_has_error(con))
			break;
		tv.tv_sec = 2;
		tv.tv_usec = 0;
		xcb_flush(con);
		refresh();
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
					if ((cmdresp = fdopen(cmdfd, "w")) != NULL)
						parsecmd(buf);
					else {
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
}

void eventrandr(xcb_randr_screen_change_notify_event_t *re)
{
	int upd = 1;
	xcb_generic_error_t *e;
	xcb_dpms_info_reply_t *info;
	xcb_dpms_capable_reply_t *dpms;

	if (re->root != root)
		return;
	DBG("eventrandr: SCREEN_CHANGE_NOTIFY -- width: %d, height: %d", re->width, re->height);
	if (!(dpms = xcb_dpms_capable_reply(con, xcb_dpms_capable(con), &e)) || !dpms->capable)
		iferr(0, "unable to get dpms capable reply or server not dpms capable", e);
	else if (!(info = xcb_dpms_info_reply(con, xcb_dpms_info(con), &e)))
		iferr(0, "unable to get dpms info reply", e);
	else {
		DBG("eventrandr: DPMS - state: %d, power_level: %d", info->state, info->power_level);
		upd = !info->state || !info->power_level;
		free(info);
	}
	free(dpms);
	if (upd && updrandr())
		updworkspaces(globalcfg[NumWs]);
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

void focus(Client *c)
{
	if (!c || c->ws != c->ws->mon->ws)
		c = selws->stack;
	if (selws->sel && selws->sel != c)
		unfocus(selws->sel, 0);
	if (c) {
		if (c->urgent)
			seturgent(c, 0);
		detachstack(c);
		attachstack(c);
		grabbuttons(c, 1);
		drawborder(c, 1);
		setinputfocus(c);
	} else {
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
		xcb_delete_property(con, root, netatom[Active]);
	}
	selws->sel = c;
}

void freeclient(Client *c, int destroyed)
{
	if (!c)
		return;
	if (c->cb && running)
		c->cb->fn(c, 1);
	detach(c, 0);
	detachstack(c);
	if (!destroyed) {
		xcb_grab_server(con);
		xcb_configure_window(con, c->win, XCB_CONFIG_WINDOW_BORDER_WIDTH, &c->old_bw);
		xcb_ungrab_button(con, XCB_BUTTON_INDEX_ANY, c->win, XCB_MOD_MASK_ANY);
		setwmwinstate(c->win, XCB_ICCCM_WM_STATE_WITHDRAWN);
		xcb_flush(con);
		xcb_ungrab_server(con);
	}
	if (running) { /* spec says these should be removed on withdraw but not on wm shutdown */
		xcb_delete_property(con, c->win, netatom[State]);
		xcb_delete_property(con, c->win, netatom[WmDesktop]);
	}
	free(c);
	updclientlist();
	needsrefresh++;
}

void freedesk(Desk *d, int destroyed)
{
	Desk **dd = &desks;

	DETACH(d, dd);
	if (!destroyed) {
		xcb_grab_server(con);
		setwmwinstate(d->win, XCB_ICCCM_WM_STATE_WITHDRAWN);
		xcb_flush(con);
		xcb_ungrab_server(con);
	}
	free(d);
	updclientlist();
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

	DETACH(p, pp);
	if (!destroyed) {
		xcb_grab_server(con);
		setwmwinstate(p->win, XCB_ICCCM_WM_STATE_WITHDRAWN);
		xcb_flush(con);
		xcb_ungrab_server(con);
	}
	updstruts(p, 0);
	free(p);
	updclientlist();
	needsrefresh++;
}

void freerule(Rule *r)
{
	Rule **rr = &rules;

	DETACH(r, rr);
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

void freewin(xcb_window_t win, int destroyed)
{
	Desk *d;
	Panel *p;
	Client *c;

	if ((c = wintoclient(win)))
		freeclient(c, destroyed);
	else if ((p = wintopanel(win)))
		freepanel(p, destroyed);
	else if ((d = wintodesk(win)))
		freedesk(d, destroyed);
}

void freemap(Map *n)
{
	Map **nn;

	nn = &maps;
	DETACH(n, nn);
	free(n);
}

void freewm(void)
{
	unsigned int i;
	char fdstr[64];

	while (panels)
		freepanel(panels, 0);
	while (desks)
		freedesk(desks, 0);
	while (workspaces) {
		while (workspaces->stack)
			freeclient(workspaces->stack, 0); // NOLINT
		freews(workspaces);
	}
	while (monitors)
		freemon(monitors);
	while (rules)
		freerule(rules);

	if (con) {
		for (i = 0; i < LEN(cursors); i++)
			xcb_free_cursor(con, cursor[i]);
		xcb_key_symbols_free(keysyms);
		xcb_destroy_window(con, wmcheck);
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT,
				XCB_INPUT_FOCUS_POINTER_ROOT, XCB_CURRENT_TIME);
		if (!restart)
			xcb_delete_property(con, root, netatom[Active]);
		xcb_flush(con);
		xcb_disconnect(con);
	}

	if (restart) {
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
	unsigned int i, j, mods[] = { 0, XCB_MOD_MASK_LOCK, 0, XCB_MOD_MASK_LOCK };

	lockmask = 0;
	if ((m = xcb_get_modifier_mapping_reply(con, xcb_get_modifier_mapping(con), &e))) {
		if ((t = xcb_key_symbols_get_keycode(keysyms, nlock))
				&& (kc = xcb_get_modifier_mapping_keycodes(m)))
			for (i = 0; i < 8; i++)
				for (j = 0; j < m->keycodes_per_modifier; j++)
					if (kc[i * m->keycodes_per_modifier + j] == *t)
						lockmask = (1 << i);
	} else {
		iferr(0, "unable to get modifier mapping for numlock", e);
	}
	free(t);
	free(m);

	mods[2] |= lockmask, mods[3] |= lockmask;
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
			| XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_POINTER_MOTION,
			XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, root, cursor, XCB_CURRENT_TIME);
	if ((ptr = xcb_grab_pointer_reply(con, pc, &e)))
		r = ptr->status == XCB_GRAB_STATUS_SUCCESS;
	else
		iferr(0, "unable to grab pointer", e);
	free(ptr);
	return r;
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
		mx = c->trans->x;
		my = c->trans->y;
		mw = c->trans->w;
		mh = c->trans->h;
	} else {
		gap = matchgap ? c->ws->gappx : 0;
		mx = c->ws->mon->wx;
		my = c->ws->mon->wy;
		mw = c->ws->mon->ww;
		mh = c->ws->mon->wh;
	}
	switch (horz) {
	case Left: x = mx + gap; break;
	case Right: x = mx + mw - W(c) - gap; break;
	case Center: x = (mx + mw - W(c)) / 2; break;
	}
	switch (vert) {
	case Top: y = my + gap; break;
	case Bottom: y = my + mh - H(c) - gap; break;
	case Center: y = (my + mh - H(c)) / 2; break;
	}
	if (c->ws == c->ws->mon->ws)
		resizehint(c, x, y, c->w, c->h, c->bw, 0, 0);
}

Monitor *idtomon(xcb_randr_output_t id)
{
	Monitor *m;

	FOR_EACH(m, monitors)
		if (m->id == id)
			return m;
	return m;
}

int iferr(int lvl, char *msg, xcb_generic_error_t *e)
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

void initclient(xcb_window_t win, Geometry *g)
{
	Client *c;

	DBG("initclient: managing new window - 0x%08x", win);
	c = ecalloc(1, sizeof(Client));
	c->win = win;
	c->x = c->old_x = g->x;
	c->y = c->old_y = g->y;
	c->w = c->old_w = g->width;
	c->h = c->old_h = g->height;
	c->depth = g->depth;
	c->old_bw = g->border_width;
	c->bw = border[Width];
	c->trans = wintoclient(wintrans(c->win));
	applyrule(c, NULL);
	c->w = CLAMP(c->w, globalcfg[MinWH], c->ws->mon->ww);
	c->h = CLAMP(c->h, globalcfg[MinWH], c->ws->mon->wh);
	if (c->trans) {
		c->x = c->trans->x + ((W(c->trans) - W(c)) / 2);
		c->y = c->trans->y + ((H(c->trans) - H(c)) / 2);
	}
	xcb_configure_window(con, c->win, XCB_CONFIG_WINDOW_BORDER_WIDTH, &c->bw);
	sendconfigure(c);
	wintype(c);
	sizehints(c, 1);
	winhints(c);
	subscribe(c->win, XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_FOCUS_CHANGE
			| XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY);
	drawborder(c, 0);
	grabbuttons(c, 0);
	if (FLOATING(c) || (c->floating = c->oldstate = c->trans || c->fixed)) {
		c->x = CLAMP(c->x, c->ws->mon->wx, c->ws->mon->wx + c->ws->mon->ww - W(c));
		c->y = CLAMP(c->y, c->ws->mon->wy, c->ws->mon->wy + c->ws->mon->wh - H(c));
		if (c->x + c->y <= c->ws->mon->wx)
			offsetfloat(c, 6, &c->x, &c->y, &c->w, &c->h);
		setstackmode(c->win, XCB_STACK_MODE_ABOVE);
	}
	PROP_APPEND(root, netatom[ClientList], XCB_ATOM_WINDOW, 32, 1, &c->win);
	MOVE(c->win, c->x + 2 * scr_w, c->y);
	setwmwinstate(c->win, XCB_ICCCM_WM_STATE_NORMAL);
	if (c->ws == c->ws->mon->ws)
		unfocus(selws->sel, 0);
	c->ws->sel = c;
	if (c->cb)
		c->cb->fn(c, 0);
	DBG("initclient: mapped - 0x%08x - workspace %d - %d,%d @ %dx%d - floating: %d",
			c->win, c->ws->num, c->x, c->y, c->w, c->h, FLOATING(c));
}

void initdesk(xcb_window_t win, Geometry *g)
{
	Desk *d;

	DBG("initdesktopwin: 0x%08x - %d,%d @ %dx%d", win, g->x, g->y, g->width, g->height);
	d = ecalloc(1, sizeof(Desk));
	d->win = win;
	if (!(d->mon = coordtomon(g->x, g->y)))
		d->mon = selws->mon;
	d->next = desks;
	desks = d;
	subscribe(d->win, XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY);
	d->x = d->mon->wx;
	d->y = d->mon->wy;
	d->w = d->mon->ww;
	d->h = d->mon->wh;
	MOVERESIZE(d->win, d->x, d->y, d->w, d->h, 0);
	setwmwinstate(d->win, XCB_ICCCM_WM_STATE_NORMAL);
	setstackmode(d->win, XCB_STACK_MODE_BELOW);
	PROP_APPEND(root, netatom[ClientList], XCB_ATOM_WINDOW, 32, 1, &d->win);
}

void initmon(int num, char *name, xcb_randr_output_t id, int x, int y, int w, int h)
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
	m->connected = 1;
	strlcpy(m->name, name, sizeof(m->name));
	FIND_TAIL(tail, monitors);
	if (tail)
		tail->next = m;
	else
		monitors = m;
}

void initmap(xcb_window_t win)
{
	Map *n;

	n = ecalloc(1, sizeof(Map));
	n->win = win;
	n->next = maps;
	maps = n;
	needsrefresh++;
}

void initpanel(xcb_window_t win, Geometry *g)
{
	int *s;
	Panel *p;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t rc;
	xcb_get_property_reply_t *r = NULL;

	DBG("initpanel: 0x%08x - %d,%d @ %dx%d", win, g->x, g->y, g->width, g->height);
	rc = xcb_get_property(con, 0, win, netatom[StrutPartial], XCB_ATOM_CARDINAL, 0, 4);
	p = ecalloc(1, sizeof(Panel));
	p->win = win;
	p->x = g->x;
	p->y = g->y;
	p->w = g->width;
	p->h = g->height;
	if (!(p->mon = coordtomon(p->x, p->y)))
		p->mon = selws->mon;
	if (!(r = xcb_get_property_reply(con, rc, &e)) || r->type == XCB_NONE) {
		iferr(0, "unable to get _NET_WM_STRUT_PARTIAL reply from window", e);
		rc = xcb_get_property(con, 0, p->win, netatom[Strut], XCB_ATOM_CARDINAL, 0, 4);
		if (!(r = xcb_get_property_reply(con, rc, &e)))
			iferr(0, "unable to get _NET_WM_STRUT reply from window", e);
	}
	if (r && r->value_len && (s = xcb_get_property_value(r))) {
		DBG("initpanel: 0x%08x - struts: %d, %d, %d, %d", p->win, s[0], s[1], s[2], s[3]);
		p->strut_l = s[0];
		p->strut_r = s[1];
		p->strut_t = s[2];
		p->strut_b = s[3];
		updstruts(p, 1);
	}
	free(r);
	p->next = panels;
	panels = p;
	subscribe(p->win, XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY);
	setwmwinstate(p->win, XCB_ICCCM_WM_STATE_NORMAL);
	PROP_APPEND(root, netatom[ClientList], XCB_ATOM_WINDOW, 32, 1, &p->win);
	DBG("initpanel: mapped - 0x%08x - mon: %s - %d,%d @ %dx%d",
			p->win, p->mon->name, p->x, p->y, p->w, p->h);
}

int initrandr(void)
{
	int extbase;
	const xcb_query_extension_reply_t *ext;

	if (!(ext = xcb_get_extension_data(con, &xcb_randr_id)) || !ext->present) {
		warnx("unable to get randr extension data");
		return -1;
	}
	updrandr();
	extbase = ext->first_event;
	xcb_randr_select_input(con, root,
			XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE | XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE
			| XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE | XCB_RANDR_NOTIFY_MASK_OUTPUT_PROPERTY);
	return extbase;
}

Rule *initrule(Rule *r)
{
	size_t len;
	Rule *wr;

	wr = ecalloc(1, sizeof(Rule));
	wr->x = r->x;
	wr->y = r->y;
	wr->w = r->w;
	wr->h = r->h;
	wr->ws = r->ws;
	wr->cb = r->cb;
	wr->bw = r->bw;
	wr->xgrav = r->xgrav;
	wr->ygrav = r->ygrav;
	wr->focus = r->focus;
	wr->sticky = r->sticky;
	wr->floating = r->floating;
	if (r->mon) {
		len = strlen(r->mon) + 1;
		wr->mon = ecalloc(1, len);
		strlcpy(wr->mon, r->mon, len);
	}
	if (initrulereg(wr, r)) {
		wr->next = rules;
		rules = wr;
		DBG("initrule: class: %s, inst: %s, title: %s, mon: %s, ws: %d, "
				"floating: %d, sticky: %d, focus: %d, position: %d,%d @ %d x %d",
				wr->class, wr->inst, wr->title, wr->mon, wr->ws, wr->floating,
				wr->sticky, wr->focus, wr->x, wr->y, wr->w, wr->h);
	} else {
		free(wr->mon);
		free(wr);
		wr = NULL;
	}
	return wr;
}

int initrulereg(Rule *r, Rule *wr)
{
	int i;
	size_t len;
	char buf[NAME_MAX], *e;

	if (wr->class) {
		len = strlen(wr->class) + 1;
		r->class = ecalloc(1, len);
		strlcpy(r->class, wr->class, len);
		if ((i = regcomp(&(r->classreg), r->class, REG_NOSUB | REG_EXTENDED | REG_ICASE))) {
			regerror(i, &(r->classreg), buf, sizeof(buf));
			e = "class";
			goto error;
		}
	}
	if (wr->inst) {
		len = strlen(wr->inst) + 1;
		r->inst = ecalloc(1, len);
		strlcpy(r->inst, wr->inst, len);
		if ((i = regcomp(&(r->instreg), r->inst, REG_NOSUB | REG_EXTENDED | REG_ICASE))) {
			regerror(i, &(r->instreg), buf, sizeof(buf));
			e = "instance";
			goto error;
		}
	}
	if (wr->title) {
		len = strlen(wr->title) + 1;
		r->title = ecalloc(1, len);
		strlcpy(r->title, wr->title, len);
		if ((i = regcomp(&(r->titlereg), r->title, REG_NOSUB | REG_EXTENDED | REG_ICASE))) {
			regerror(i, &(r->titlereg), buf, sizeof(buf));
			e = "title";
			goto error;
		}
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
	unsigned int i;
	Geometry **g;
	WindowAttr **wa;
	xcb_window_t *w;
	xcb_atom_t state;
	xcb_generic_error_t *e;
	xcb_query_tree_reply_t *rt;
	uint8_t icon = XCB_ICCCM_WM_STATE_ICONIC;

	if (!(rt = xcb_query_tree_reply(con, xcb_query_tree(con, root), &e))) {
		iferr(1, "unable to query tree from root window", e);
	} else if (rt->children_len) {
		w = xcb_query_tree_children(rt);
		g = ecalloc(rt->children_len, sizeof(Geometry *));
		wa = ecalloc(rt->children_len, sizeof(WindowAttr *));
		for (i = 0; i < rt->children_len; i++) {
			g[i] = NULL;
			if (!(wa[i] = winattr(w[i])) || !(g[i] = wingeom(w[i]))
					|| !(wa[i]->map_state == XCB_MAP_STATE_VIEWABLE
						|| (winprop(w[i], wmatom[WMState], &state) && state == icon)))
			{
				w[i] = XCB_WINDOW_NONE;
			} else if (!wintrans(w[i])) {
				mapwin(w[i], g[i], wa[i], 0);
				w[i] = XCB_WINDOW_NONE;
			}
		}
		for (i = 0; i < rt->children_len; i++) {
			if (w[i] != XCB_WINDOW_NONE)
				mapwin(w[i], g[i], wa[i], 0);
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
	char *hostname = NULL;
	int display = 0, screen = 0;
	static struct sockaddr_un sockaddr;

	if (sockfd > 0)
		return; /* nothing to do */

	if (!(sock = getenv("YAXWM_SOCK"))) {
		sock = ecalloc(1, 2048);
		if (xcb_parse_display(NULL, &hostname, &display, &screen) != 0)
			snprintf(sock, 2048, "/tmp/yaxwm_%s_%i_%i.socket", hostname, display, screen); // NOLINT
		free(hostname);
		if (!send && setenv("YAXWM_SOCK", sock, 0) < 0)
			err(1, "unable to export socket path to environment: %s", sock);
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
	}
}

void initwm(void)
{
	unsigned int i;
	xcb_void_cookie_t c;
	xcb_cursor_context_t *ctx;
	struct sigaction sa;
	int sigs[] = { SIGTERM, SIGINT, SIGHUP, SIGCHLD };

	sa.sa_handler = sighandle;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	for (i = 0; i < LEN(sigs); i++)
		if (sigaction(sigs[i], &sa, NULL) < 0)
			err(1, "unable to setup handler for signal: %d", sigs[i]);
	if ((randrbase = initrandr()) < 0 || !monitors)
		initmon(0, "default", 0, 0, 0, scr_w, scr_h);
	updnumws(globalcfg[NumWs]);
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
	usenetcurdesktop();
	setnetwsnames();
	c = xcb_change_window_attributes_checked(con, root, XCB_CW_EVENT_MASK | XCB_CW_CURSOR,
			(unsigned int []){ XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT
			| XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_BUTTON_PRESS
			| XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_ENTER_WINDOW
			| XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_STRUCTURE_NOTIFY
			| XCB_EVENT_MASK_PROPERTY_CHANGE, cursor[Normal] });
	iferr(1, "unable to change root window event mask or cursor", xcb_request_check(con, c));

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
	ws->gappx = MAX(0, wsdef.gappx);
	ws->defgap = ws->gappx;
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

int layoutws(Workspace *ws)
{
	int i, ret = 1;

	if (ws) {
		showhide(ws->stack);
		if (ws->layout->fn)
			ret = ws->layout->fn(ws);
		restack(ws);
	} else FOR_EACH(ws, workspaces) {
		showhide(ws->stack);
		if (ws == ws->mon->ws && ws->layout->fn) {
			i = ws->layout->fn(ws);
			ret = ret && ws == selws ? i : ret;
		}
	}
	return ret;
}

void mapwin(xcb_window_t win, Geometry *g, WindowAttr *wa, int check)
{
	xcb_atom_t type;

	if (check && (wintoclient(win) || wintopanel(win) || wintodesk(win)))
		return;
	DBG("mapwin: unmanaged window - 0x%08x", win);
	if (winprop(win, netatom[WindowType], &type)) {
		if (type == netatom[Dock])
			initpanel(win, g);
		else if (type == netatom[Desktop])
			initdesk(win, g);
		else if (type != netatom[Splash] && !wa->override_redirect)
			initclient(win, g);
	} else if (!wa->override_redirect)
		initclient(win, g);
	initmap(win);
}

int mono(Workspace *ws)
{
	int g, b;
	Client *c;

	g = globalcfg[SmartGap] ? 0 : ws->gappx;
	for (c = nextt(ws->clients); c; c = nextt(c->next)) {
		b = globalcfg[SmartBorder] ? 0 : c->bw;
		resizehint(c, ws->mon->wx + ws->padl + g, ws->mon->wy + ws->padt + g,
				ws->mon->ww - ws->padl - ws->padr - (2 * g) - (2 * b),
				ws->mon->wh - ws->padt - ws->padb - (2 * g) - (2 * b),
				globalcfg[SmartBorder] ? 0 : c->bw, 0, 0);
	}
	return 1;
}

void movefocus(int direction)
{
	Client *c;

	if (!selws->sel)
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

	if (!(c = cmdclient) || FLOATING(c) || !nextt(c->ws->clients->next))
		return;
	while (direction) {
		if (direction > 0) {
			detach(c, (t = nextt(c->next)) ? 0 : 1);
			if (t) {
				c->next = t->next;
				t->next = c;
			}
			direction--;
		} else {
			if (c == nextt(c->ws->clients)) {
				detach(c, 0);
				attach(c, 0);
			} else {
				FIND_PREVTILED(t, c, c->ws->clients);
				detach(c, (i = (t == nextt(c->ws->clients)) ? 1 : 0));
				if (!i) {
					c->next = t;
					FIND_PREV(t, c->next, c->ws->clients);
					t->next = c;
				}
			}
			direction++;
		}
	}
	needsrefresh++;
}

void mousemvr(int move)
{
	Client *c;
	Monitor *m;
	xcb_timestamp_t last = 0;
	xcb_motion_notify_event_t *e;
	xcb_generic_event_t *ev = NULL;
	int mx, my, ox, oy, ow, oh, nw, nh, nx, ny, released = 0;

	if (!(c = selws->sel) || FULLSCREEN(c) || (!move && c->fixed) || !querypointer(&mx, &my))
		return;
	ox = nx = c->x;
	oy = ny = c->y;
	ow = nw = c->w;
	oh = nh = c->h;
	if (!grabpointer(cursor[move ? Move : Resize]))
		return;
	xcb_flush(con);
	while (running && !released) {
		while (!(ev = xcb_wait_for_event(con)))
			xcb_flush(con);
		switch (XCB_EVENT_RESPONSE_TYPE(ev)) {
		case XCB_MAP_REQUEST:       /* FALLTHROUGH */
		case XCB_CONFIGURE_REQUEST:
			eventhandle(ev);
			break;
		case XCB_BUTTON_RELEASE:
			released = 1;
			break;
		case XCB_MOTION_NOTIFY:
			e = (xcb_motion_notify_event_t *)ev;
			if ((e->time - last) < (1000 / 60))
				break;
			last = e->time;
			if (move) {
				nx = ox + (e->root_x - mx);
				ny = oy + (e->root_y - my);
			} else {
				nw = ow + (e->root_x - mx);
				nh = oh + (e->root_y - my);
			}
			if ((nw != c->w || nh != c->h || nx != c->x || ny != c->y)) {
				if (!FLOATING(c)) {
					c->floating = 1;
					if (c->max_w)
						c->w = MIN(c->w, c->max_w);
					if (c->max_h)
						c->h = MIN(c->h, c->max_h);
					c->x = CLAMP(c->x, c->ws->mon->wx, c->ws->mon->wx + c->ws->mon->ww - W(c));
					c->y = CLAMP(c->y, c->ws->mon->wy, c->ws->mon->wy + c->ws->mon->wh - H(c));
					resizehint(c, c->x, c->y, c->w, c->h, c->bw, 1, 1);
					layoutws(c->ws);
				}
				if (move && (m = coordtomon(e->root_x, e->root_y)) && m->ws != c->ws) {
					setclientws(c, m->ws->num);
					changews(m->ws, 0, 0);
					focus(c);
				}
				resizehint(c, nx, ny, nw, nh, c->bw, 1, 1);
			}
			break;
		}
		free(ev);
	}
	ungrabpointer();
	if (!move)
		eventignore(XCB_ENTER_NOTIFY);
}

Monitor *nextmon(Monitor *m)
{
	while (m && !m->connected)
		m = m->next;
	return m;
}

Client *nextt(Client *c)
{
	while (c && c->floating)
		c = c->next;
	return c;
}

void offsetfloat(Client *c, int d, int *x, int *y, int *w, int *h)
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
		offset += globalcfg[MinWH];
	else
		offset += (offset * -1) + rand() % 200;
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
		fprintf(cmdresp, "!invalid boolean argument: %s - expected true, false, 1, 0", *argv);
	return argv;
}

int parseclient(char **argv, Client **c)
{
	char *end;
	unsigned int i;

	if (argv && *argv) {
		if ((*argv[0] == '#' || (*argv[0] == '0' && *argv[0] == 'x'))
				&& (i = strtoul(**argv == '#' ? *argv + 1 : *argv, &end, 16)) > 0 && *end == '\0')
		{
			if (!(*c = wintoclient(i))) {
				fprintf(cmdresp, "!invalid window id argument: %s", *argv);
				return -1;
			}
			return 1;
		}
	}
	return 0;
}

void parsecmd(char *buf)
{
	unsigned int i, n = 0, matched = 0;
	char *argv[30], k[NAME_MAX], tok[NAME_MAX], args[30][NAME_MAX];

	DBG("parsecmd: tokenizing input buffer: %s", buf);
	if (parsetoken(&buf, k, sizeof(k))) {
		for (i = 0; i < LEN(keywords); i++)
			if ((matched = !strcmp(keywords[i].name, k))) {
				DBG("parsecmd: matched command keyword: %s", k);
				while (n + 1 < LEN(args) && buf && *buf && parsetoken(&buf, tok, sizeof(tok))) {
					strlcpy(args[n], tok, sizeof(args[n]));
					argv[n] = args[n];
					DBG("parsecmd: parsed token: argv[%d] = %s", n, argv[n]);
					n++;
				}
				argv[n] = NULL;
				if (*argv) {
					cmdusemon = keywords[i].fn == cmdmon;
					keywords[i].fn(argv);
				} else
					fprintf(cmdresp, "!%s %s", k, enoargs);
				break;
			}
		if (!matched)
			fprintf(cmdresp, "!invalid or unknown command: %s", k);
	}
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
			if (len == 6)
				*setting = (argb | 0xff000000);
			else if ((a = ((argb & 0xff000000) >> 24)) && a != 0xff) {
				r = (((argb & 0xff0000) >> 16) * a) / 255;
				g = (((argb & 0xff00) >> 8) * a) / 255;
				b = (((argb & 0xff) >> 0) * a) / 255;
				*setting = (a << 24 | r << 16 | g << 8 | b << 0);
			} else
				*setting = argb;
			return argv;
		}
	}
	fprintf(cmdresp, "!invalid colour argument: %s - expected (#/0x)(AA)RRGGBB", *argv);
	return argv;
}

char **parsefloat(char **argv, float *setting, int *rel)
{
	float f;
	char *end;

	if (!argv || !*argv)
		return argv;
	if ((f = strtof(*argv, &end)) != 0.0 && *end == '\0') {
		if (f < -0.95 || f > 0.95)
			fprintf(cmdresp, "!float argument is out of range: %s - min: -0.95, max: 0.95", *argv);
		else {
			if (rel) /* check if it's a relative number (has a sign) */
				*rel = **argv == '-' || **argv == '+';
			*setting = f;
		}

	} else
		fprintf(cmdresp, "!invalid or incomplete float argument: %s - expected (-/+)0.N", *argv);
	return argv;
}

char **parsegeom(char **argv, char type, int *i, int *rel, int *grav)
{
	enum { None, Left, Right, Center, Top, Bottom };

	if (!argv || !*argv)
		return argv;
	if (!grav)
		argv = parseint(argv, i, rel, type == 'x' || type == 'y' ? 1 : 0);
	else {
		switch (type) {
		case 'x':
			if (grav && !strcmp("center", *argv))
				*grav = Center;
			else if (grav && !strcmp("left", *argv))
				*grav = Left;
			else if (grav && !strcmp("right", *argv))
				*grav = Right;
			else
				argv = parseint(argv, i, rel, 1);
			break;
		case 'y':
			if (grav && !strcmp("center", *argv))
				*grav = Center;
			else if (grav && !strcmp("top", *argv))
				*grav = Top;
			else if (grav && !strcmp("bottom", *argv))
				*grav = Bottom;
			else
				argv = parseint(argv, i, rel, 1);
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
	} else
		fprintf(cmdresp, "!invalid integer argument: %s - expected (-/+)N", *argv);
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
		fprintf(cmdresp, "!integer argument is out of range: %s - min: %d, max: %d",
				*argv, min, max);
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

int parsetoken(char **src, char *dst, size_t size)
{
	size_t n = 0;
	int q, sq = 0;
	char *s, *head, *tail;

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
		tail = strpbrk(*src, " =\t\n");
	}

	s = head;
	while (n + 1 < size && tail ? s < tail : *s) {
		if (q && !sq && *s == '\\' && *(s + 1) == '"') {
			s++;
		} else {
			n++;
			*dst++ = *s++;
		}
	}
	*dst = '\0';
	*src = tail ? ++tail : '\0';

	return n || q;
}

Client *prevc(Client *c)
{
	Client *tmp;

	if (c == selws->clients)
		return NULL;
	for (tmp = selws->clients; tmp && tmp->next != c; tmp = tmp->next)
		;
	return tmp;
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
		*x = p->root_x;
		*y = p->root_y;
		free(p);
		return 1;
	} else
		iferr(0, "unable to query pointer", e);
	return 0;
}

void refresh(void)
{
	Monitor *m;

	if (!needsrefresh)
		return;
	layoutws(NULL);
	while (maps) {
		xcb_map_window(con, maps->win);
		freemap(maps);
	}
	for (m = nextmon(monitors); m; m = nextmon(m->next))
		restack(m->ws);
	focus(NULL);
	eventignore(XCB_ENTER_NOTIFY);
	needsrefresh = 0;
}

void relocate(Workspace *ws, Monitor *old)
{
	Client *c;
	Monitor *m;
	float xoff, yoff;
	float xdiv, ydiv;

	DBG("relocate: moving clients from %s to %s", old->name, ws->mon->name);
	if (!(m = ws->mon) || m == old)
		return;
	FOR_EACH(c, ws->clients)
		if (FLOATING(c)) { /* TODO: simplify this mess */
			if ((xoff = c->x - old->x) && (xdiv = old->w / xoff) != 0.0) {
				if (c->x + W(c) == old->wx + old->ww) /* edge */
					c->x = m->wx + m->ww - W(c);
				else if (c->x + (W(c) / 2) == old->wx + (old->ww / 2)) /* center */
					c->y = (m->wx + m->ww - W(c)) / 2;
				else
					c->x = CLAMP(m->wx + (m->ww / xdiv), m->wx - (W(c) - globalcfg[MinXY]),
							m->wx + m->ww - globalcfg[MinXY]);
			} else
				c->x = CLAMP(c->x, m->wx - (W(c) - globalcfg[MinXY]),
						m->x + m->w - globalcfg[MinXY]);
			if ((yoff = c->y - old->y) && (ydiv = old->h / yoff) != 0.0) {
				if (c->y + H(c) == old->wy + old->wh) /* edge */
					c->y = m->wy + m->wh - H(c);
				else if (c->y + (H(c) / 2) == old->wy + (old->wh / 2)) /* center */
					c->y = (m->wy + m->wh - H(c)) / 2;
				else
					c->y = CLAMP(m->wy + (m->wh / ydiv), m->wy - (H(c) - globalcfg[MinXY]),
							m->wy + m->wh - globalcfg[MinXY]);
			} else
				c->y = CLAMP(c->y, m->wy - (H(c) - globalcfg[MinXY]),
						m->wy + m->wh - globalcfg[MinXY]);
		}
}

void resize(Client *c, int x, int y, int w, int h, int bw)
{
	setclientgeom(c, x, y, w, h);
	MOVERESIZE(c->win, x, y, w, h, bw);
	drawborder(c, c == selws->sel);
	sendconfigure(c);
}

void resizehint(Client *c, int x, int y, int w, int h, int bw, int usermotion, int mouse)
{
	if (applysizehints(c, &x, &y, &w, &h, bw, usermotion, mouse))
		resize(c, x, y, w, h, bw);
}

void restack(Workspace *ws)
{
	Desk *d;
	Panel *p;
	Client *c;

	if (!ws)
		ws = selws;
	if (!ws || !(c = ws->sel))
		return;
	FOR_EACH(p, panels)
		if (p->mon == ws->mon)
			setstackmode(c->win, XCB_STACK_MODE_ABOVE);
	if (FLOATING(c))
		setstackmode(c->win, XCB_STACK_MODE_ABOVE);
	if (ws->layout->fn) {
		FOR_STACK(c, ws->stack)
			if (!c->floating && c->ws == c->ws->mon->ws)
				setstackmode(c->win, XCB_STACK_MODE_BELOW);
	}
	FOR_EACH(d, desks)
		if (d->mon == ws->mon)
			setstackmode(c->win, XCB_STACK_MODE_BELOW);
}

int rulecmp(Rule *r, char *title, char *class, char *inst)
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

	DBG("sendconfigure: sending 0x%08x configure notify event", c->win);
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
	sendevent(c->win, (char *)&ce, XCB_EVENT_MASK_STRUCTURE_NOTIFY);
}

void sendevent(xcb_window_t win, const char *ev, unsigned int mask)
{
	xcb_void_cookie_t vc;

	vc = xcb_send_event_checked(con, 0, win, mask, ev);
	iferr(0, "unable to send configure notify event to window", xcb_request_check(con, vc));
}

int sendwmproto(Client *c, int wmproto)
{
	int n, exists = 0;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t rpc;
	xcb_client_message_event_t cme;
	xcb_icccm_get_wm_protocols_reply_t proto;

	DBG("sendwmproto: checking if 0x%08x supports: %s", c->win, wmatoms[wmproto]);
	rpc = xcb_icccm_get_wm_protocols(con, c->win, wmatom[Protocols]);
	if (xcb_icccm_get_wm_protocols_reply(con, rpc, &proto, &e)) {
		n = proto.atoms_len;
		while (!exists && n--)
			exists = proto.atoms[n] == wmatom[wmproto];
		xcb_icccm_get_wm_protocols_reply_wipe(&proto);
	} else {
		iferr(0, "unable to get requested wm protocol", e);
	}
	if (exists) {
		DBG("sendwmproto: sending %s message to 0x%08x", wmatoms[wmproto], c->win);
		cme.response_type = XCB_CLIENT_MESSAGE;
		cme.window = c->win;
		cme.type = wmatom[Protocols];
		cme.format = 32;
		cme.data.data32[0] = wmatom[wmproto];
		cme.data.data32[1] = XCB_TIME_CURRENT_TIME;
		sendevent(c->win, (char *)&cme, XCB_EVENT_MASK_NO_EVENT);
	}
	return exists;
}

void setclientgeom(Client *c, int x, int y, int w, int h)
{
	DBG("setclientgeom: 0x%08x -> %d,%d @ %d x %d", c->win, x, y, w, h);
	c->old_x = c->x;
	c->old_y = c->y;
	c->old_w = c->w;
	c->old_h = c->h;
	c->x = x;
	c->y = y;
	c->w = w;
	c->h = h;
}

void setclientws(Client *c, int num)
{
	DBG("setclientws: 0x%08x -> %d", c->win, num);
	if (c->ws) {
		DBG("setclientws: detaching from existing workspace: %d", c->ws->num);
		detach(c, 0);
		detachstack(c);
	}
	if (!(c->ws = itows(num))) {
		DBG("setclientws: no matching workspace: %d -- using selws: %d", num, selws->num);
		c->ws = selws;
	}
	PROP_REPLACE(c->win, netatom[WmDesktop], XCB_ATOM_CARDINAL, 32, 1, &c->ws->num);
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
	} else if (!fullscreen && c->fullscreen) {
		PROP_REPLACE(c->win, netatom[State], XCB_ATOM_ATOM, 32, 0, (unsigned char *)0);
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

void setinputfocus(Client *c)
{
	if (!c->noinput) {
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, c->win, XCB_CURRENT_TIME);
		PROP_REPLACE(root, netatom[Active], XCB_ATOM_WINDOW, 32, 1, &c->win);
	}
	sendwmproto(c, TakeFocus);
}

void setsticky(Client *c, int sticky)
{
	unsigned int all = 0xffffffff;

	if (sticky && !c->sticky) {
		cmdfloat(NULL);
		c->sticky = 1;
		PROP_REPLACE(c->win, netatom[WmDesktop], XCB_ATOM_CARDINAL, 32, 1, &all);
	} else if (!sticky && c->sticky) {
		c->sticky = 0;
		PROP_REPLACE(c->win, netatom[WmDesktop], XCB_ATOM_CARDINAL, 32, 1, &c->ws->num);
	}
}

void setstackmode(xcb_window_t win, unsigned int mode)
{
	xcb_configure_window(con, win, XCB_CONFIG_WINDOW_STACK_MODE, &mode);
}

void setwmwinstate(xcb_window_t win, unsigned int state)
{
	unsigned int s[] = { state, XCB_ATOM_NONE };
	PROP_REPLACE(win, wmatom[WMState], wmatom[WMState], 32, 2, s);
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
		for (i = 0; (names[len++] = ws->name[i]); i++);
	PROP_REPLACE(root, netatom[DesktopNames], wmatom[Utf8Str], 8, --len, names);
	free(names);
}

void seturgent(Client *c, int urg)
{
	xcb_generic_error_t *e;
	xcb_icccm_wm_hints_t wmh;
	xcb_get_property_cookie_t pc;

	DBG("seturgent: 0x%08x - urgent: %d", c->win, urg);
	pc = xcb_icccm_get_wm_hints(con, c->win);
	if (c != selws->sel)
		drawborder(c, 0);
	if (xcb_icccm_get_wm_hints_reply(con, pc, &wmh, &e)) {
		wmh.flags = urg ? (wmh.flags | XCB_ICCCM_WM_HINT_X_URGENCY)
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
			if (ISFULL(c))
				resize(c, m->x, m->y, m->w, m->h, 0);
			else
				resize(c, c->x, c->y, c->w, c->h, c->bw);
		}
		showhide(c->snext);
	} else {
		showhide(c->snext);
		if (!c->sticky)
			MOVE(c->win, W(c) * -2, c->y);
		else if (c->ws != selws && m == selws->mon) {
			sel = lastws->sel == c ? c : selws->sel;
			setclientws(c, selws->num);
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
		iferr(0, "unable to get wm normal hints", e);
	}
	c->fixed = (c->max_w && c->max_h && c->max_w == c->min_w && c->max_h == c->min_h);
}

size_t strlcat(char *dst, const char *src, size_t siz)
{
	char *d = dst;
	const char *s = src;
	size_t n = siz;
	size_t dlen;

	/* Find the end of dst and adjust bytes left but don't go past end */
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

	return dlen + (s - src);	/* count does not include NUL */
}

size_t strlcpy(char *dst, const char *src, size_t dsize)
{
	const char *osrc = src;
	size_t nleft = dsize;

	/* Copy as many bytes as will fit. */
	if (nleft != 0) {
		while (--nleft != 0) {
			if ((*dst++ = *src++) == '\0')
				break;
		}
	}

	/* Not enough room in dst, add NUL and traverse rest of src. */
	if (nleft == 0) {
		if (dsize != 0)
			*dst = '\0';		/* NUL-terminate dst */
		while (*src++)
			;
	}

	return (src - osrc - 1);	/* count does not include NUL */
}

void subscribe(xcb_window_t win, unsigned int events)
{
	xcb_change_window_attributes(con, win, XCB_CW_EVENT_MASK, &events);
}

int tiler(Client *c, Client *p, int ww, int wh, int x, int y,
		int w, int h, int bw, int gap, int *newy, int nrem, int avail)
{
	int ret = 1;
	int b = bw ? c->bw : bw;

	DBG("tiler: 0x%08x - %d,%d @ %dx%d - newy: %d, nrem: %d, avail; %d",
			c->win, x, y, w, h, *newy, nrem, avail);
	if (!c->hoff && h < globalcfg[MinWH]) {
		c->floating = 1;
		h = MAX(wh / 6, 240);
		w = MAX(ww / 6, 360);
		offsetfloat(c, 4, &x, &y, &w, &h);
		setstackmode(c->win, XCB_STACK_MODE_ABOVE);
	} else if (nrem > 1 && (nrem - 1) * (globalcfg[MinWH] + gap) > avail) {
		h += avail - ((nrem - 1) * (globalcfg[MinWH] + gap));
		ret = -1;
	} else if (nrem == 1 && *newy + (h - gap) != wh) {
		if (p) {
			if (p->h + avail < globalcfg[MinWH]) {
				ret = -1;
				setclientgeom(p, p->x, p->y, p->w, globalcfg[MinWH]);
				y = p->y + globalcfg[MinWH] + gap;
				h = wh - (p->y + p->h);
			} else if (h < globalcfg[MinWH]) {
				ret = -1;
				setclientgeom(p, p->x, p->y, p->w,
						p->h + avail - (globalcfg[MinWH] - h - (2 * b)));
				y = p->y + p->h + (2 * b) + gap;
				h = globalcfg[MinWH] - (2 * b);
			} else {
				setclientgeom(p, p->x, p->y, p->w, p->h + avail);
				y += avail;
			}
		} else {
			h = wh;
			ret = -1;
		}
	} else if (h < globalcfg[MinWH]) {
		ret = -1;
		h = globalcfg[MinWH];
	}
	setclientgeom(c, x, y, w - (2 * b), h - (2 * b));
	if (!c->floating)
		*newy += h + gap;
	return ret;
}

int tile(Workspace *ws)
{
	int ret = 1;
	Monitor *m = ws->mon;
	Client *c, *prev = NULL;
	int i, n, nr, my, sy, ssy, w, h, bw, g;
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
	g = globalcfg[SmartGap] && n == 1 ? 0 : ws->gappx;
	bw = globalcfg[SmartBorder] && n == 1 ? 0 : 1;
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

	DBG("tile: ws: %d - mon height: %d - mwidth: %d - swidth: %d - sswidth: %d",
			ws->num, m->ww, mw, sw, ssw);
	for (i = 0, my = sy = ssy = g, c = nextt(ws->clients); c; c = nextt(c->next), ++i) {
		if (i < ws->nmaster) {
			nr = MIN(n, ws->nmaster) - i;
			h = ((wh - my) / MAX(1, nr)) - g + c->hoff;
			w = mw - g * (5 - ns) / 2;
			if (tiler(c, prev, ww - (2 * g), wh - (2 * g), wx + g,
						wy + my, w, h, bw, g, &my, nr, wh - (my + h + g)) < 0)
				ret = -1;
		} else if (i - ws->nmaster < ws->nstack) {
			nr = MIN(n - ws->nmaster, ws->nstack) - (i - ws->nmaster);
			h = ((wh - sy) / MAX(1, nr)) - g + c->hoff;
			w = sw - g * (5 - ns - ss) / 2;
			if (tiler(c, prev, ww - (2 * g), wh - (2 * g), wx + mw + (g / ns),
						wy + sy, w, h, bw, g, &sy, nr, wh - (sy + h + g)) < 0)
				ret = -1;
		} else {
			nr = n - i;
			h = ((wh - ssy) / MAX(1, nr)) - g + c->hoff;
			w = ssw - g * (5 - ns) / 2;
			if (tiler(c, prev, ww - (2 * g), wh - (2 * g), wx + mw + sw + (g / ns),
						wy + ssy, w, h, bw, g, &ssy, nr, wh - (ssy + h + g)) < 0)
				ret = -1;
		}
		prev = (nr == 1 && n - i != 0) ? NULL : c;
	}
	/* now actually do the resizes needed to avoid flicker when resizing previous clients */
	for (c = nextt(ws->clients); c; c = nextt(c->next)) {
		applysizehints(c, &c->x, &c->y, &c->w, &c->h, bw ? c->bw : 0, 0, 0);
		if (c->x != c->old_x || c->y != c->old_y || c->w != c->old_w
				|| c->h != c->old_h || (bw ? c->bw : 0) != c->old_bw)
		{
			MOVERESIZE(c->win, c->x, c->y, c->w, c->h, bw ? c->bw : 0);
			drawborder(c, c == selws->sel);
			sendconfigure(c);
		}
	}
	return ret;
}

void unfocus(Client *c, int focusroot)
{
	if (!c)
		return;
	grabbuttons(c, 0);
	drawborder(c, 0);
	if (focusroot) {
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
		xcb_delete_property(con, root, netatom[Active]);
	}
}

void ungrabpointer(void)
{
	xcb_void_cookie_t c;

	c = xcb_ungrab_pointer_checked(con, XCB_CURRENT_TIME);
	iferr(1, "failed to ungrab pointer", xcb_request_check(con, c));
}

void updclientlist(void)
{
	Desk *d;
	Panel *p;
	Client *c;
	Workspace *ws;

	xcb_delete_property(con, root, netatom[ClientList]);
	FOR_CLIENTS(c, ws)
		PROP_APPEND(root, netatom[ClientList], XCB_ATOM_WINDOW, 32, 1, &c->win);
	FOR_EACH(p, panels)
		PROP_APPEND(root, netatom[ClientList], XCB_ATOM_WINDOW, 32, 1, &p->win);
	FOR_EACH(d, desks)
		PROP_APPEND(root, netatom[ClientList], XCB_ATOM_WINDOW, 32, 1, &d->win);
}

void updnumws(int needed)
{
	Workspace *ws;
	Monitor *m = NULL;

	if (!assignws(needed))
		return;
	m = nextmon(monitors);
	FOR_EACH(ws, workspaces) {
		if (!m->ws)
			m->ws = ws;
		ws->mon = m;
		DBG("updnumws: %d:%s -> %s - visible: %d", ws->num, ws->name, m->name, ws == m->ws);
		if (!(m = nextmon(m->next)))
			m = nextmon(monitors);
	}
	PROP_REPLACE(root, netatom[NumDesktops], XCB_ATOM_CARDINAL, 32, 1, &globalcfg[NumWs]);
	updviewports();
	setnetwsnames();
}

int updoutput(xcb_randr_output_t id, xcb_randr_get_output_info_reply_t *o,
		xcb_timestamp_t timestamp, int changed, int *nmons,
		unsigned int *maxw, unsigned int *maxh, unsigned int *mmaxw, unsigned int *mmaxh)
{
	unsigned int n;
	Monitor *m;
	char name[64];
	xcb_generic_error_t *e;
	xcb_randr_get_crtc_info_cookie_t ck;
	xcb_randr_get_crtc_info_reply_t *crtc;

	ck = xcb_randr_get_crtc_info(con, o->crtc, timestamp);
	crtc = xcb_randr_get_crtc_info_reply(con, ck, &e);
	if (!crtc || !xcb_randr_get_crtc_info_outputs_length(crtc)) {
		iferr(0, "unable to get crtc info reply", e);
		goto out;
	}
	n = xcb_randr_get_output_info_name_length(o) + 1;
	strlcpy(name, (char *)xcb_randr_get_output_info_name(o), MIN(sizeof(name), n));
	FOR_EACH(m, monitors) {
		if (id != m->id && m->x == crtc->x && m->y == crtc->y) {
			DBG("updoutput: %s is a clone of %s", name, m->name);
			goto out;
		}
	}
	if (crtc->x + crtc->width > (int)*maxw) {
		*maxw = crtc->x + crtc->width;
		*mmaxw += o->mm_width;
	}
	if (crtc->y + crtc->height > (int)*maxh) {
		*maxh = crtc->y + crtc->height;
		*mmaxh += o->mm_height;
	}
	if ((m = idtomon(id))) {
		changed = changed || !m->connected || crtc->x != m->x || crtc->y != m->y
			|| crtc->width != m->w || crtc->height != m->h;
		m->num = *nmons++;
		m->x = m->wx = crtc->x;
		m->y = m->wy = crtc->y;
		m->w = m->ww = crtc->width;
		m->h = m->wh = crtc->height;
		m->connected = 1;
	} else {
		initmon(*nmons++, name, id, crtc->x, crtc->y, crtc->width, crtc->height);
		changed = 1;
	}
	DBG("updoutput: %s - %d,%d @ %dx%d - changed: %d", name,
			crtc->x, crtc->y, crtc->width, crtc->height, changed);
out:
	free(crtc);
	return changed;
}

int updoutputs(xcb_randr_output_t *outs, int nouts, xcb_timestamp_t t)
{
	Monitor *m;
	xcb_generic_error_t *e;
	int i, nmons, changed = 0;
	xcb_randr_get_output_info_reply_t *o;
	xcb_randr_get_output_info_cookie_t oc[nouts];
	unsigned int maxw = 0, maxh = 0, mmaxw = 0, mmaxh = 0;

	DBG("updoutputs: checking %d outputs for changes", nouts);
	for (i = 0; i < nouts; i++)
		oc[i] = xcb_randr_get_output_info(con, outs[i], t);
	for (i = 0, nmons = 0; i < nouts; i++) {
		if (!(o = xcb_randr_get_output_info_reply(con, oc[i], &e)) || o->crtc == XCB_NONE)
			iferr(0, "unable to get output info or output has no crtc", e);
		else if (o->connection == XCB_RANDR_CONNECTION_CONNECTED)
			changed = updoutput(outs[i], o, t, changed, &nmons, &maxw, &maxh, &mmaxw, &mmaxh);
		else if (o->connection == XCB_RANDR_CONNECTION_DISCONNECTED && (m = idtomon(outs[i])))
			changed = disablemon(m, o, changed);
		free(o);
	}
	if (changed)
		updscreen(maxw, maxh, mmaxw, mmaxh);
	return changed;
}

int updrandr(void)
{
	int n, changed = 0;
	xcb_randr_output_t *o;
	xcb_generic_error_t *e;
	xcb_randr_get_screen_resources_current_reply_t *r;
	xcb_randr_get_screen_resources_current_cookie_t rc;

	rc = xcb_randr_get_screen_resources_current(con, root);
	if ((r = xcb_randr_get_screen_resources_current_reply(con, rc, &e))) {
		n = xcb_randr_get_screen_resources_current_outputs_length(r);
		o = xcb_randr_get_screen_resources_current_outputs(r);
		changed = updoutputs(o, n, r->config_timestamp);
		free(r);
	} else
		iferr(0, "unable to get screen resources", e);
	return changed;
}

void updscreen(int maxw, int maxh, int mmaxw, int mmaxh)
{
	xcb_void_cookie_t vc;
	xcb_randr_get_output_primary_reply_t *po = NULL;

	if (maxw != scr_w || maxh != scr_h) {
		DBG("updscreen: size changed: %d,%d -> %d,%d", scr_w, scr_h, maxw, maxh);
		scr_w = maxw;
		scr_h = maxh;
		/* we need to update the root screen size with the new size. X doesn't update
		 * itself so we're left with void space when a monitor is disconnected, where
		 * the cursor and windows can go but not be seen, yikes! */
		vc = xcb_randr_set_screen_size_checked(con, root, maxw, maxh, mmaxw, mmaxh);
		iferr(0, "unable to set new screen size", xcb_request_check(con, vc));
	}
	po = xcb_randr_get_output_primary_reply(con, xcb_randr_get_output_primary(con, root), NULL);
	if (!(primary = idtomon(po->output)))
		primary = monitors;
	free(po);
}

void updstruts(Panel *p, int apply)
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

void updviewports(void)
{
	int v[2];
	Workspace *ws;

	xcb_delete_property(con, root, netatom[Viewport]);
	FOR_EACH(ws, workspaces) {
		if (!ws->mon)
			ws->mon = primary;
		v[0] = ws->mon->x;
		v[1] = ws->mon->y;
		PROP_APPEND(root, netatom[Viewport], XCB_ATOM_CARDINAL, 32, 2, &v);
	}
}

void updworkspaces(int needed)
{
	Desk *d;
	Panel *p;
	Client *c;
	Workspace *ws;

	updnumws(needed);
	FOR_CLIENTS(c, ws) {
		if (ISFULL(c))
			resize(c, ws->mon->x, ws->mon->y, ws->mon->w, ws->mon->h, c->bw);
	}
	FOR_EACH(p, panels)
		updstruts(p, 1);
	FOR_EACH(d, desks)
		if (d->x != d->mon->wx || d->y != d->mon->wy || d->w != d->mon->ww || d->h != d->mon->wh) {
			d->x = d->mon->wx, d->y = d->mon->wy, d->w = d->mon->ww, d->h = d->mon->wh;
			MOVERESIZE(d->win, d->x, d->y, d->w, d->h, 0);
		}
	usenetcurdesktop();
	needsrefresh++;
}

void usage(int e, char flag)
{
	switch (flag) {
	case 'h':
		fprintf(stderr, "usage: yaxwm [-hv] [-s SOCKET_FD] [-c COMMAND]\n");
		break;
	case 'v':
		fprintf(stderr, "yaxwm "VERSION"\n");
		break;
	}
	exit(e);
}

void usenetcurdesktop(void)
{
	int cws;
	xcb_atom_t r;
	Workspace *ws;

	cws = winprop(root, netatom[CurDesktop], &r) && r < 100 ? r : 0;
	if (cws + 1 > globalcfg[NumWs])
		updnumws(cws + 1);
	ws = itows(cws);
	changews(ws ? ws : workspaces, 1, 0);
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
			iferr(0, "unable to get window attributes reply", e);
	}
	return wa;
}

Geometry *wingeom(xcb_window_t win)
{
	Geometry *g = NULL;
	xcb_generic_error_t *e;
	xcb_get_geometry_cookie_t gc;

	if (!win)
		return g;
	gc = xcb_get_geometry(con, win);
	DBG("wingeom: getting window geometry - 0x%08x", win);
	if (!(g = xcb_get_geometry_reply(con, gc, &e)))
		iferr(0, "unable to get window geometry reply", e);
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
		c->noinput = (wmh.flags & XCB_ICCCM_WM_HINT_INPUT) ? !wmh.input : 0;
	} else {
		iferr(0, "unable to get window wm hints reply", e);
	}
}

int winmotifhints(xcb_window_t win, int *decorate)
{
	int i = 0;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t c;
	xcb_get_property_reply_t *r = NULL;

	c = xcb_get_property(con, 0, win, wmatom[MotifHints], wmatom[MotifHints], 0, 5);
	DBG("winmotifhints: getting window motif hints property atom - 0x%08x", win);
	if ((r = xcb_get_property_reply(con, c, &e)) && xcb_get_property_value_length(r)) {
		i = 1;
		*decorate = ((xcb_atom_t *)xcb_get_property_value(r))[2];
		DBG("winmotifhints: decorations property reply value: %d", *decorate);
	} else
		iferr(0, "unable to get window motif hints property reply", e);
	free(r);
	return i;
}

int winprop(xcb_window_t win, xcb_atom_t prop, xcb_atom_t *ret)
{
	int i = 0;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t c;
	xcb_get_property_reply_t *r = NULL;

	c = xcb_get_property(con, 0, win, prop, XCB_ATOM_ANY, 0, 1);
	DBG("winprop: getting window property atom - 0x%08x", win);
	if ((r = xcb_get_property_reply(con, c, &e)) && xcb_get_property_value_length(r)) {
		i = 1;
		*ret = *(xcb_atom_t *)xcb_get_property_value(r);
		DBG("winprop: property reply value: %d", *ret);
	} else
		iferr(0, "unable to get window property reply", e);
	free(r);
	return i;
}

int wintextprop(xcb_window_t win, xcb_atom_t atom, char *text, size_t size)
{
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t c;
	xcb_icccm_get_text_property_reply_t r;

	c = xcb_icccm_get_text_property(con, win, atom);
	DBG("wintextprop: getting window text property - 0x%08x", win);
	if (!xcb_icccm_get_text_property_reply(con, c, &r, &e)) {
		iferr(0, "unable to get text property reply", e);
		return 0;
	} else if (r.name && r.name_len) {
		strlcpy(text, r.name, MIN(r.name_len + 1, size));
		DBG("winclassprop: text property reply: %s", text);
	}
	xcb_icccm_get_text_property_reply_wipe(&r);
	return 1;
}

int winclassprop(xcb_window_t win, char *class, char *inst, size_t csize, size_t isize)
{
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t pc;
	xcb_icccm_get_wm_class_reply_t prop;

	pc = xcb_icccm_get_wm_class(con, win);
	if (xcb_icccm_get_wm_class_reply(con, pc, &prop, &e)) {
		strlcpy(class, prop.class_name, csize);
		strlcpy(inst, prop.instance_name, isize);
		DBG("winclassprop: class reply: %s, %s", class, inst);
		xcb_icccm_get_wm_class_reply_wipe(&prop);
	} else {
		iferr(0, "failed to get window class", e);
		*inst = *class = '\0';
		return 0;
	}
	return 1;
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
	DBG("wintrans: getting transient for hint - 0x%08x", win);
	if (!xcb_icccm_get_wm_transient_for_reply(con, pc, &t, &e))
		iferr(0, "unable to get wm transient for hint", e);
	return t;
}

void wintype(Client *c)
{
	xcb_atom_t type, state;

	if (winprop(c->win, netatom[State], &state) && state == netatom[Fullscreen])
		setfullscreen(c, 1);
	if (winprop(c->win, netatom[WindowType], &type)) {
		if (type == netatom[Dialog] || type == netatom[Splash])
			c->floating = 1;
	} else if (c->trans || wintrans(c->win))
		c->floating = 1;
}

int writecmd(int argc, char *argv[])
{
	ssize_t s;
	size_t j = 0, n = 0;
	int i, r = 0, offs = 1;
	char *eq = NULL, *sp = NULL, buf[BUFSIZ], resp[BUFSIZ];

	if (!argc)
		errx(1, "%s", enoargs);
	initsock(1);

	struct pollfd fds[] = {
		{ sockfd,        POLLIN,  0 },
		{ STDOUT_FILENO, POLLHUP, 0 },
	};
	for (i = 0, j = 0, offs = 1; n + 1 < sizeof(buf) && i < argc; i++, j = 0, offs = 1) {
		if ((sp = strchr(argv[i], ' ')) || (sp = strchr(argv[i], '\t'))) {
			if (!(eq = strchr(argv[i], '=')) || sp < eq) /* no equal found or equal is part of the quoted string */
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
					fprintf(stderr, "yaxwm: command error: %s\n", resp + 1);
					fflush(stderr);
				} else {
					fprintf(stdout, "%s\n", resp);
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
