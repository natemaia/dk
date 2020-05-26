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



/* file for writing command messages into */
static FILE *cmdresp;
static const char *ebadarg = "invalid argument for";
static const char *enoargs = "command requires additional arguments but none were given";


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
typedef struct Rule Rule;
typedef struct Desk Desk;
typedef struct Panel Panel;
typedef struct Client Client;
typedef struct Layout Layout;
typedef struct WinNode WinNode;
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

struct WinNode {
	xcb_window_t win;
	WinNode *next;
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
static void changews(Workspace *, int, int);
static void cmdborder(char **);
static void cmdcycle(char **);
static void cmdfakefull(char **);
static void cmdfloat(char **);
static void cmdfocus(char **);
static void cmdprint(char **);
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
static void clientborder(Client *, int);
static Monitor *coordtomon(int, int);
static void detach(Client *, int);
static void *ecalloc(size_t, size_t);
static void eventignore(uint8_t);
static void eventloop(void);
static void eventrandr(xcb_randr_screen_change_notify_event_t *);
static void execcfg(void);
static void offsetfloat(Client *, int, int *, int *, int *, int *);
static void focus(Client *);
static void freeclient(Client *, int);
static void freedesk(Desk *, int);
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
static Client *prevc(Client *);
static void initmon(int, char *, xcb_randr_output_t, int, int, int, int);
static void initpanel(xcb_window_t, Geometry *);
static int initrulereg(Rule *, Rule *);
static void initneedsmap(xcb_window_t win);
static Rule *initrule(Rule *);
static void initscan(void);
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
static void parsecmd(char *);
static void printerror(xcb_generic_error_t *);
static int querypointer(int *, int *);
static void relocate(Workspace *, Monitor *);
static void resize(Client *, int, int, int, int, int);
static void resizehint(Client *, int, int, int, int, int, int, int);
static void restack(Workspace *);
static void freeneedsmap(WinNode *);
static void refresh(void);
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

