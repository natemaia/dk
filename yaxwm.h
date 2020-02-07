/* yet another X window manager
* see license file for copyright and license details
* vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
*/
#ifndef VERSION
#define VERSION "0.1"
#endif

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

/* bog standard */
#define W(x)       ((x)->w + 2 * (x)->bw)
#define H(x)       ((x)->h + 2 * (x)->bw)
#define MAX(a, b)  ((a) > (b) ? (a) : (b))
#define MIN(a, b)  ((a) < (b) ? (a) : (b))
#define LEN(x)     (sizeof(x) / sizeof(x[0]))
/* common short names for some modifiers and buttons */
#define ALT         (XCB_MOD_MASK_1)
#define SUPER       (XCB_MOD_MASK_4)
#define SHIFT       (XCB_MOD_MASK_SHIFT)
#define CTRL        (XCB_MOD_MASK_CONTROL)
#define BUTTON1     (XCB_BUTTON_INDEX_1)
#define BUTTON2     (XCB_BUTTON_INDEX_2)
#define BUTTON3     (XCB_BUTTON_INDEX_3)
#define PRESS       (XCB_KEY_PRESS)
#define RELEASE     (XCB_KEY_RELEASE)
/* dedicated media keys on many keyboards */
#define MUTE        (0x1008ff12)
#define VOLUP       (0x1008ff13)
#define VOLDOWN     (0x1008ff11)
/* linked list quick access */
#define FOR_EACH(v, list)      for ((v) = (list); (v); (v) = (v)->next)
#define FOR_STACK(v, list)     for ((v) = (list); (v); (v) = (v)->snext)
#define FOR_TAIL(v, list)      for ((v) = (list); (v) && (v)->next; (v) = (v)->next)
#define FOR_WSCLIENTS(c, ws)   FOR_EACH((ws), workspaces) FOR_EACH((c), (ws)->clients)
#define FOR_WSHIDDEN(c, ws)    FOR_EACH((ws), workspaces) FOR_EACH((c), (ws)->hidden)
#define FOR_PREV(v, cur, list) for ((v) = (list); (v) && (v)->next && (v)->next != (cur); (v) = (v)->next)
/* dissolves into nothing when DEBUG isn't defined */
#define DBG(fmt, ...)
#define DBGBIND(event, mod, sym)
/* less wordy calls to change a windows property */
#define PROP_APPEND(win, atom, type, membsize, nmemb, value) \
	xcb_change_property(con, XCB_PROP_MODE_APPEND, (win), (atom), (type), (membsize), (nmemb), (value))
#define PROP_REPLACE(win, atom, type, membsize, nmemb, value) \
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, (win), (atom), (type), (membsize), (nmemb), (value))
/* shorter names for some xcb masks */
#define STICKYMASK  (0xFFFFFFFF)
#define CLNMOD(mod) (mod & ~(numlockmask | XCB_MOD_MASK_LOCK))
#define BWMASK      (XCB_CONFIG_WINDOW_BORDER_WIDTH)
#define XYMASK      (XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y)
#define WHMASK      (XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT)
#define BUTTONMASK  (XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE)
#define GRABMASK    (XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_POINTER_MOTION_HINT)
#define CLIENTMASK  (XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_FOCUS_CHANGE \
		| XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY)
#define ROOTMASK    (XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY \
		| XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION \
		| XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_STRUCTURE_NOTIFY \
		| XCB_EVENT_MASK_PROPERTY_CHANGE)
#define ASYNC       (XCB_GRAB_MODE_ASYNC)
#define SYNC        (XCB_GRAB_MODE_SYNC)

/* Don't use this, use resize() or resizehint() instead.
 * It's used internally to move clients off-screen without changing
 * their stored values (c->x, c->y) so we can later move them back
 * this is how windows are hidden/shown instead of mapping/unmapping them */
#define MOVE(win, x, y) xcb_configure_window(con, (win), XYMASK, (uint []){(x), (y)})

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
	Width, Default, Focus, Unfocus
};

enum Cursors {
	Normal, Move, Resize
};

enum WMAtoms {
	Protocols, Delete, WMState, ChangeState, TakeFocus, Utf8Str
};

enum NetAtoms {
	Supported,       Name,             State,          Check,
	Fullscreen,      NumDesktops,      CurrentDesktop, ActiveWindow,
	WindowType,      WindowTypeDialog, Desktop,        ClientList,
	DesktopViewport, DesktopGeometry,  DesktopNames,   WindowTypeDock,
	Strut,           StrutPartial,     FrameExtents,   Hidden,
};

enum Gravity {
	Left, Right, Center, Top, Bottom,
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
	void (*cbfunc)(Client *c);
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
	uint nstack;
	uint gappx;
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
	uint num;
	char *name;
	uint nmaster;
	uint nstack;
	uint gappx;
	float splitratio;
	void (*layout)(Workspace *);
	Monitor *mon;
	Workspace *next;
	Client *sel, *stack, *clients, *hidden;
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
	[ChangeState] = "WM_CHANGE_STATE",
};

static const char *netatomnames[] = {
	[Name] = "_NET_WM_NAME",
	[State] = "_NET_WM_STATE",
	[Strut] = "_NET_WM_STRUT",
	[Desktop] = "_NET_WM_DESKTOP",
	[Supported] = "_NET_SUPPORTED",
	[Hidden] = "_NET_WM_STATE_HIDDEN",
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

char *argv0;                            /* program name */
int scr_w, scr_h;                       /* root window size */
int randrbase = -1;                     /* randr extension response */
uint running = 1;                       /* continue handling events */
uint numws = 0;                         /* number of workspaces currently allocated */
uint mousebtn = 0;                      /* mouse button currently being pressed */
uint numlockmask = 0;                   /* numlock modifier bit mask */
Panel *panels, *hidden;                 /* panel linked list head */
Monitor *monitors;                      /* monitor linked list head */
Workspace *selws;                       /* selected workspace */
Workspace *workspaces;                  /* workspace linked list head */
xcb_screen_t *scr;                      /* the X screen */
xcb_connection_t *con;                  /* xcb connection to the X server */
xcb_window_t root, wmcheck;             /* root window and _NET_SUPPORTING_WM_CHECK window */
xcb_key_symbols_t *keysyms;             /* current keymap symbols */
xcb_cursor_t cursor[LEN(cursors)];      /* cursors for moving, resizing, and normal */
xcb_atom_t wmatoms[LEN(wmatomnames)];   /* _WM atoms used mostly internally */
xcb_atom_t netatoms[LEN(netatomnames)]; /* _NET atoms used both internally and by other clients */

/* function prototypes */
void assignworkspaces(void);
void attach(Client *c, int tohead);
void attachpanel(Panel *p);
void attachstack(Client *c);
void changefocus(const Arg *arg);
void changews(Workspace *ws, int usermotion);
void checkerror(char *prompt, xcb_generic_error_t *e);
void *clientrules(Client *c, xcb_window_t *trans);
void configure(Client *c);
void detach(Client *c, int reattach);
void detachstack(Client *c);
void *ecalloc(size_t elems, size_t size);
void eventhandle(xcb_generic_event_t *ev);
void eventloop(void);
void fixupworkspaces(void);
void focus(Client *c);
void follow(const Arg *arg);
void freeclient(Client *c, int destroyed);
void freemon(Monitor *m);
void freepanel(Panel *panel, int destroyed);
void freewm(void);
void freews(Workspace *ws);
void grabbuttons(Client *c, int focused);
void grabkeys(void);
int grabpointer(xcb_cursor_t cursor);
void gravitate(Client *c, int vert, int horz, int matchgap);
void hideclient(Client *c, int hide);
void hidepanel(Panel *p, int hide);
void ignorefocusevents(void);
void initatoms(xcb_atom_t *atoms, const char **names, int num);
void initclient(xcb_window_t win, xcb_window_t trans);
void initexisting(void);
Monitor *initmon(char *name, xcb_randr_output_t id, int x, int y, int w, int h);
void initpanel(xcb_window_t win);
int initrandr(void);
void initwm(void);
void initworkspaces(void);
Workspace *initws(uint num, WsRule *r);
char *itoa(int n, char *s);
Workspace *itows(uint num);
void killclient(const Arg *arg);
void layoutws(Workspace *ws);
void monocle(Workspace *ws);
void mousemotion(xcb_button_t b);
Client *nexttiled(Client *c);
Monitor *outputtomon(xcb_randr_output_t id);
int pointerxy(int *x, int *y);
Monitor *ptrtomon(int x, int y);
Monitor *randrclone(xcb_randr_output_t id, int x, int y);
void resetorquit(const Arg *arg);
void resize(Client *c, int x, int y, int w, int h, int bw);
void resizehint(Client *c, int x, int y, int w, int h, int bw, int interact);
void restack(Workspace *ws);
int ruleregcmp(regex_t *r, char *class, char *inst);
void runcmd(const Arg *arg);
void send(const Arg *arg);
int sendevent(Client *c, int wmproto);
void setborderpx(const Arg *arg);
void setclientws(Client *c, uint num);
void setfocus(Client *c);
void setfullscreen(Client *c, int fullscreen);
void setgappx(const Arg *arg);
void setlayout(const Arg *arg);
void setnetwinstate(xcb_window_t win, long state);
void setnmaster(const Arg *arg);
void setnstack(const Arg *arg);
int setsizehints(Client *c, int *x, int *y, int *w, int *h, int interact);
void setsplit(const Arg *arg);
void setstackmode(xcb_window_t win, uint mode);
void seturgency(Client *c, int urg);
void setwinstate(xcb_window_t win, long state);
void showhide(Client *c);
void sighandle(int);
void sizehints(Client *c);
size_t strlcpy(char *dst, const char *src, size_t size);
void swapclient(const Arg *arg);
void tile(Workspace *ws);
void togglefloat(const Arg *arg);
void unfocus(Client *c, int focusroot);
void updatenumlock(void);
void updatenumws(uint needed);
int updateoutputs(xcb_randr_output_t *outputs, int len, xcb_timestamp_t timestamp);
int updaterandr(void);
void updatestruts(Panel *p, int apply);
void view(const Arg *arg);
xcb_get_window_attributes_reply_t *windowattr(xcb_window_t win);
void windowhints(Client *c);
xcb_atom_t windowprop(xcb_window_t win, xcb_atom_t prop);
xcb_window_t windowtrans(xcb_window_t win);
void windowtype(Client *c);
Client *wintoclient(xcb_window_t win);
Panel *wintopanel(xcb_window_t win);
Workspace *wintows(xcb_window_t win);

#include "debug.c"

/* config needs access to everything defined */
#include "config.h"
