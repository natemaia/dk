/* dk - /dəˈkā/ window manager
 *
 * see license file for copyright and license details
 * vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
 */

#pragma once

#ifdef DEBUG
#define DBG(fmt, ...) warnx("%d: " fmt, __LINE__, ##__VA_ARGS__); fflush(stderr);
#else
#define DBG(fmt, ...)
#endif

#ifndef VERSION
#define VERSION "0.91"
#endif

#define W(c) (c->w + (2 * c->bw))
#define H(c) (c->h + (2 * c->bw))
#define LEN(x) (sizeof(x) / sizeof(x[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(x, min, max) (MIN(MAX((x), (min)), (max)))

#define FLOATING(c) (c->state & STATE_FLOATING || !c->ws->layout->func)
#define FULLSCREEN(c) (c->state & STATE_FULLSCREEN && !(c->state & STATE_FAKEFULL))

#define FOR_EACH(v, list) if (list) for (v = list; v; v = v->next)
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
	xcb_change_property(con, XCB_PROP_MODE_##mode, win, atom, type, (membsize), (nmemb), value)
#define GET(win, val, vc, error, type, functtype)                       \
	do {                                                                \
		if (win && win != root) {                                       \
			vc = xcb_get_##functtype(con, win);                         \
			if (!(val = xcb_get_##functtype##_reply(con, vc, &error)))  \
				iferr(0, "unable to get window " type " reply", error); \
		}                                                               \
	} while (0)

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
	char title[255], class[64], inst[64];
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

typedef struct Cmd {
	const char *str;
	int (*func)(char **);
} Cmd;

typedef struct WsCmd {
	const char *str;
	int (*func)(Workspace *);
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


extern char **environ;
static FILE *cmdresp;
static unsigned int lockmask = 0;
static char *argv0, sock[255], status[255];
static int scr_h, scr_w, sockfd, running, restart, randrbase, cmdusemon, needsrefresh;
const char *ebadarg = "invalid argument for";
const char *enoargs = "command requires additional arguments but none were given";

Desk *desks;
Rule *rules;
Panel *panels;
Client *cmdclient;
Monitor *primary, *monitors, *selmon, *lastmon;
Workspace *setws, *selws, *lastws, *workspaces;

static xcb_screen_t *scr;
static xcb_connection_t *con;
static xcb_window_t root, wmcheck;
static xcb_key_symbols_t *keysyms;
static xcb_cursor_t cursor[CURS_LAST];
static xcb_atom_t wmatom[LEN(wmatoms)], netatom[LEN(netatoms)];


void applypanelstrut(Panel *p);
int applysizehints(Client *c, int *x, int *y, int *w, int *h, int bw, int usermotion, int mouse);
int assignws(Workspace *ws, Monitor *new);
void changews(Workspace *ws, int swap, int warp);
void clienthints(Client *c);
int clientname(Client *c);
void clientrule(Client *c, Rule *wr, int nofocus);
void clienttype(Client *c);
Monitor *coordtomon(int x, int y);
void detach(Client *c, int reattach);
void drawborder(Client *c, int focused);
void execcfg(void);
void focus(Client *c);
void freemon(Monitor *m);
void freerule(Rule *r);
void freewm(void);
void freews(Workspace *ws);
void grabbuttons(Client *c, int focused);
void gravitate(Client *c, int horz, int vert, int matchgap);
int iferr(int lvl, char *msg, xcb_generic_error_t *e);
Rule *initrule(Rule *wr);
void initscan(void);
void initsock(void);
void initwm(void);
Monitor *itomon(int num);
Workspace *itows(int num);
void manage(xcb_window_t win, int scan);
void movestack(int direction);
Monitor *nextmon(Monitor *m);
Client *nexttiled(Client *c);
Monitor *outputtomon(xcb_randr_output_t id);
void popfloat(Client *c);
void pushstatus(void);
void quadrant(Client *c, int *x, int *y, int *w, int *h);
void refresh(void);
void relocate(Client *c, Monitor *new, Monitor *old);
void relocatews(Workspace *ws, Monitor *old);
void resize(Client *c, int x, int y, int w, int h, int bw);
void resizehint(Client *c, int x, int y, int w, int h, int bw, int usermotion, int mouse);
void restack(Workspace *ws);
int rulecmp(Client *c, Rule *r);
void sendconfigure(Client *c);
int sendwmproto(Client *c, int wmproto);
void setfullscreen(Client *c, int fullscreen);
void setinputfocus(Client *c);
void setnetwsnames(void);
void setstackmode(xcb_window_t win, unsigned int mode);
void seturgent(Client *c, int urg);
void setwmwinstate(xcb_window_t win, long state);
void setworkspace(Client *c, int num, int stacktail);
void showhide(Client *c);
void sizehints(Client *c, int uss);
void unfocus(Client *c, int focusroot);
void unmanage(xcb_window_t win, int destroyed);
void updnetworkspaces(void);
int updoutputs(xcb_randr_output_t *outs, int nouts, xcb_timestamp_t t);
int updrandr(void);
void updstruts(Panel *p, int apply);
void updworkspaces(int needed);
xcb_get_window_attributes_reply_t *winattr(xcb_window_t win);
xcb_get_geometry_reply_t *wingeom(xcb_window_t win);
int winprop(xcb_window_t win, xcb_atom_t prop, xcb_atom_t *ret);
Client *wintoclient(xcb_window_t win);
Desk *wintodesk(xcb_window_t win);
Panel *wintopanel(xcb_window_t win);
xcb_window_t wintrans(xcb_window_t win);
