#define W(x)          ((x)->w + 2 * (x)->bw)
#define H(x)          ((x)->h + 2 * (x)->bw)
#define MAX(a, b)     ((a) > (b) ? (a) : (b))
#define MIN(a, b)     ((a) < (b) ? (a) : (b))
#define EVTYPE(e)     (e->response_type & 0x7f)
#define EVISSEND(e)   (e->response_type & ~0x7f)
#define LEN(x)        (sizeof(x) / sizeof(x[0]))
#define CLNMOD(m)     (m & ~(numlockmask|XCB_MOD_MASK_LOCK))
#define BUTTONMASK    XCB_EVENT_MASK_BUTTON_PRESS|XCB_EVENT_MASK_BUTTON_RELEASE
#define MOTIONMASK    XCB_EVENT_MASK_BUTTON_RELEASE|XCB_EVENT_MASK_BUTTON_MOTION|XCB_EVENT_MASK_POINTER_MOTION_HINT

typedef unsigned int uint;
typedef unsigned char uchar;

typedef union Arg Arg;
typedef struct Key Key;
typedef struct Rule Rule;
typedef struct Client Client;
typedef struct Layout Layout;
typedef struct Monitor Monitor;

enum { /* cursors */
	Normal, Move, Resize, CurLast
};

enum { /* WM atoms */
	WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast
};

enum { /* EWMH atoms */
	NetSupported, NetWMName, NetWMState, NetWMCheck, NetWMFullscreen, NetNumDesktops,
	NetCurrentDesktop, NetActiveWindow, NetWMWindowType, NetWMWindowTypeDialog,
	NetWMDesktop, NetClientList, NetDesktopViewport, NetLast
};

union Arg {
	int i;
	uint ui;
	float f;
	const void *v;
};

struct Key {
	int type;
	uint mod;
	xcb_keysym_t keysym;
	void (*func)(const Arg *);
	const Arg arg;
};

struct Rule {
	char *regex;
	int workspace, floating;
	uint mon;
	regex_t regcomp;
};

struct Client {
	int x, y, w, h, bw;
	uint workspace;
	float min_aspect, max_aspect;
	int old_x, old_y, old_w, old_h, old_bw;
	int base_w, base_h, increment_w, increment_h, max_w, max_h, min_w, min_h;
	int fixed, floating, fullscreen, urgent, nofocus, oldstate;
	Client *next, *snext;
	Monitor *mon;
	xcb_window_t win;
};

struct Layout {
	void (*func)(Monitor *);
};

struct Monitor {
	int x, y, w, h;
	float splitratio;
	uint num, workspace, nmaster;
	int winarea_x, winarea_y, winarea_w, winarea_h;
	Client *clients, *stack, *sel;
	Monitor *next;
	const Layout *layout;
};

static const char *cursors[] = {
	[Move] = "fleur",
	[Normal] = "arrow",
	[Resize] = "sizing"
};

static const char *wmatomnames[] = {
	[WMState] = "WM_STATE",
	[WMDelete] = "WM_DELETE_WINDOW",
	[WMProtocols] = "WM_PROTOCOLS",
	[WMTakeFocus] = "WM_TAKE_FOCUS"
};

static const char *netatomnames[] = {
	[NetWMName] = "_NET_WM_NAME",
	[NetWMState] = "_NET_WM_STATE",
	[NetSupported] = "_NET_SUPPORTED",
	[NetWMDesktop] = "_NET_WM_DESKTOP",
	[NetClientList] = "_NET_CLIENT_LIST",
	[NetActiveWindow] = "_NET_ACTIVE_WINDOW",
	[NetWMCheck] = "_NET_SUPPORTING_WM_CHECK",
	[NetWMWindowType] = "_NET_WM_WINDOW_TYPE",
	[NetCurrentDesktop] = "_NET_CURRENT_DESKTOP",
	[NetNumDesktops] = "_NET_NUMBER_OF_DESKTOPS",
	[NetDesktopViewport] = "_NET_DESKTOP_VIEWPORT",
	[NetWMFullscreen] = "_NET_WM_STATE_FULLSCREEN",
	[NetWMWindowTypeDialog] = "_NET_WM_WINDOW_TYPE_DIALOG",
};

static int scr_w;
static int scr_h;
static char *argv0;
static xcb_screen_t *scr;
static xcb_connection_t *con;
static Monitor *mons, *selmon;
static xcb_window_t root, wmcheck;
static xcb_key_symbols_t *keysyms;
static xcb_cursor_t cursor[CurLast];
static uint running = 1, numlockmask = 0;
static xcb_atom_t wmatoms[WMLast], netatoms[NetLast];

/* interactive functions usually called from bindings */
static void follow(const Arg *arg);
static void killclient(const Arg *arg);
static void resetorquit(const Arg *arg);
static void runcmd(const Arg *arg);
static void send(const Arg *arg);
static void setfocus(const Arg *arg);
static void setlayout(const Arg *arg);
static void setnmaster(const Arg *arg);
static void setsplit(const Arg *arg);
static void swapclient(const Arg *arg);
static void togglefloat(const Arg *arg);
static void view(const Arg *arg);

/* internal functions */
static void attach(Client *c);
static void attachstack(Client *c);
static void freewm(void);
static void configure(Client *c);
static void clientrules(Client *c);
static int clientwmprotoexists(Client *c, xcb_atom_t proto);
static void detach(Client *c, int reattach);
static void detachstack(Client *c);
static void eventloop(void);
static void initexisting(void);
static void focus(Client *c);
static void focusclient(Client *c);
static void freeclient(Client *c, int destroyed);
static void freemonitor(Monitor *m);
static void geometry(Client *c);
static int grabpointer(xcb_cursor_t cursor);
static void initatoms(xcb_atom_t *atoms, const char **names, int num);
static void initbinds(int onlykeys);
static void initclient(xcb_window_t win);
static void initscreen(void);
static Monitor *initmon(int num);
static int initmons(void);
static void initwm(void);
static void layoutmon(Monitor *m);
static Client *nexttiled(Client *c);
static int pointerxy(int *x, int *y);
static Monitor *ptrtomon(int x, int y);
static void resize(Client *c, int x, int y, int w, int h);
static void resizehint(Client *c, int x, int y, int w, int h, int interact);
static void restack(Monitor *m);
static int sendevent(Client *c, xcb_atom_t proto);
static void setclientdesktop(Client *c, int num);
static void setclientstate(Client *c, long state);
static void setcurdesktop(void);
static void setfield(int *dst, int val, int *old);
static void setfullscreen(Client *c, int fullscreen);
static int setsizehints(Client *c, int *x, int *y, int *w, int *h, int interact);
static void showhide(Client *c);
static void sigchld(int unused);
static void sizehints(Client *c);
static void tile(Monitor *m);
static void unfocus(Client *c, int focusroot);
static void updateclientlist(void);
static xcb_get_window_attributes_reply_t *windowattr(xcb_window_t win);
static xcb_atom_t windowprop(xcb_window_t win, xcb_atom_t prop);
static xcb_window_t windowtrans(xcb_window_t win, Client *c);
static void windowtype(Client *c);
static Client *wintoclient(xcb_window_t win);
static Monitor *wintomon(xcb_window_t win);
static void wmhints(Client *c);
static int xcbeventerr(xcb_generic_event_t *ev);

#ifdef DEBUG
static void dprint(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
}
#define DBG(fmt, ...) dprint("%s:%d - " fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#else
#define DBG(fmt, ...)
#endif
