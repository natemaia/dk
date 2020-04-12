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
#include <locale.h>

#include <xcb/randr.h>
#include <xcb/xproto.h>
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
#define VERSION "0.6"
#endif

#define W(x)                ((x)->w + (2 * (x)->bw))
#define H(x)                ((x)->h + (2 * (x)->bw))
#define MIN(a, b)           ((a) < (b) ? (a) : (b))
#define MAX(a, b)           ((a) > (b) ? (a) : (b))
#define CLAMP(x, min, max)  (MIN(MAX((x), (min)), (max)))
#define LEN(x)              (sizeof(x) / sizeof(x[0]))
#define CLNMOD(mod)         (mod & ~(lockmask | XCB_MOD_MASK_LOCK))
#define FLOATING(c)         ((c)->floating || !(c)->ws->layout->fn)
#define STICKY              (0xffffffff)
#define BWMASK              (XCB_CONFIG_WINDOW_BORDER_WIDTH)
#define XYMASK              (XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y)
#define WHMASK              (XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT)
#define BUTTONMASK          (XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE)
#define EVENT_RESPONSE_MASK (0x7f)
#define EVENT_TYPE(e)       (e->response_type &  EVENT_RESPONSE_MASK)
#define EVENT_SENT(e)       (e->response_type & ~EVENT_RESPONSE_MASK)

#define FOR_EACH(v, list)\
	for ((v) = (list); (v); (v) = (v)->next)
#define FIND_TAIL(v, list)\
	for ((v) = (list); (v) && (v)->next; (v) = (v)->next)
#define FIND_PREV(v, cur, list)\
	for ((v) = (list); (v) && (v)->next && (v)->next != (cur); (v) = (v)->next)

#define FOR_STACK(v, list)\
	for ((v) = (list); (v); (v) = (v)->snext)
#define FOR_CLIENTS(c, ws)\
	FOR_EACH((ws), workspaces) FOR_EACH((c), (ws)->clients)
#define FIND_TILETAIL(v, list)\
	for ((v) = nextt((list)); (v) && nextt((v)->next); (v) = nextt((v)->next))
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

enum Cursors { Move, Normal, Resize };
enum Borders { Width, Smart, Focus, Unfocus, Urgent };
enum Gravity { None, Left, Right, Center, Top, Bottom };
enum GlobalCfg { SizeHints, FocusMouse, FocusUrgent, NumWs, MinXY, MinWH };

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
	int sticky, fixed, floating, fullscreen, ffs, urgent, noinput, oldstate;
	Client *trans, *next, *snext;
	Workspace *ws;
	Callback *cb;
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
	int connected;
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
	void (*fn)(Client *, int);
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
	int padl, padr, padt, padb;
	float split;
	float ssplit;
	Layout *layout;
};

static void applywinrule(Client *c);
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
static void cmdssplit(char **);
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
static Monitor *nextcon(Monitor *m, int direction);
static Client *nextt(Client *);
static Monitor *opttomon(int opt);
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
static void setinputfocus(Client *);
static void setnetwsnames(void);
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
static void usenetcurdesktop(void);
static WindowAttr *winattr(xcb_window_t);
static Geometry *wingeom(xcb_window_t);
static void winhints(Client *);
static xcb_atom_t winprop(xcb_window_t, xcb_atom_t);
static int wintextprop(xcb_window_t, xcb_atom_t, char *, size_t);
static Client *wintoclient(xcb_window_t);
static DeskWin *wintodeskwin(xcb_window_t);
static Panel *wintopanel(xcb_window_t);
static xcb_window_t wintrans(xcb_window_t);
static void wintype(Client *);

enum MoveOpts {
	optnext,
	optprev,
	optlast
};
static char *moveopts[] = {
	[optnext] = "next",
	[optprev] = "prev",
	[optlast] = "last",
	NULL
};

enum WMAtoms {
	Delete,
	Protocols,
	TakeFocus,
	Utf8Str,
	WMState
};
static const char *wmatoms[] = {
	[Delete] = "WM_DELETE_WINDOW",
	[Protocols] = "WM_PROTOCOLS",
	[TakeFocus] = "WM_TAKE_FOCUS",
	[Utf8Str] = "UTF8_STRING",
	[WMState] = "WM_STATE",
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
	{ "ssplit", cmdssplit  },
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

static FILE *cmdresp; /* file for writing command messages into */
static const char *enoargs = "command requires additional arguments but none were given";
