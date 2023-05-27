/* dk window manager
 *
 * see license file for copyright and license details
 * vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
 */

#pragma once

#include <sys/un.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <regex.h>
#include <signal.h>
#include <fcntl.h>
#include <err.h>

#include <xcb/randr.h>
#include <xcb/xproto.h>
#include <xcb/xcb_util.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_cursor.h>
#include <xcb/xcb_keysyms.h>

#ifdef DEBUG
	#define DBG(fmt, ...)  warnx("%d: " fmt, __LINE__, __VA_ARGS__);
#else
	#define DBG(fmt, ...)
#endif

#ifndef VERSION
	#define VERSION "1.0"
#endif

#ifndef NAN
	#ifdef __GNUC_PREREQ
		#if __GNUC_PREREQ (3, 3)
			#define NAN (__builtin_nanf(""))
		#endif
	#else
		#define NAN (0.0f / 0.0f)
	#endif
#endif

#define LEN(x)                (sizeof(x) / sizeof(*x))
#define MIN(a, b)             ((a) < (b) ? (a) : (b))
#define MAX(a, b)             ((a) > (b) ? (a) : (b))
#define CLAMP(x, min, max)    (MIN(MAX((x), (min)), (max)))
#define INRECT(x, y, w, h, rx, ry, rw, rh) \
	(x >= rx && x + w <= rx + rw && y >= ry && y + h <= ry + rh)

#define ISTILE(ws) (ws->layout->func == ltile || ws->layout->func == rtile)

#define W(c)            (c->w + (2 * c->bw))
#define H(c)            (c->h + (2 * c->bw))
#define FLOATING(c)     (c->state & STATE_FLOATING || !c->ws->layout->func)
#define FULLSCREEN(c)   (c->state & STATE_FULLSCREEN && !(c->state & STATE_FAKEFULL))

#define FOR_EACH(v, list)    if (list) for (v = list; v; v = v->next)
#define FOR_CLIENTS(c, ws)   FOR_EACH(ws, workspaces) FOR_EACH(c, ws->clients)

#define FIND_TAIL(v, list)        for (v = list; v && v->next; v = v->next)
#define FIND_PREV(v, cur, list)   for (v = list; v && v->next && v->next != cur; v = v->next)

#define WINTO(for_macro, win, ptr, arr)                           \
	do {                                                          \
		if (win != XCB_WINDOW_NONE && win != root)                \
			for_macro(ptr, arr) if (ptr->win == win) return ptr;  \
		return NULL;                                              \
	} while (0)

#define ATTACH(v, list)           \
	do {                          \
		v->next = list; list = v; \
	} while (0)

#define DETACH(v, listptr)				      \
	do {                                      \
		while (*(listptr) && *(listptr) != v) \
			(listptr) = &(*(listptr))->next;  \
		*(listptr) = v->next;                 \
	} while (0)

#define PROP(mode, win, atom, type, membsize, nmemb, value) \
	xcb_change_property(con, XCB_PROP_MODE_##mode, win, atom, type, (membsize), (nmemb), (const void *)value)

#define GET(win, val, error, type, functtype)                                                 \
	do {                                                                                      \
		if (win == XCB_WINDOW_NONE) return val;                                               \
		if (!(val = xcb_get_##functtype##_reply(con, xcb_get_##functtype(con, win), &error))) \
			iferr(0, "unable to get window " type " reply", error);                           \
	} while (0)


#define MOVE(win, x, y)                                                       \
	xcb_configure_window(con, win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (uint32_t[]){(x), (y)})

#define MOVERESIZE(win, x, y, w, h, bw)                                     \
	xcb_configure_window(con, win,                                          \
			XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y                       \
			| XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT            \
			| XCB_CONFIG_WINDOW_BORDER_WIDTH,                               \
			(uint32_t[]){(x), (y), MAX((w), globalcfg[GLB_MIN_WH].val),     \
			MAX((h), globalcfg[GLB_MIN_WH].val), (bw)})


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
	STATE_WASFLOATING  = 1 << 9,
	STATE_IGNORECFG    = 1 << 10,
	STATE_ABOVE        = 1 << 11,
	STATE_HIDDEN       = 1 << 12,
};

enum Cursors {
	CURS_MOVE   = 0,
	CURS_NORMAL = 1,
	CURS_RESIZE = 2,
	CURS_LAST   = 3,
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
	BORD_LAST      = 8,
};

enum DirOpts {
	DIR_NEXT   = 0,
	DIR_PREV   = 1,
	DIR_LAST   = 2,
	DIR_NEXTNE = 3,
	DIR_PREVNE = 4,
	DIR_END    = 5
};

enum WMAtoms {
	WM_DELETE  = 0,
	WM_FOCUS   = 1,
	WM_MOTIF   = 2,
	WM_PROTO   = 3,
	WM_STATE   = 4,
	WM_UTF8STR = 5,
	WM_LAST    = 6,
};

enum NetAtoms {
	NET_ACTIVE          = 0,
	NET_CLIENTS         = 1,
	NET_CLOSE           = 2,
	NET_DESK_CUR        = 3,
	NET_DESK_GEOM       = 4,
	NET_DESK_NAMES      = 5,
	NET_DESK_NUM        = 6,
	NET_DESK_VP         = 7,
	NET_DESK_WA         = 8,
	NET_STATE_ABOVE     = 9,
	NET_STATE_DEMANDATT = 10,
	NET_STATE_FULL      = 11,
	NET_SUPPORTED       = 12,
	NET_TYPE_DESK       = 13,
	NET_TYPE_DIALOG     = 14,
	NET_TYPE_DOCK       = 15,
	NET_TYPE_SPLASH     = 16,
	NET_WM_CHECK        = 17,
	NET_WM_DESK         = 18,
	NET_WM_NAME         = 19,
	NET_WM_STATE        = 20,
	NET_WM_STRUT        = 21,
	NET_WM_STRUTP       = 22,
	NET_WM_TYPE         = 23,
	NET_LAST            = 24,
};

enum Gravities {
	GRAV_NONE   = 0,
	GRAV_LEFT   = 1,
	GRAV_RIGHT  = 2,
	GRAV_CENTER = 3,
	GRAV_TOP    = 4,
	GRAV_BOTTOM = 5,
	GRAV_LAST   = 6,
};

enum StatusType {
	STAT_WS   = 0,
	STAT_LYT  = 1,
	STAT_WIN  = 2,
	STAT_BAR  = 3,
	STAT_FULL = 4,
};

enum CfgType {
	TYPE_BOOL  = 0,
	TYPE_NUMWS = 1,
	TYPE_INT   = 2,
};

enum GlobalSettings {
	GLB_WS_NUM       = 0,
	GLB_WS_STATIC    = 1,
	GLB_FOCUS_MOUSE  = 2,
	GLB_FOCUS_OPEN   = 3,
	GLB_FOCUS_URGENT = 4,
	GLB_MIN_WH       = 5,
	GLB_MIN_XY       = 6,
	GLB_SMART_BORDER = 7,
	GLB_SMART_GAP    = 8,
	GLB_TILE_HINTS   = 9,
	GLB_TILE_TOHEAD  = 10,
	GLB_LAST         = 11,
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
	uint32_t state;
	xcb_window_t win;
	struct Desk *next;
	Monitor *mon;
} Desk;

typedef struct Rule {
	int x, y, w, h, bw;
	int xgrav, ygrav;
	int ws, focus;
	uint32_t state;
	xcb_atom_t type;
	char *title, *class, *inst, *mon;
	const Callback *cb;
	regex_t titlereg, classreg, instreg;
	struct Rule *next;
} Rule;

typedef struct Panel {
	int x, y, w, h;
	int l, r, t, b; /* struts */
	uint32_t state;
	char class[64], inst[64];
	xcb_window_t win;
	struct Panel *next;
	Monitor *mon;
} Panel;

typedef struct Status {
	int num;
	uint32_t type;
	FILE *file;
	char *path;
	struct Status *next;
} Status;

typedef struct Client {
	char title[256], class[64], inst[64];
	int32_t x, y, w, h, bw, hoff, depth, old_x, old_y, old_w, old_h, old_bw;
	int32_t max_w, max_h, min_w, min_h, base_w, base_h, inc_w, inc_h, hints;
	float min_aspect, max_aspect;
	uint32_t state, old_state;
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
	int implements_resize;
	int invert_split_direction;
} Layout;

typedef struct GlobalCfg {
	int val;
	enum CfgType type;
	char *str;
} GlobalCfg;

struct Callback {
	const char *name;
	void (*func)(Client *, int);
};

struct Workspace {
	int nmaster, nstack, gappx, smartgap;
	int padr, padl, padt, padb;
	float msplit, ssplit;
	const Layout *layout;
	int num;
	char name[64];
	Monitor *mon;
	Workspace *next;
	Client *sel, *stack, *clients;
};


/* dk.c values */
extern FILE *cmdresp;
extern uint32_t lockmask;
extern char *argv0, **environ;
extern int scr_h, scr_w, randrbase, cmdusemon, winchange, wschange, lytchange;
extern int running, restart, needsrefresh, status_usingcmdresp, depth;

extern Desk *desks;
extern Rule *rules;
extern Panel *panels;
extern Status *stats;
extern Client *cmdc;
extern Monitor *monitors, *primary, *selmon, *lastmon;
extern Workspace *workspaces, *setws, *selws, *lastws;

/* extern xcb_screen_t *scr; */
extern xcb_connection_t *con;
extern xcb_window_t root;
extern xcb_key_symbols_t *keysyms;
extern xcb_cursor_t cursor[CURS_LAST];
extern xcb_atom_t wmatom[WM_LAST], netatom[NET_LAST];

extern const char *ebadarg;
extern const char *enoargs;
extern const char *wmatoms[WM_LAST];
extern const char *netatoms[NET_LAST];
extern const char *cursors[CURS_LAST];
extern const char *directions[DIR_END];
extern const char *gravities[GRAV_LAST];

/* config.h values */
extern uint32_t border[BORD_LAST];
extern GlobalCfg globalcfg[GLB_LAST];
extern xcb_mod_mask_t mousemod;
extern xcb_button_t mousemove, mouseresize;
extern Callback callbacks[];
extern Cmd keywords[];
extern Cmd setcmds[];
extern Cmd wincmds[];
extern Layout layouts[];
extern WsCmd wscmds[];
extern Workspace wsdef;

int applysizehints(Client *c, int *x, int *y, int *w, int *h, int bw, int usermotion, int mouse);
int assignws(Workspace *ws, Monitor *new); //NOLINT
void changews(Workspace *ws, int swap, int warp);
void clientborder(Client *c, int focused);
void clienthints(Client *c);
int clientname(Client *c);
void clientrule(Client *c, Rule *wr, int nofocus);
void clienttype(Client *c);
Monitor *coordtomon(int x, int y);
void detach(Client *c, int reattach);
void execcfg(void);
void fillstruts(Panel *p);
void focus(Client *c);
void freerule(Rule *r);
void freewm(void);
void grabbuttons(Client *c);
void gravitate(Client *c, int horz, int vert, int matchgap);
int iferr(int lvl, char *msg, xcb_generic_error_t *e);
Rule *initrule(Rule *wr);
Status *initstatus(Status *tmp);
Monitor *itomon(int num);
Workspace *itows(int num);
void manage(xcb_window_t win, int scan);
void movestack(int direction);
Monitor *nextmon(Monitor *m);
Client *nexttiled(Client *c);
void popfloat(Client *c);
void printstatus(Status *s, int freeable);
void quadrant(Client *c, int *x, int *y, const int *w, const int *h);
void relocate(Client *c, Monitor *new, Monitor *old); //NOLINT
void resize(Client *c, int x, int y, int w, int h, int bw);
void resizehint(Client *c, int x, int y, int w, int h, int bw, int usermotion, int mouse);
void restack(Workspace *ws);
void sendconfigure(Client *c);
int sendwmproto(Client *c, int wmproto);
void setfullscreen(Client *c, int fullscreen);
void setinputfocus(Client *c);
void setnetstate(xcb_window_t win, uint32_t state);
void setnetwsnames(void);
void setstackmode(xcb_window_t win, uint32_t mode);
void seturgent(Client *c, int urg);
void setwinstate(xcb_window_t win, uint32_t state);
void setworkspace(Client *c, int num, int stacktail);
void showhide(Client *c);
void sizehints(Client *c, int uss);
int tilecount(Workspace *ws);
void unfocus(Client *c, int focusroot);
void unmanage(xcb_window_t win, int destroyed);
int updrandr(int init);
void updstruts(void);
void updworkspaces(int needed);
Client *wintoclient(xcb_window_t win);
Desk *wintodesk(xcb_window_t win);
Panel *wintopanel(xcb_window_t win);
xcb_window_t wintrans(xcb_window_t win);

#ifdef FUNCDEBUG
void __cyg_profile_func_enter(void *fn, void *caller) __attribute__((no_instrument_function));
void __cyg_profile_func_exit(void *fn, void *caller) __attribute__((no_instrument_function));
#endif

