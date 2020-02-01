/* yet another X window manager
*
* vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
*/

/* sigaction */
#define _XOPEN_SOURCE 700

#include <err.h>
#include <stdio.h>
#include <ctype.h>
#include <regex.h>
#include <signal.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <locale.h>
#include <sys/wait.h>
#include <xcb/xcb.h>
#include <xcb/randr.h>
#include <xcb/xproto.h>
#include <xcb/xcb_util.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_cursor.h>
#include <xcb/xcb_keysyms.h>
#include <X11/keysym.h>


#define W(x)                   ((x)->w + 2 * (x)->bw)
#define H(x)                   ((x)->h + 2 * (x)->bw)
#define LEN(x)                 (sizeof(x) / sizeof(x[0]))
#define MAX(a, b)              ((a) > (b) ? (a) : (b))
#define MIN(a, b)              ((a) < (b) ? (a) : (b))
#define CLNMOD(mod)            (mod & ~(numlockmask | XCB_MOD_MASK_LOCK))

/* linked list quick access */
#define FOR_EACH(v, list)      for ((v) = (list); (v); (v) = (v)->next)
#define FOR_STACK(v, list)     for ((v) = (list); (v); (v) = (v)->snext)
#define FOR_TAIL(v, list)      for ((v) = (list); (v) && (v)->next; (v) = (v)->next)
#define FOR_PREV(v, cur, list) for ((v) = (list); (v) && (v)->next && (v)->next != (cur); (v) = (v)->next)
#define FOR_WSCLIENTS(c, ws)   FOR_EACH((ws), workspaces) FOR_EACH((c), (ws)->clients)

/* dissolves into nothing when DEBUG isn't defined */
#define DBG(fmt, ...)
#define DBGBIND(event, mod, sym)

/* less wordy calls to some xcb functions */
#define MOVE(win, x, y) \
	xcb_configure_window(con, (win), XYMASK, (uint []){(x), (y)})
#define PROP_APPEND(win, atom, type, membsize, nmemb, value) \
	xcb_change_property(con, XCB_PROP_MODE_APPEND, (win), (atom), (type), (membsize), (nmemb), (value))
#define PROP_REPLACE(win, atom, type, membsize, nmemb, value) \
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, (win), (atom), (type), (membsize), (nmemb), (value))


/* bit masks */
#define STICKYMASK  (0xFFFFFFFF)
#define BWMASK      (XCB_CONFIG_WINDOW_BORDER_WIDTH)
#define XYMASK      (XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y)
#define WHMASK      (XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT)
#define BUTTONMASK  (XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE)
#define GRABMASK    (XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_POINTER_MOTION_HINT)

#define BUTTON1     (XCB_BUTTON_INDEX_1)
#define BUTTON2     (XCB_BUTTON_INDEX_2)
#define BUTTON3     (XCB_BUTTON_INDEX_3)
#define ASYNC       (XCB_GRAB_MODE_ASYNC)
#define SYNC        (XCB_GRAB_MODE_SYNC)

typedef unsigned int uint;
typedef unsigned char uchar;
typedef union Arg Arg;
typedef struct Bind Bind;
typedef struct Rule Rule;
typedef struct Panel Panel;
typedef struct WsRule WsRule;
typedef struct Client Client;
typedef struct Monitor Monitor;
typedef struct Workspace Workspace;


enum Borders {
	Width, Focus, Unfocus
};

enum Cursors {
	Normal, Move, Resize, CurLast
};

enum WMAtoms {
	Protocols, Delete, WMState, TakeFocus, Utf8Str, WMLast
};

enum NetAtoms {
	Supported,       Name,             State,          Check,
	Fullscreen,      NumDesktops,      CurrentDesktop, ActiveWindow,
	WindowType,      WindowTypeDialog, Desktop,        ClientList,
	DesktopViewport, DesktopGeometry,  DesktopNames,   WindowTypeDock,
	Strut,           StrutPartial,     FrameExtents,   NetLast
};

union Arg {
	int i;
	uint ui;
	float f;
	const void *v;
};

struct Bind {
	uint8_t type;
	uint mod;
	xcb_keysym_t keysym;
	void (*func)(const Arg *);
	const Arg arg;
};

struct Rule {
	char *regex;
	char *monitor;
	int workspace;
	uint floating;
	regex_t regcomp;
};

struct Panel {
	int x, y, w, h;
	int strut_l, strut_r, strut_t, strut_b;
	Panel *next;
	Monitor *mon;
	xcb_window_t win;
};

struct Client {
	int x, y, w, h, bw;
	int old_x, old_y, old_w, old_h, old_bw;
	int max_w, max_h, min_w, min_h;
	int base_w, base_h, increment_w, increment_h;
	float min_aspect, max_aspect;
	int sticky, fixed, floating, fullscreen, urgent, nofocus, oldstate;
	Client *next, *snext;
	Workspace *ws;
	xcb_window_t win;
};

struct WsRule {
	char *name;
	uint nmaster;
	float splitratio;
	void (*layout)(Workspace *);
};

struct Monitor {
	char *name;
	xcb_randr_output_t id;
	int x, y, w, h;
	int winarea_x, winarea_y, winarea_w, winarea_h;
	Monitor *next;
	Workspace *ws;
};

struct Workspace {
	char *name;
	uint nmaster;
	float splitratio;
	void (*layout)(Workspace *);
	uint num;
	Client *sel, *stack, *clients;
	Monitor *mon;
	Workspace *next;
};

static const char *cursors[] = {
	[Move] = "fleur", [Normal] = "arrow", [Resize] = "sizing"
};

static const char *wmatomnames[] = {
	[WMState] = "WM_STATE",
	[Utf8Str] = "UTF8_STRING",
	[Protocols] = "WM_PROTOCOLS",
	[TakeFocus] = "WM_TAKE_FOCUS",
	[Delete] = "WM_DELETE_WINDOW",
};

static const char *netatomnames[] = {
	[Name] = "_NET_WM_NAME",
	[State] = "_NET_WM_STATE",
	[Strut] = "_NET_WM_STRUT",
	[Desktop] = "_NET_WM_DESKTOP",
	[Supported] = "_NET_SUPPORTED",
	[ClientList] = "_NET_CLIENT_LIST",
	[Check] = "_NET_SUPPORTING_WM_CHECK",
	[WindowType] = "_NET_WM_WINDOW_TYPE",
	[FrameExtents] = "_NET_FRAME_EXTENTS",
	[DesktopNames] = "_NET_DESKTOP_NAMES",
	[ActiveWindow] = "_NET_ACTIVE_WINDOW",
	[StrutPartial] = "_NET_WM_STRUT_PARTIAL",
	[Fullscreen] = "_NET_WM_STATE_FULLSCREEN",
	[CurrentDesktop] = "_NET_CURRENT_DESKTOP",
	[NumDesktops] = "_NET_NUMBER_OF_DESKTOPS",
	[DesktopViewport] = "_NET_DESKTOP_VIEWPORT",
	[DesktopGeometry] = "_NET_DESKTOP_GEOMETRY",
	[WindowTypeDock] = "_NET_WM_WINDOW_TYPE_DOCK",
	[WindowTypeDialog] = "_NET_WM_WINDOW_TYPE_DIALOG",
};


static char *argv0;          /* program name */
static int scr_w, scr_h;     /* root window size */
static int randrbase = -1;   /* randr extension response */
static uint running = 1;     /* exit cleanly when 0 */
static uint numws = 0;       /* number of workspaces currently allocated */
static uint mousebtn = 0;    /* mouse button currently being pressed */
static uint numlockmask = 0; /* numlock modifier bit mask */

static Panel *panels;         /* panel linked list head */
static Monitor *monitors;     /* monitor linked list head */
static Workspace *selws;      /* selected workspace */
static Workspace *workspaces; /* workspace linked list head */

static xcb_screen_t *scr;            /* the X screen */
static xcb_connection_t *con;        /* xcb connection to the X server */
static xcb_window_t root, wmcheck;   /* root window and _NET_SUPPORTING_WM_CHECK window */
static xcb_key_symbols_t *keysyms;   /* current keymap symbols */
static xcb_atom_t wmatoms[WMLast];   /* _WM atoms used mostly internally */
static xcb_cursor_t cursor[CurLast]; /* cursors for moving, resizing, and normal */
static xcb_atom_t netatoms[NetLast]; /* _NET atoms used both internally and by other clients */


/* function prototypes */
static Client *nexttiled(Client *c);
static Client *wintoclient(xcb_window_t win);
static Monitor *initmon(char *name, xcb_randr_output_t id, int x, int y, int w, int h);
static Monitor *ptrtomon(int x, int y);
static Workspace *initws(uint num, char *name, uint nmaster, float splitratio, void (*layout)(Workspace *));
static Workspace *itows(uint num);
static Workspace *wintows(xcb_window_t win);
static char *itoa(int n, char *s);
static int grabpointer(xcb_cursor_t cursor);
static int pointerxy(int *x, int *y);
static int ruleregcmp(regex_t *r, char *class, char *inst);
static int sendevent(Client *c, int wmproto);
static int setsizehints(Client *c, int *x, int *y, int *w, int *h, int interact);
static int windowgeom(xcb_window_t win, int *x, int *y, int *w, int *h, int *bw);
static size_t strlcpy(char *dst, const char *src, size_t size);
static void *ecalloc(size_t elems, size_t size);
static void updatenumlock(void);
static void assignworkspaces(void);
static void attach(Client *c, int tohead);
static void attachstack(Client *c);
static void changefocus(const Arg *arg);
static void changews(Workspace *ws, int usermotion);
static void checkerror(char *prompt, xcb_generic_error_t *e);
static void clientrules(Client *c, xcb_window_t trans);
static void configure(Client *c);
static void detach(Client *c, int reattach);
static void detachstack(Client *c);
static void eventhandle(xcb_generic_event_t *ev);
static void eventloop(void);
static void fixupworkspaces(void);
static void focus(Client *c);
static void follow(const Arg *arg);
static void freeclient(Client *c, int destroyed);
static void freemon(Monitor *m);
static void freewm(void);
static void freews(Workspace *ws);
static void grabbuttons(Client *c, int focused);
static void grabkeys(void);
static void ignorefocusevents(void);
static void initatoms(xcb_atom_t *atoms, const char **names, int num);
static void initclient(xcb_window_t win, xcb_window_t trans);
static void initexisting(void);
static void initwm(void);
static void initworkspaces(void);
static void killclient(const Arg *arg);
static void layoutws(Workspace *ws, int allowfocusevents);
static void resetorquit(const Arg *arg);
static void resize(Client *c, int x, int y, int w, int h);
static void resizehint(Client *c, int x, int y, int w, int h, int interact);
static void restack(Workspace *ws);
static void runcmd(const Arg *arg);
static void send(const Arg *arg);
static void setclientws(Client *c, uint num);
static void setfocus(Client *c);
static void setfullscreen(Client *c, int fullscreen);
static void setlayout(const Arg *arg);
static void setnmaster(const Arg *arg);
static void setsplit(const Arg *arg);
static void setstackmode(xcb_window_t win, uint mode);
static void seturgency(Client *c, int urg);
static void setwinstate(xcb_window_t win, long state);
static void showhide(Client *c, int allowfocusevents);
static void sighandle(int);
static void sizehints(Client *c);
static void swapclient(const Arg *arg);
static void tile(Workspace *ws);
static void togglefloat(const Arg *arg);
static void unfocus(Client *c, int focusroot);
static void updatenumws(uint needed);
static void view(const Arg *arg);
static void windowhints(Client *c);
static void windowtype(Client *c);
static xcb_atom_t windowprop(xcb_window_t win, xcb_atom_t prop);
static xcb_get_window_attributes_reply_t *windowattr(xcb_window_t win);
static xcb_window_t windowtrans(xcb_window_t win);

/* extras */
#include "include/panel.c"
#include "include/randr.c"
#include "include/debug.c"

/* config last */
#include "config.h"
