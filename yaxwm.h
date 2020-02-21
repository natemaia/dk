/* yet another X window manager
* see license file for copyright and license details
* vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
*/

#ifndef VERSION
#define VERSION "0.1"
#endif

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
#define FOR_EACH(v, list)    for ((v) = (list); (v); (v) = (v)->next)
#define FOR_STACK(v, list)   for ((v) = (list); (v); (v) = (v)->snext)
#define FOR_CLIENTS(c, ws)   FOR_EACH((ws), workspaces) FOR_EACH((c), (ws)->clients)

/* find the last client in the list, tile version may not be the actual
 * tail but the last tiled client, will never be null unless the list is */
#define FIND_STAIL(v, list)\
	for ((v) = (list); (v) && (v)->snext; (v) = (v)->snext)
#define FIND_TAIL(v, list)\
	for ((v) = (list); (v) && (v)->next; (v) = (v)->next)
#define FIND_TILETAIL(v, list)\
	for ((v) = nexttiled((list)); (v) && nexttiled((v)->next); (v) = nexttiled((v)->next))

/* find the next client in the list (circular), when at the tail
 * we wrap around to the head, will never be null unless the list is */
#define FIND_SNEXT(v, cur, list)\
	(v) = (cur)->snext ? (cur)->snext : (list)
#define FIND_NEXT(v, cur, list)\
	(v) = (cur)->next ? (cur)->next : (list)
#define FIND_NEXTTILED(v, cur, list)\
	(v) = nexttiled((cur)->next) ? nexttiled((cur)->next) : nexttiled((list))

/* find the previous client in the list (circular), when at the head
 * we wrap around to the tail, will never be null unless the list is */
#define FIND_SPREV(v, cur, list)\
	for ((v) = (list); (v) && (v)->snext && (v)->snext != (cur); (v) = (v)->snext)
#define FIND_PREV(v, cur, list)\
	for ((v) = (list); (v) && (v)->next && (v)->next != (cur); (v) = (v)->next)
#define FIND_PREVTILED(v, cur, list)\
	for ((v) = nexttiled((list)); (v) && nexttiled((v)->next) && nexttiled((v)->next) != (cur); (v) = nexttiled((v)->next))

/* less wordy calls to change a windows property */
#define PROP_APPEND(win, atom, type, membsize, nmemb, value) \
	xcb_change_property(con, XCB_PROP_MODE_APPEND, (win), (atom), (type), (membsize), (nmemb), (value))
#define PROP_REPLACE(win, atom, type, membsize, nmemb, value) \
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, (win), (atom), (type), (membsize), (nmemb), (value))

/* shorter names for some xcb masks */
#define UNSET       (INT_MAX)
#define STICKY      (0xFFFFFFFF)
#define SYNC        (XCB_GRAB_MODE_SYNC)
#define ASYNC       (XCB_GRAB_MODE_ASYNC)
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

/* Don't use these, use resize() or resizehint() instead.
 * They're used internally to move clients off-screen without changing
 * their stored values (c->x, c->y) so we can later move them back
 * this is how windows are hidden/shown instead of mapping/unmapping them */
#define MOVE(win, x, y) xcb_configure_window(con, (win), XYMASK, (uint []){(x), (y)})
#define RESIZE(win, w, h) xcb_configure_window(con, (win), WHMASK, (uint []){(w), (h)})
#define MOVERESIZE(win, x, y, w, h) xcb_configure_window(con, (win), XYMASK | WHMASK, (uint []){(x), (y), (w), (h)})


typedef unsigned int uint;
typedef unsigned char uchar;
typedef union Arg Arg;

typedef struct Bind Bind;
typedef struct Rule Rule;
typedef struct WsRule WsRule;
typedef struct MouseBind MouseBind;

typedef struct Panel Panel;
typedef struct Client Client;
typedef struct Monitor Monitor;
typedef struct Workspace Workspace;

typedef struct Setting Setting;
typedef struct Keyword Keyword;

enum Opt {
	Std, Min, Ws, Lyt, List, Mouse, Col, Quit, Abs
};

enum Borders {
	Width, Focus, Unfocus
};

enum Gravity {
	Left, Right, Center, Top, Bottom,
};

union Arg {
	int i;
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

struct MouseBind {
	uint mod;
	xcb_button_t button;
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
	int num;
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

struct Setting {
	char *name;
	void (*func)(const Arg *);
	const void *opts;
};

struct Keyword {
	char *name;
	void (*func)(const Arg *);
	int requiresarg;
};

/* cursors used for normal operation, moving, and resizing */
enum Cursors {
	Normal, Move, Resize
};
static const char *cursors[] = {
	[Move] = "fleur",
	[Normal] = "arrow",
	[Resize] = "sizing"
};

/* supported WM_* atoms */
enum WMAtoms {
	Protocols, Delete, WMState, TakeFocus, Utf8Str
};
static const char *wmatomnames[] = {
	[Delete] = "WM_DELETE_WINDOW",
	[Protocols] = "WM_PROTOCOLS",
	[TakeFocus] = "WM_TAKE_FOCUS",
	[Utf8Str] = "UTF8_STRING",
	[WMState] = "WM_STATE",
};

/* supported _NET_* atoms */
enum NetAtoms {
	Supported,       Name,             State,            Check,           Fullscreen,
	ActiveWindow,    WindowType,       WindowTypeDialog, WindowTypeDock,  FrameExtents,
	Desktop,         CurrentDesktop,   NumDesktops,      DesktopViewport, DesktopGeometry,
	DesktopNames,    ClientList,       Strut,            StrutPartial,    WorkArea,
};
static const char *netatomnames[] = {
	[ActiveWindow] = "_NET_ACTIVE_WINDOW",
	[Check] = "_NET_SUPPORTING_WM_CHECK",
	[ClientList] = "_NET_CLIENT_LIST",
	[CurrentDesktop] = "_NET_CURRENT_DESKTOP",
	[DesktopGeometry] = "_NET_DESKTOP_GEOMETRY",
	[DesktopNames] = "_NET_DESKTOP_NAMES",
	[DesktopViewport] = "_NET_DESKTOP_VIEWPORT",
	[Desktop] = "_NET_WM_DESKTOP",
	[FrameExtents] = "_NET_FRAME_EXTENTS",
	[Fullscreen] = "_NET_WM_STATE_FULLSCREEN",
	[Name] = "_NET_WM_NAME",
	[NumDesktops] = "_NET_NUMBER_OF_DESKTOPS",
	[State] = "_NET_WM_STATE",
	[StrutPartial] = "_NET_WM_STRUT_PARTIAL",
	[Strut] = "_NET_WM_STRUT",
	[Supported] = "_NET_SUPPORTED",
	[WindowTypeDialog] = "_NET_WM_WINDOW_TYPE_DIALOG",
	[WindowTypeDock] = "_NET_WM_WINDOW_TYPE_DOCK",
	[WindowType] = "_NET_WM_WINDOW_TYPE",
	[WorkArea] = "_NET_WORKAREA",
};


int adjbdorgap(int i, char *opt, int changing, int other);
void adjmvfocus(const Arg *arg, void (*fn)(const Arg *));
void assignworkspaces(void);
void attach(Client *c, int tohead);
void attachpanel(Panel *p);
void attachstack(Client *c);
void changefocus(const Arg *arg);
void changews(Workspace *ws, int usermotion);
void checkerror(char *prompt, xcb_generic_error_t *e);
void *clientrules(Client *c, xcb_window_t *trans);
void cmdborder(const Arg *arg);
void cmdcolour(const Arg *arg);
void cmdexec(const Arg *arg);
void cmdexec(const Arg *arg);
void cmdfloat(const Arg *arg);
void cmdfocus(const Arg *arg);
void cmdgappx(const Arg *arg);
void cmdkill(const Arg *arg);
void cmdlayout(const Arg *arg);
void cmdmouse(const Arg *arg);
void cmdmove(const Arg *arg);
void cmdnmaster(const Arg *arg);
void cmdnstack(const Arg *arg);
char *cmdoptparse(char **argv, char **opts, int *argi);
void cmdparse(char *buf);
void cmdquit(const Arg *arg);
void cmdsetting(const Arg *arg);
void cmdsplit(const Arg *arg);
void cmdswap(const Arg *arg);
void cmdws(const Arg *arg);
void configure(Client *c);
Client *coordtoclient(int x, int y);
Monitor *coordtomon(int x, int y);
void detach(Client *c, int reattach);
void detachstack(Client *c);
void *ecalloc(size_t elems, size_t size);
void eventhandle(xcb_generic_event_t *ev);
void eventignore(uint8_t type);
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
void initatoms(xcb_atom_t *atoms, const char **names, int num);
void initclient(xcb_window_t win, xcb_window_t trans);
Monitor *initmon(char *name, xcb_randr_output_t id, int x, int y, int w, int h);
void initpanel(xcb_window_t win);
int initrandr(void);
void initscan(void);
void initwm(void);
void initworkspaces(void);
Workspace *initws(int num, WsRule *r);
char *itoa(int n, char *s);
Workspace *itows(int num);
void layoutws(Workspace *ws);
char *masktomods(uint mask, char *out, int outsize);
void monocle(Workspace *ws);
void mousemotion(xcb_button_t b);
void mousemoveresize(const Arg *arg);
void movestack(const Arg *arg);
Client *nexttiled(Client *c);
Monitor *outputtomon(xcb_randr_output_t id);
void printbind(xcb_generic_event_t *e, uint modmask, xcb_keysym_t keysym);
void print(const char *fmt, ...);
int querypointer(int *x, int *y);
Monitor *randrclone(xcb_randr_output_t id, int x, int y);
void resize(Client *c, int x, int y, int w, int h, int bw);
void resizehint(Client *c, int x, int y, int w, int h, int bw, int interact);
void restack(Workspace *ws);
int rulecmp(regex_t *r, char *class, char *inst);
void send(const Arg *arg);
int sendevent(Client *c, int wmproto);
void setborderpx(const Arg *arg);
void setclientws(Client *c, uint num);
void setfocus(Client *c);
void setfullscreen(Client *c, int fullscreen);
void setgappx(const Arg *arg);
void setlayout(const Arg *arg);
void setnetworkareavp(void);
void setnmaster(const Arg *arg);
void setnstack(const Arg *arg);
void setsplit(const Arg *arg);
void setstackmode(xcb_window_t win, uint mode);
void seturgency(Client *c, int urg);
void setwinstate(xcb_window_t win, uint32_t state);
void setwinvis(Client *c, Panel *p, int visible);
void showhide(Client *c);
void sighandle(int);
void sizehints(Client *c);
size_t strlcat(char *dst, const char *src, size_t size);
size_t strlcpy(char *dst, const char *src, size_t size);
void tile(Workspace *ws);
void unfocus(Client *c, int focusroot);
void ungrabpointer(void);
void updatenumlock(void);
void updatenumws(int needed);
int updateoutputs(xcb_randr_output_t *outputs, int len, xcb_timestamp_t timestamp);
int updaterandr(void);
void updatestruts(Panel *p, int apply);
int usesizehints(Client *c, int *x, int *y, int *w, int *h, int interact);
void view(const Arg *arg);
xcb_get_window_attributes_reply_t *winattr(xcb_window_t win);
void winhints(Client *c);
xcb_atom_t winprop(xcb_window_t win, xcb_atom_t prop);
int wintextprop(xcb_window_t w, xcb_atom_t atom, char *text, size_t size);
Client *wintoclient(xcb_window_t win);
Panel *wintopanel(xcb_window_t win);
Workspace *wintows(xcb_window_t win);
xcb_window_t wintrans(xcb_window_t win);
void wintype(Client *c);

