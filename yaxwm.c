#include <err.h>
#include <stdio.h>
#include <regex.h>
#include <signal.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <locale.h>
#include <sys/wait.h>

#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xcb_util.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_cursor.h>
#include <xcb/xcb_keysyms.h>
#include <X11/keysym.h>

#define W(x)          ((x)->w + 2 * (x)->bw)
#define H(x)          ((x)->h + 2 * (x)->bw)
#define MAX(a, b)     ((a) > (b) ? (a) : (b))
#define MIN(a, b)     ((a) < (b) ? (a) : (b))
#define LEN(x)        (sizeof(x) / sizeof(x[0]))
#define CLNMOD(m)     (m & ~(numlockmask|XCB_MOD_MASK_LOCK))
#define BUTTONMASK    XCB_EVENT_MASK_BUTTON_PRESS|XCB_EVENT_MASK_BUTTON_RELEASE
#define MOTIONMASK    XCB_EVENT_MASK_BUTTON_RELEASE|XCB_EVENT_MASK_BUTTON_MOTION|XCB_EVENT_MASK_POINTER_MOTION_HINT

typedef union Arg Arg;
typedef struct Bind Bind;
typedef struct Rule Rule;
typedef unsigned int uint;
typedef unsigned char uchar;
typedef struct Client Client;
typedef struct Monitor Monitor;
typedef struct Workspace Workspace;

enum Borders { Width, Focus, Unfocus };
enum Cursors { Normal, Move, Resize, CurLast };
enum WMAtoms { WMProtocols, WMDelete, WMState, WMTakeFocus, utf8str, WMLast };
enum NetAtoms { NetSupported, NetWMName, NetWMState, NetWMCheck, NetWMFullscreen,
	NetNumDesktops, NetCurrentDesktop, NetActiveWindow, NetWMWindowType,
	NetWMWindowTypeDialog, NetWMDesktop, NetClientList, NetDesktopViewport,
	NetDesktopGeometry, NetDesktopNames, NetWMWindowTypeDock, NetLast
};

union Arg {
	int i;
	uint ui;
	float f;
	const void *v;
};

struct Bind {
	uint type, mod;
	xcb_keysym_t keysym;
	void (*func)(const Arg *);
	const Arg arg;
};

struct Rule {
	char *regex;
	int workspace, floating;
	regex_t regcomp;
};

struct Client {
	/* geometry */
	int x, y, w, h, bw;
	int old_x, old_y, old_w, old_h, old_bw;

	/* size hints */
	int max_w, max_h;
	int min_w, min_h;
	int base_w, base_h;
	int increment_w, increment_h;
	float min_aspect, max_aspect;

	/* state */
	int fixed, floating, fullscreen, urgent, nofocus, oldstate;

	/* linkage */
	Client *next;
	Client *snext;
	Workspace *ws;
	xcb_window_t win;
};

struct Monitor {
	uint num;

	/* geometry */
	int x, y, w, h;
	int winarea_x, winarea_y, winarea_w, winarea_h;

	/* linkage */
	Monitor *next; /* next monitor - for looping */
	Workspace *ws; /* currently selected workspace on this monitor */
};

struct Workspace {
	uint num;

	/* configuration */
	char *name;
	uint nmaster;
	float ratio;
	void (*layout)(Workspace *);

	/* linkage */
	Client *clients; /* this workspace client list */
	Client *sel;     /* selected client on this workspace */
	Client *stack;   /* this workspace focus stack client list */
	Monitor *mon;    /* monitor this workspace is attached to */
	Workspace *next; /* next workspace - for looping */
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
	[WMTakeFocus] = "WM_TAKE_FOCUS",
	[utf8str] = "UTF8_STRING"
};

static const char *netatomnames[] = {
	[NetWMName] = "_NET_WM_NAME",
	[NetWMState] = "_NET_WM_STATE",
	[NetSupported] = "_NET_SUPPORTED",
	[NetWMDesktop] = "_NET_WM_DESKTOP",
	[NetClientList] = "_NET_CLIENT_LIST",
	[NetDesktopNames] = "_NET_DESKTOP_NAMES",
	[NetActiveWindow] = "_NET_ACTIVE_WINDOW",
	[NetWMCheck] = "_NET_SUPPORTING_WM_CHECK",
	[NetWMWindowType] = "_NET_WM_WINDOW_TYPE",
	[NetCurrentDesktop] = "_NET_CURRENT_DESKTOP",
	[NetNumDesktops] = "_NET_NUMBER_OF_DESKTOPS",
	[NetDesktopViewport] = "_NET_DESKTOP_VIEWPORT",
	[NetDesktopGeometry] = "_NET_DESKTOP_GEOMETRY",
	[NetWMFullscreen] = "_NET_WM_STATE_FULLSCREEN",
	[NetWMWindowTypeDock] = "_NET_WM_WINDOW_TYPE_DOCK",
	[NetWMWindowTypeDialog] = "_NET_WM_WINDOW_TYPE_DIALOG",
};

static char *argv0; /* argv[0] from main (program name) */
static Monitor *mons; /* monitor linked list head */
static int scr_w, scr_h; /* root window size */
static uint running = 1; /* exit cleanly when 0 */
static xcb_screen_t *scr; /* the X screen */
static uint numlockmask = 0; /* numlock modifier bit mask */
static xcb_connection_t *con; /* xcb connection to the X server */
static Workspace *wses, *selws; /* workspace linked list head and selected workspace */
static xcb_window_t root, wmcheck; /* root window and _NET_SUPPORTING_WM_CHECK window */
static xcb_key_symbols_t *keysyms; /* current keymap symbols */
static xcb_atom_t wmatoms[WMLast]; /* array of _WM atoms used mostly internally */
static xcb_cursor_t cursor[CurLast]; /* array of cursors for moving, resizing, and normal */
static xcb_atom_t netatoms[NetLast]; /* array of _NET atoms used both internally and by other clients */

/* function prototypes */
static Client *nexttiled(Client *c);
static Client *wintoclient(xcb_window_t win);
static Monitor *initmon(int num);
static Monitor *ptrtomon(int x, int y);
static Workspace *initws(char *name, int num);
static Workspace *itows(uint num);
static Workspace *wintows(xcb_window_t win);
static inline void *ecalloc(size_t nmemb, size_t size);
static inline void ignoreevent(int type);
static inline void setclientdesktop(Client *c, uint num);
static inline void setclientstate(Client *c, long state);
static inline void setfield(int *dst, int val, int *old);
static int grabpointer(xcb_cursor_t cursor);
static int initmons(void);
static int pointerxy(int *x, int *y);
static int sendevent(Client *c, int wmproto);
static int setsizehints(Client *c, int *x, int *y, int *w, int *h, int interact);
static int sstrlen(const char *str);
static void tile(Workspace *ws);
static void assignworkspaces(void);
static void attach(Client *c, int tohead);
static void changefocus(const Arg *arg);
static void clientrules(Client *c);
static void configure(Client *c);
static void detach(Client *c, int reattach);
static void eventloop(void);
static void focus(Client *c);
static void follow(const Arg *arg);
static void freeclient(Client *c, int destroyed);
static void freemon(Monitor *m);
static void freewm(void);
static void freews(Workspace *ws);
static void geometry(Client *c);
static void initatoms(xcb_atom_t *atoms, const char **names, int num);
static void initbinds(int onlykeys);
static void initclient(xcb_window_t win, xcb_window_t trans);
static void initexisting(void);
static void initscreen(void);
static void initwm(void);
static void initworkspaces(void);
static void killclient(const Arg *arg);
static void layoutws(Workspace *ws);
static void resetorquit(const Arg *arg);
static void resize(Client *c, int x, int y, int w, int h);
static void resizehint(Client *c, int x, int y, int w, int h, int interact);
static void restack(Workspace *ws);
static void runcmd(const Arg *arg);
static void send(const Arg *arg);
static void sendmon(Client *c, Monitor *m);
static void setfocus(Client *c);
static void setfullscreen(Client *c, int fullscreen);
static void setlayout(const Arg *arg);
static void setnmaster(const Arg *arg);
static void setsplit(const Arg *arg);
static void showhide(Client *c);
static void sigchld(int unused);
static void sizehints(Client *c);
static void swapclient(const Arg *arg);
static void togglefloat(const Arg *arg);
static void unfocus(Client *c, int focusroot);
static void view(const Arg *arg);
static void windowhints(Client *c);
static void windowtype(Client *c);
static xcb_atom_t windowprop(xcb_window_t win, xcb_atom_t prop);
static xcb_get_window_attributes_reply_t *windowattr(xcb_window_t win);
static xcb_window_t windowtrans(xcb_window_t win);

#ifdef DEBUG
static void print(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
}
#define DBG(fmt, ...) print("%s:%d - " fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#else
#define DBG(fmt, ...)
#endif

#include "config.h"

static void attach(Client *c, int tohead)
{
	Client *n;

	if (!tohead && (n = nexttiled(c->ws->clients))) {
		c->next = n->next;
		n->next = c;
	} else {
		c->next = c->ws->clients;
		c->ws->clients = c;
	}
}

static void attachstack(Client *c)
{
	c->snext = c->ws->stack;
	c->ws->stack = c;
}

static void assignworkspaces(void)
{
	Monitor *m;
	Workspace *ws;
	uint i, j, n = 0;

	for (m = mons; m; m = m->next)
		 ++n;
	if (!n)
		errx(1, "fatal error: no monitors available, exiting..");
	j = LEN(workspaces) / n;
	DBG("%d workspaces - %d per monitor", LEN(workspaces), j);
	for (m = mons, ws = wses; m; m = m->next)
		for (i = 0; ws && i < j; ++i, ws = ws->next) {
			ws->mon = m;
			DBG("workspace: %d - monitor: %d", ws->num, m->num);
			if (!i)
				m->ws = ws;
		}
	if (j * n != LEN(workspaces)) {
		DBG("leftovers after dividing between monitors, assigning one per monitor until exhausted");
		for (m = mons; ws; m = mons)
			while (ws && m) {
				DBG("workspace: %d - monitor: %d", ws->num, m->num);
				ws->mon = m;
				ws = ws->next;
				m = m->next;
			}
	}
}

static void changefocus(const Arg *arg)
{
	Client *c = NULL, *i;

	if (!selws->sel || selws->sel->fullscreen)
		return;
	DBG("finding %s client from window: %d", arg->i > 0 ? "next" : "previous", selws->sel->win);
	if (arg->i > 0)
		c = selws->sel->next ? selws->sel->next : selws->clients;
	else for (i = selws->clients; i != selws->sel; i = i->next)
		c = i;
	if (c) {
		DBG("found client window: %d", c->win);
		focus(c);
		restack(c->ws);
	} else {
		DBG("unable to find next client");
	}
}

static void configure(Client *c)
{
	xcb_configure_notify_event_t ce;

	DBG("sending configure notify event to client window: %d", c->win);
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

static void clientrules(Client *c)
{
	int ws;
	uint i;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t pc;
	xcb_icccm_get_wm_class_reply_t prop;

	pc = xcb_icccm_get_wm_class(con, c->win);
	DBG("setting client defaults and rule matching");
	c->floating = 0;
	if ((ws = windowprop(c->win, netatoms[NetWMDesktop])) < 0)
		ws = selws->num;
	DBG("client _NET_WM_DESKTOP: %d", ws);
	if (xcb_icccm_get_wm_class_reply(con, pc, &prop, &e) && prop.class_name && prop.instance_name) {
		DBG("got window class: %s - instance: %s", prop.class_name, prop.instance_name);
		for (i = 0; i < LEN(rules); i++)
			if (!regexec(&rules[i].regcomp, prop.class_name, 0, NULL, 0) || !regexec(&rules[i].regcomp, prop.instance_name, 0, NULL, 0)) {
				DBG("found matching rule");
				c->floating = rules[i].floating;
				if (rules[i].workspace >= 0)
					ws = rules[i].workspace;
				break;
			}
		xcb_icccm_get_wm_class_reply_wipe(&prop);
	} else if (e) {
		warnx("failed to get window class - X11 error: %d: %s", e->error_code, xcb_event_get_error_label(e->error_code));
		free(e);
	}
	setclientdesktop(c, ws);
	DBG("set client values - workspace: %d, floating: %d, monitor: %d", c->ws->num, c->floating, c->ws->mon->num);
}

static void detach(Client *c, int reattach)
{
	Client **tc = &c->ws->clients;

	while (*tc && *tc != c)
		tc = &(*tc)->next;
	*tc = c->next;
	if (reattach)
		attach(c, 1);
}

static void detachstack(Client *c)
{
	Client **tc = &c->ws->stack;

	while (*tc && *tc != c)
		tc = &(*tc)->snext;
	*tc = c->snext;
	if (c == c->ws->sel)
		c->ws->sel = c->ws->stack;
}

static inline void *ecalloc(size_t nmemb, size_t size)
{
	void *p;

	if (!(p = calloc(nmemb, size))) /* calloc(3) is crucial because it zeroes the memory, linked list ->next will always be NULL */
		err(1, "unable to allocate space");
	return p;
}

static void eventloop(void)
{
	int x, y;
	Client *c;
	Monitor *m;
	Workspace *ws;
	uint i, mousebtn = 0;
	xcb_generic_event_t *ev;
	xcb_generic_error_t *err;

	xcb_aux_sync(con);
	while (running && (ev = xcb_wait_for_event(con)) != NULL) {
		switch (XCB_EVENT_RESPONSE_TYPE(ev)) {
			case XCB_FOCUS_IN:
			{
				xcb_focus_in_event_t *e = (xcb_focus_in_event_t *)ev;

				if (selws->sel && e->event != selws->sel->win) {
					DBG("focus in event for window: %d - focusing selected window: %d", e->event, selws->sel->win);
					setfocus(selws->sel);
				}
				break;
			}
			case XCB_CONFIGURE_NOTIFY:
			{
				xcb_configure_notify_event_t *e = (xcb_configure_notify_event_t *)ev;

				if (e->window == root) {
					DBG("root window configure notify event");
					i = (scr_h != e->height || scr_w != e->width);
					scr_w = e->width;
					scr_h = e->height;
					if (initmons() || i) {
						xcb_change_property(con, XCB_PROP_MODE_REPLACE, root,
								netatoms[NetDesktopGeometry], XCB_ATOM_CARDINAL, 32, 2, (uint32_t []){scr_w, scr_h});
						assignworkspaces();
						for (ws = wses; ws; ws = ws->next)
							for (c = ws->clients; c; c = c->next)
								if (c->fullscreen)
									resize(c, ws->mon->x, ws->mon->y, ws->mon->w, ws->mon->h);
						focus(NULL);
						layoutws(NULL);
					}
				}
				break;
			}
			case XCB_MAPPING_NOTIFY:
			{
				xcb_mapping_notify_event_t *e = (xcb_mapping_notify_event_t *)ev;

				if (e->request == XCB_MAPPING_KEYBOARD) {
					xcb_refresh_keyboard_mapping(keysyms, e);
					initbinds(1);
				}
				break;
			}
			case XCB_CONFIGURE_REQUEST:
			{
				uint16_t xy = XCB_CONFIG_WINDOW_X|XCB_CONFIG_WINDOW_Y;
				uint16_t wh = XCB_CONFIG_WINDOW_WIDTH|XCB_CONFIG_WINDOW_HEIGHT;
				xcb_configure_request_event_t *e = (xcb_configure_request_event_t *)ev;

				if ((c = wintoclient(e->window))) {
					DBG("configure request event for managed window: %d", e->window);
					if (e->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH)
						c->bw = e->border_width;
					else if (c->floating || !selws->layout) {
						m = c->ws->mon;
						if (e->value_mask & XCB_CONFIG_WINDOW_X)      setfield(&c->x, m->x + e->x, &c->old_x);
						if (e->value_mask & XCB_CONFIG_WINDOW_Y)      setfield(&c->y, m->y + e->y, &c->old_y);
						if (e->value_mask & XCB_CONFIG_WINDOW_WIDTH)  setfield(&c->w, e->width, &c->old_w);
						if (e->value_mask & XCB_CONFIG_WINDOW_HEIGHT) setfield(&c->h, e->height, &c->old_h);
						if ((c->x + c->w) > m->x + m->w)              setfield(&c->y, m->x + (m->w / 2 - c->w / 2), &c->old_y);
						if ((c->y + c->h) > m->y + m->h)              setfield(&c->y, m->y + (m->h / 2 - c->h / 2), &c->old_y);
						if ((e->value_mask & (xy)) && !(e->value_mask & (wh)))
							configure(c);
						if (c->ws == c->ws->mon->ws)
							resize(c, c->x, c->y, c->w, c->h);
					} else {
						configure(c);
					}
				} else {
					DBG("configure request event for unmanaged window: %d", e->window);
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
				xcb_aux_sync(con);
				break;
			}
			case XCB_DESTROY_NOTIFY:
			{
				xcb_destroy_notify_event_t *e = (xcb_destroy_notify_event_t *)ev;

				if ((c = wintoclient(e->window))) {
					DBG("destroy notify event - window: %d", c->win);
					freeclient(c, 1);
				}
				break;
			}
			case XCB_ENTER_NOTIFY:
			{
				xcb_enter_notify_event_t *e = (xcb_enter_notify_event_t *)ev;

				if (e->event != root && (e->mode != XCB_NOTIFY_MODE_NORMAL || e->detail == XCB_NOTIFY_DETAIL_INFERIOR))
					break;
				DBG("enter notify event - window: %d", e->event);
				c = wintoclient(e->event);
				if ((ws = c ? c->ws : wintows(e->event)) != selws) {
					unfocus(selws->sel, 1);
					selws = ws;
					selws->mon->ws = ws;
				} else if (!c || c == selws->sel)
					break;
				focus(c);
				break;
			}
			case XCB_BUTTON_PRESS:
			{
				xcb_button_press_event_t *b = (xcb_button_press_event_t *)ev;

				if (!b->child || b->child == root || !(c = selws->sel))
					break;
				if (b->detail == XCB_BUTTON_INDEX_1 || b->detail == XCB_BUTTON_INDEX_3) {
					DBG("button press event - button: %d", b->detail);
					restack(selws);
					if ((mousebtn = b->detail) == XCB_BUTTON_INDEX_1)
						xcb_warp_pointer(con, XCB_NONE, c->win, 0, 0, 0, 0, c->w/2, c->h/2);
					else
						xcb_warp_pointer(con, XCB_NONE, c->win, 0, 0, 0, 0, c->w, c->h);
					if (!grabpointer(cursor[b->detail == XCB_BUTTON_INDEX_1 ? Move : Resize]))
						break;
				} else if (b->detail == XCB_BUTTON_INDEX_2)
					togglefloat(NULL);
				break;
			}
			case XCB_BUTTON_RELEASE:
			{
				DBG("button release event, ungrabbing pointer");
				if ((err = xcb_request_check(con, xcb_ungrab_pointer_checked(con, XCB_CURRENT_TIME)))) {
					free(err);
					errx(1, "failed to ungrab pointer");
				}
				if (selws->sel && (m = ptrtomon(selws->sel->x + selws->sel->w / 2, selws->sel->y + selws->sel->h / 2)) != selws->mon) {
					sendmon(selws->sel, m);
					selws = m->ws;
					focus(NULL);
				}
				if (mousebtn == XCB_BUTTON_INDEX_3)
					ignoreevent(XCB_ENTER_NOTIFY);
				mousebtn = 0;
				break;
			}
			case XCB_MOTION_NOTIFY:
			{
				xcb_motion_notify_event_t *e = (xcb_motion_notify_event_t *)ev;

				(void)(e);
				if (!mousebtn || !(c = selws->sel) || c->fullscreen)
					break;
				DBG("motion notify event - window: %d - child: %d - coord: %d,%d", e->event, e->child, e->root_x, e->root_y);
				if (!c->floating && selws->layout)
					togglefloat(NULL);
				if (pointerxy(&x, &y) && (!selws->layout || c->floating)) {
					if (mousebtn == 1)
						resizehint(c, x - c->w / 2, y - c->h / 2, c->w, c->h, 1);
					else
						resizehint(c, c->x, c->y, x - c->x, y - c->y, 1);
				}
				break;
			}
			case XCB_KEY_RELEASE:
			{
				xcb_keysym_t sym;
				xcb_key_release_event_t *e = (xcb_key_release_event_t *)ev;

				sym = xcb_key_release_lookup_keysym(keysyms, e, 0);
				for (i = 0; i < LEN(binds); i++)
					if (sym == binds[i].keysym && binds[i].type == XCB_KEY_RELEASE && binds[i].func && CLNMOD(binds[i].mod) == CLNMOD(e->state)) {
						DBG("%s event - key: %u - mod: %u", xcb_event_get_label(ev->response_type), e->detail, CLNMOD(binds[i].mod));
						binds[i].func(&(binds[i].arg));
						break;
					}
				break;
			}
			case XCB_KEY_PRESS:
			{
				xcb_keysym_t sym;
				xcb_key_press_event_t *e = (xcb_key_press_event_t *)ev;

				sym = xcb_key_press_lookup_keysym(keysyms, e, 0);
				for (i = 0; i < LEN(binds); i++)
					if (sym == binds[i].keysym && binds[i].type == XCB_KEY_PRESS && binds[i].func && CLNMOD(binds[i].mod) == CLNMOD(e->state)) {
						DBG("%s event - key: %u - mod: %u", xcb_event_get_label(ev->response_type), e->detail, CLNMOD(binds[i].mod));
						binds[i].func(&(binds[i].arg));
						break;
					}
				break;
			}
			case XCB_MAP_REQUEST:
			{
				xcb_get_window_attributes_reply_t *wa;
				xcb_map_request_event_t *e = (xcb_map_request_event_t *)ev;

				DBG("map request event for window: %i", e->window);
				if (windowprop(e->window, netatoms[NetWMWindowType]) == netatoms[NetWMWindowTypeDock]) {
					fprintf(stderr, "map request window is type: _NET_WM_WINDOW_TYPE_DOCK");
					xcb_map_window(con, e->window);
					break;
				} else if ((wa = windowattr(e->window)) && !wa->override_redirect && !wintoclient(e->window))
					initclient(e->window, 0);
				free(wa);
				break;
			}
			case XCB_UNMAP_NOTIFY:
			{
				xcb_unmap_notify_event_t *e = (xcb_unmap_notify_event_t *)ev;

				if ((c = wintoclient(e->window))) {
					if (XCB_EVENT_SENT(e)) {
						DBG("unmap notify event resulted from a SendEvent for managed window: %i - setting state to withdrawn", e->window);
						setclientstate(c, XCB_ICCCM_WM_STATE_WITHDRAWN);
					} else {
						DBG("unmap notify event for managed window: %i - freeing", e->window);
						freeclient(c, 0);
					}
				}
				break;
			}
			case XCB_CLIENT_MESSAGE:
			{
				xcb_client_message_event_t *e = (xcb_client_message_event_t *)ev;

				if (!(c = wintoclient(e->window)))
					break;
				if (e->type == netatoms[NetWMState] && e->data.data32[1] == netatoms[NetWMFullscreen]) {
					setfullscreen(c, e->data.data32[0]);
				} else if (e->type == netatoms[NetActiveWindow]) {
					unfocus(selws->sel, 1);
					view(&(const Arg){.ui = c->ws->num});
					focus(c);
					restack(selws);
				}
				break;
			}
			case XCB_PROPERTY_NOTIFY:
			{
				xcb_window_t trans;
				xcb_property_notify_event_t *e = (xcb_property_notify_event_t *)ev;

				if (e->state != XCB_PROPERTY_DELETE && (c = wintoclient(e->window))) {
					if (e->atom == XCB_ATOM_WM_TRANSIENT_FOR) {
						if (!c->floating && (trans = windowtrans(c->win)) != XCB_NONE && (c->floating = (wintoclient(trans) != NULL)))
							layoutws(c->ws);
					} else if (e->atom == XCB_ATOM_WM_NORMAL_HINTS) {
						sizehints(c);
					} else if (e->atom == XCB_ATOM_WM_HINTS) {
						windowhints(c);
					}
					if (e->atom == netatoms[NetWMWindowType])
						windowtype(c);
				}
				break;
			}
			default:
			{
				xcb_generic_error_t *e = (xcb_generic_error_t *)ev;

				if (ev->response_type) {
					DBG("unhandled event: %s", xcb_event_get_label(ev->response_type));
					break;
				} else if (e->error_code == 3                                                     /* BadWindow */
						|| (e->error_code == 8  && (e->major_code == 42 || e->major_code == 12))  /* BadMatch & SetInputFocus/ConfigureWindow */
						|| (e->error_code == 10 && (e->major_code == 28 || e->major_code == 33))) /* BadAccess & GrabButton/GrabKey */
					break;

				/* TODO: some kind of error handling for those we don't want to (or shouldn't) ignore, similar to xlib? */
				warnx("event error: %d: \"%s\" - %d: \"%s\"", e->error_code, xcb_event_get_error_label(e->error_code),
						e->major_code, xcb_event_get_request_label(e->major_code));
				break;
			}
		}
		free(ev);
	}
}

static void focus(Client *c)
{
	if (!c || c->ws != c->ws->mon->ws) {
		DBG("passed null or client is on an inactive workspace, focusing current workspace stack");
		c = selws->stack;
	}
	if (selws->sel && selws->sel != c) {
		DBG("current workspace has an active client and it's not the client to focus, unfocus selected client");
		unfocus(selws->sel, 0);
	}
	if (c) {
		DBG("focusing client: %d", c->win);
		if (c->ws != selws) {
			selws = c->ws;
			selws->mon->ws = c->ws;
		}
		detachstack(c);
		attachstack(c);
		xcb_change_window_attributes(con, c->win, XCB_CW_BORDER_PIXEL, &borders[Focus]);
		setfocus(c);
	} else {
		DBG("focusing root window");
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
		xcb_delete_property(con, root, netatoms[NetActiveWindow]);
	}
	selws->sel = c;
}

static void follow(const Arg *arg)
{
	if (selws->sel && arg->ui != selws->num) {
		send(arg);
		view(arg);
	}
}

static void freeclient(Client *c, int destroyed)
{
	Client *n;
	Workspace *ws, *focusws = c->ws;

	DBG("freeing client: %d - destroyed: %i", c->win, destroyed);
	detach(c, 0);
	detachstack(c);
	if (!destroyed) {
		xcb_grab_server(con);
		xcb_configure_window(con, c->win, XCB_CONFIG_WINDOW_BORDER_WIDTH, &c->old_bw);
		setclientstate(c, XCB_ICCCM_WM_STATE_WITHDRAWN);
		xcb_aux_sync(con);
		xcb_ungrab_server(con);
	}
	free(c);
	focus(NULL);
	xcb_delete_property(con, root, netatoms[NetClientList]);
	for (ws = wses; ws; ws = ws-> next)
		for (n = ws->clients; n; n = n->next)
			xcb_change_property(con, XCB_PROP_MODE_APPEND, root, netatoms[NetClientList], XCB_ATOM_WINDOW, 32, 1, &n->win);
	layoutws(focusws);
}

static void freemon(Monitor *m)
{
	Monitor *mon;

	if (m == mons)
		mons = mons->next;
	else {
		for (mon = mons; mon && mon->next != m; mon = mon->next);
		mon->next = m->next;
	}
	DBG("freeing monitor: %d", m->num);
	free(m);
}

static void freewm(void)
{
	uint i;
	Workspace *ws;

	for (ws = wses; ws; ws = ws->next)
		while (ws->stack)
			freeclient(ws->stack, 0);
	xcb_ungrab_button(con, XCB_BUTTON_INDEX_ANY, root, XCB_MOD_MASK_ANY);
	xcb_ungrab_key(con, XCB_GRAB_ANY, root, XCB_MOD_MASK_ANY);
	xcb_key_symbols_free(keysyms);
	while (mons)
		freemon(mons);
	while (wses)
		freews(wses);
	for (i = 0; i < LEN(cursors); i++)
		xcb_free_cursor(con, cursor[i]);
	for (i = 0; i < LEN(rules); i++)
		regfree(&rules[i].regcomp);
	xcb_destroy_window(con, wmcheck);
	xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, XCB_INPUT_FOCUS_POINTER_ROOT, XCB_CURRENT_TIME);
	xcb_aux_sync(con);
	xcb_delete_property(con, root, netatoms[NetActiveWindow]);
	xcb_disconnect(con);
}

static void freews(Workspace *ws)
{
	Workspace *workspace;

	if (ws == wses)
		wses = wses->next;
	else {
		for (workspace = wses; workspace && workspace->next != ws; workspace = workspace->next);
		workspace->next = ws->next;
	}
	DBG("freeing workspace: %d", ws->num);
	free(ws);
}

static void geometry(Client *c)
{
	xcb_generic_error_t *e;
	xcb_get_geometry_reply_t *g;

	if ((g = xcb_get_geometry_reply(con, xcb_get_geometry(con, c->win), &e))) {
		DBG("using geometry given by the window");
		setfield(&c->w, g->width, &c->old_w);
		setfield(&c->h, g->height, &c->old_h);
		setfield(&c->x, g->x + c->ws->mon->x > 0 ? g->x + c->ws->mon->x : c->ws->mon->x + (c->ws->mon->w - c->w / 2), &c->old_x);
		setfield(&c->y, g->y + c->ws->mon->y > 0 ? g->y + c->ws->mon->y : c->ws->mon->y + (c->ws->mon->h - c->h / 2), &c->old_y);
		setfield(&c->bw, g->border_width, &c->old_bw);
		free(g);
	} else {
		if (e) {
			warnx("failed to get window geometry - X11 error: %d: %s", e->error_code, xcb_event_get_error_label(e->error_code));
			free(e);
		}
		setfield(&c->w, c->ws->mon->w / 2, &c->old_w);
		setfield(&c->h, c->ws->mon->h / 2, &c->old_h);
		setfield(&c->x, c->ws->mon->x + (c->ws->mon->w - c->w / 2), &c->old_x);
		setfield(&c->y, c->ws->mon->y + (c->ws->mon->h - c->h / 2), &c->old_y);
		setfield(&c->bw, borders[Width], &c->old_bw);
	}
	c->bw = borders[Width];
}

static int grabpointer(xcb_cursor_t cursor)
{
	int r = 0;
	xcb_generic_error_t *e;
	xcb_grab_pointer_cookie_t pc;
	xcb_grab_pointer_reply_t *ptr;

	pc = xcb_grab_pointer(con, 0, root, MOTIONMASK, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, root, cursor, XCB_CURRENT_TIME);
	if ((ptr = xcb_grab_pointer_reply(con, pc, &e))) {
		r = ptr->status == XCB_GRAB_STATUS_SUCCESS ? 1 : 0;
		free(ptr);
	} else if (e) {
		warnx("unable to grab pointer - X11 error: %d: %s", e->error_code, xcb_event_get_error_label(e->error_code));
		free(e);
	}
	return r;
}

static inline void ignoreevent(int type)
{
	xcb_generic_event_t *ev = NULL;

	while ((ev = xcb_poll_for_event(con)) && XCB_EVENT_RESPONSE_TYPE(ev) != type)
		;
	xcb_poll_for_event(con);
	free(ev);
}

static void initatoms(xcb_atom_t *atoms, const char **names, int num)
{
	int i;
	xcb_generic_error_t *e;
	xcb_intern_atom_reply_t *r;
	xcb_intern_atom_cookie_t c[num];

	for (i = 0; i < num; ++i)
		c[i] = xcb_intern_atom(con, 0, sstrlen(names[i]), names[i]);
	for (i = 0; i < num; ++i) {
		if ((r = xcb_intern_atom_reply(con, c[i], &e))) {
			DBG("initializing atom: %s - value: %d", names[i], r->atom);
			atoms[i] = r->atom;
			free(r);
		} else if (e) {
			warnx("unable to initialize atom: %s - X11 error: %d: %s", names[i], e->error_code, xcb_event_get_error_label(e->error_code));
			free(e);
		}
	}
}

static void initbinds(int onlykeys)
{
	uint i, j;
	xcb_generic_error_t *e;
	xcb_keycode_t *c, *t, *cd;
	xcb_get_modifier_mapping_reply_t *m;
	static const uint btns[] = { XCB_BUTTON_INDEX_1, XCB_BUTTON_INDEX_2, XCB_BUTTON_INDEX_3 };

	DBG("setting up numlock mod mask");
	if ((m = xcb_get_modifier_mapping_reply(con, xcb_get_modifier_mapping(con), &e))) {
		if ((t = xcb_key_symbols_get_keycode(keysyms, XK_Num_Lock)) && (cd = xcb_get_modifier_mapping_keycodes(m))) {
			for (i = 0; i < 8; i++)
				for (j = 0; j < m->keycodes_per_modifier; j++)
					if (cd[i * m->keycodes_per_modifier + j] == *t)
						numlockmask = (1 << i);
			free(t);
		}
		free(m);
	} else if (e) {
		warnx("unable to get modifier mapping for numlockmask - X11 error: %d: %s", e->error_code, xcb_event_get_error_label(e->error_code));
		free(e);
	}

	DBG("setting up bindings");
	uint mods[] = { 0, XCB_MOD_MASK_LOCK, numlockmask, numlockmask|XCB_MOD_MASK_LOCK };
	if (!onlykeys)
		xcb_ungrab_button(con, XCB_BUTTON_INDEX_ANY, root, XCB_MOD_MASK_ANY);
	xcb_ungrab_key(con, XCB_GRAB_ANY, root, XCB_MOD_MASK_ANY);
	for (i = 0; i < LEN(mods); i++) {
		if (!onlykeys)
			for (j = 0; j < LEN(btns); j++)
				xcb_grab_button(con, 0, root, BUTTONMASK, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_SYNC, 0, XCB_NONE, btns[j], MODKEY|mods[i]);
		for (j = 0; j < LEN(binds); j++)
			if ((c = xcb_key_symbols_get_keycode(keysyms, binds[j].keysym))) {
				xcb_grab_key(con, 1, root, binds[j].mod|mods[i], *c, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
				free(c);
			}
	}
}

static void initclient(xcb_window_t win, xcb_window_t trans)
{
	Monitor *m;
	Client *c, *t;
	xcb_window_t w = trans;
	uint32_t mask = XCB_EVENT_MASK_ENTER_WINDOW|XCB_EVENT_MASK_FOCUS_CHANGE|XCB_EVENT_MASK_PROPERTY_CHANGE|XCB_EVENT_MASK_STRUCTURE_NOTIFY;

	DBG("initializing new client from window: %d", win);
	c = ecalloc(1, sizeof(Client));
	c->win = win;
	if ((!w || w == XCB_WINDOW_NONE) && (w = windowtrans(c->win)) == XCB_WINDOW_NONE)
		clientrules(c);
	else if ((t = wintoclient(w))) {
		DBG("window is transient of managed client, setting workspace and monitor to match");
		c->ws = t->ws;
		c->floating = 1;
		setclientdesktop(c, c->ws->num);
	}
	m = c->ws->mon;
	geometry(c);
	if (c->x <= m->x || c->x + W(c) >= m->x + m->w)
		c->x = (m->x + m->w - W(c)) / 2;
	if (c->y <= m->y || c->y + H(c) >= m->y + m->h)
		c->y = (m->y + m->h - H(c)) / 2;
	xcb_configure_window(con, c->win, XCB_CONFIG_WINDOW_BORDER_WIDTH, &c->bw);
	configure(c);
	windowtype(c);
	sizehints(c);
	windowhints(c);
	xcb_change_window_attributes(con, c->win, XCB_CW_EVENT_MASK|XCB_CW_BORDER_PIXEL, (uint32_t []){borders[Focus], mask});
	if (c->floating || (c->floating = c->oldstate = c->fixed))
		xcb_configure_window(con, c->win, XCB_CONFIG_WINDOW_STACK_MODE, (uint32_t []){XCB_STACK_MODE_ABOVE});
	attach(c, 0);
	attachstack(c);
	xcb_change_property(con, XCB_PROP_MODE_APPEND, root, netatoms[NetClientList], XCB_ATOM_WINDOW, 32, 1, &c->win);
	setclientstate(c, XCB_ICCCM_WM_STATE_NORMAL);
	if (c->ws == selws && selws->sel)
		unfocus(selws->sel, 0);
	c->ws->sel = c;
	layoutws(c->ws);
	xcb_map_window(con, c->win);
	DBG("new client mapped: %d,%d @ %dx%d - floating: %d", c->x, c->y, c->w, c->h, c->floating);
	focus(NULL);
}

static void initexisting(void)
{
	uint i, num;
	xcb_window_t *wins;
	xcb_generic_error_t *e;
	xcb_query_tree_cookie_t c;
	xcb_query_tree_reply_t *tree;

	c = xcb_query_tree(con, root);
	tree = NULL;
	DBG("getting root window tree");
	if ((tree = xcb_query_tree_reply(con, c, &e))) {
		num = tree->children_len;
		wins = xcb_query_tree_children(tree);
		xcb_atom_t state[num];
		xcb_window_t trans[num];
		xcb_get_window_attributes_reply_t *wa[num];

		for (i = 0; i < num; i++) { /* non transient */
			trans[i] = state[i] = XCB_WINDOW_NONE;
			if (windowprop(wins[i], netatoms[NetWMWindowType]) == netatoms[NetWMWindowTypeDock])
				continue;
			if (!(wa[i] = windowattr(wins[i])) || wa[i]->override_redirect || (trans[i] = windowtrans(wins[i])) != XCB_WINDOW_NONE)
				continue;
			if (wa[i]->map_state == XCB_MAP_STATE_VIEWABLE || (state[i] = windowprop(wins[i], wmatoms[WMState])) == XCB_ICCCM_WM_STATE_ICONIC)
				initclient(wins[i], 0);
		}
		for (i = 0; i < num; i++) { /* transients */
			if (windowprop(wins[i], netatoms[NetWMWindowType]) == netatoms[NetWMWindowTypeDock])
				continue;
			if (wa[i] && trans[i] && (wa[i]->map_state == XCB_MAP_STATE_VIEWABLE || state[i] == XCB_ICCCM_WM_STATE_ICONIC))
				initclient(wins[i], trans[i]);
			free(wa[i]);
		}
		free(tree);
	} else if (e) {
		warnx("FATAL: unable to query tree from root window - X11 error: %d: %s", e->error_code, xcb_event_get_error_label(e->error_code));
		free(e);
		exit(1);
	}
}

static Monitor *initmon(int num)
{
	Monitor *m;

	DBG("initializing new monitor: %d", num);
	m = ecalloc(1, sizeof(Monitor));
	m->num = num;
	return m;
}

static int initmons(void)
{
	int dirty = 0, num = 0;

	DBG("updating monitor(s)");
	if (!mons)
		mons = initmon(num++);
	if (mons->w != scr_w || mons->h != scr_h) {
		dirty = 1;
		mons->w = mons->winarea_w = scr_w;
		mons->h = mons->winarea_h = scr_h;
	}
	return dirty;
}

static void initscreen(void)
{
	if (!(scr = xcb_setup_roots_iterator(xcb_get_setup(con)).data))
		errx(1, "error getting default screen from X connection");
	root = scr->root;
	scr_w = scr->width_in_pixels;
	scr_h = scr->height_in_pixels;
	DBG("initialized root window: %i - size: %dx%d", root, scr_w, scr_h);
}

static void initwm(void)
{
	Workspace *ws;
	int r, len = 0;
	char errbuf[256];
	xcb_void_cookie_t c;
	xcb_generic_error_t *e;
	xcb_cursor_context_t *ctx;
	uint i, j, cws, n = LEN(workspaces);
	uint32_t mask = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT|XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY
		|XCB_EVENT_MASK_ENTER_WINDOW|XCB_EVENT_MASK_STRUCTURE_NOTIFY|XCB_EVENT_MASK_PROPERTY_CHANGE;

	DBG("initializing %s", argv0);
	sigchld(0);

	/* monitors & workspaces */
	initmons();
	initworkspaces();
	assignworkspaces();

	/* cursors */
	if (xcb_cursor_context_new(con, scr, &ctx) < 0)
		errx(1, "unable to create cursor context");
	for (i = 0; i < LEN(cursors); i++)
		cursor[i] = xcb_cursor_load_cursor(ctx, cursors[i]);
	xcb_cursor_context_free(ctx);

	/* rules regex */
	for (i = 0; i < LEN(rules); i++)
		if ((r = regcomp(&rules[i].regcomp, rules[i].regex, REG_NOSUB|REG_EXTENDED|REG_ICASE))) {
			regerror(r, &rules[i].regcomp, errbuf, sizeof(errbuf));
			errx(1, "invalid regex rules[%d]: %s: %s\n", i, rules[i].regex, errbuf);
		}

	/* atoms */
	initatoms(wmatoms, wmatomnames, LEN(wmatomnames));
	initatoms(netatoms, netatomnames, LEN(netatomnames));
	wmcheck = xcb_generate_id(con);
	xcb_create_window(con, XCB_COPY_FROM_PARENT, wmcheck, root, -1, -1, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_ONLY, scr->root_visual, 0, NULL);
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, wmcheck, netatoms[NetWMCheck], XCB_ATOM_WINDOW, 32, 1, &wmcheck);
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, wmcheck, netatoms[NetWMName],  wmatoms[utf8str], 8, 5, "yaxwm");
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, root, netatoms[NetWMCheck], XCB_ATOM_WINDOW, 32, 1, &wmcheck);
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, root, netatoms[NetNumDesktops], XCB_ATOM_CARDINAL, 32, 1, &n);
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, root, netatoms[NetDesktopViewport], XCB_ATOM_CARDINAL, 32, 2, (uint32_t []){0, 0});
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, root, netatoms[NetDesktopGeometry], XCB_ATOM_CARDINAL, 32, 2, (uint32_t []){scr_w, scr_h});
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, root, netatoms[NetSupported], XCB_ATOM_ATOM, 32, NetLast, netatoms);
	xcb_delete_property(con, root, netatoms[NetClientList]);

	/* NetCurrentDesktop */
	if ((r = windowprop(root, netatoms[NetCurrentDesktop])) < 0)
		r = 0;
	cws = r;
	for (ws = wses; ws && ws->num != cws; ws = ws->next)
		;
	selws = ws;
	selws->mon->ws = ws;
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, root, netatoms[NetCurrentDesktop], XCB_ATOM_CARDINAL, 32, 1, &selws->num);

	/* NetDesktopNames */
	for (i = 0; i < n; i++)
		len += sstrlen(workspaces[i]) + 1;
	char names[len];
	for (i = 0, len = 0; i < n; i++)
		for (j = 0; (names[len++] = workspaces[i][j]); j++)
			;
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, root, netatoms[NetDesktopNames], wmatoms[utf8str], 8, --len, names);

	/* root window event mask & cursor */
	c = xcb_change_window_attributes_checked(con, root, XCB_CW_EVENT_MASK|XCB_CW_CURSOR, (uint32_t []){mask, cursor[Normal]});
	if ((e = xcb_request_check(con, c))) {
		free(e);
		errx(1, "unable to change root window event mask and cursor");
	}

	/* mouse & keybinds */
	if (!(keysyms = xcb_key_symbols_alloc(con)))
		errx(1, "error unable to get keysyms from X connection");
	initbinds(0);
	focus(NULL);
}

static void initworkspaces(void)
{
	uint i = 0;
	Workspace *ws;

	if (!wses) {
		wses = initws(workspaces[i], i);
		i++;
	}
	for (ws = wses; i < LEN(workspaces); ws = ws->next, i++)
		ws->next = initws(workspaces[i], i);
}

static Workspace *initws(char *name, int num)
{
	Workspace *ws;

	DBG("initializing new workspace: %s: %d", name, num);
	ws = ecalloc(1, sizeof(Workspace));
	ws->num = num;
	ws->name = name;
	ws->nmaster = nmaster;
	ws->ratio = splitratio;
	ws->layout = layouts[0];
	return ws;
}

static Workspace *itows(uint num)
{
	Workspace *ws = wses;

	while (ws && ws->num != num)
		ws = ws->next;
	return ws;
}

static void killclient(const Arg *arg)
{
	if (!selws->sel)
		return;
	DBG("user requested kill current client");
	(void)(arg);
	if (!sendevent(selws->sel, WMDelete)) {
		xcb_grab_server(con);
		xcb_set_close_down_mode(con, XCB_CLOSE_DOWN_DESTROY_ALL);
		xcb_kill_client(con, selws->sel->win);
		xcb_aux_sync(con);
		xcb_ungrab_server(con);
	} else
		xcb_aux_sync(con);
}

static void layoutws(Workspace *ws)
{
	if (ws) {
		showhide(ws->stack);
		if (ws->layout)
			ws->layout(ws);
		restack(ws);
	} else for (ws = wses; ws; ws = ws->next) {
		showhide(ws->stack);
		if (ws == ws->mon->ws && ws->layout) {
			ws->layout(ws);
			restack(ws);
		}
	}
	xcb_aux_sync(con);
	ignoreevent(XCB_ENTER_NOTIFY);
}

static Client *nexttiled(Client *c)
{
	while (c && c->floating)
		c = c->next;
	return c;
}

static int pointerxy(int *x, int *y)
{
	xcb_generic_error_t *e;
	xcb_query_pointer_reply_t *p;

	if ((p = xcb_query_pointer_reply(con, xcb_query_pointer(con, root), &e))) {
		*x = p->root_x;
		*y = p->root_y;
		free(p);
		return 1;
	} else if (e) {
		warnx("unable to query pointer - X11 error: %d: %s", e->error_code, xcb_event_get_error_label(e->error_code));
		free(e);
	}
	return 0;
}

static Monitor *ptrtomon(int x, int y)
{
	Monitor *m;

	for (m = mons; m; m = m->next)
		if (x >= m->winarea_x && x < m->winarea_x + m->winarea_w && y >= m->winarea_y && y < m->winarea_y + m->winarea_h)
			return m;
	return selws->mon;
}

static void resetorquit(const Arg *arg)
{
	if ((running = arg->i)) {
		char *const argv[] = { argv0, NULL };
		execvp(argv[0], argv);
	}
}

static void resize(Client *c, int x, int y, int w, int h)
{
	uint32_t v[] = { x, y, w, h, c->bw };
	uint16_t mask = XCB_CONFIG_WINDOW_X|XCB_CONFIG_WINDOW_Y|XCB_CONFIG_WINDOW_WIDTH|
		XCB_CONFIG_WINDOW_HEIGHT|XCB_CONFIG_WINDOW_BORDER_WIDTH;

	DBG("resize window: %d - location: %i,%i - size: %ix%i", c->win, x, y, w, h);
	setfield(&c->x, x, &c->old_x);
	setfield(&c->y, y, &c->old_y);
	setfield(&c->w, w, &c->old_w);
	setfield(&c->h, h, &c->old_h);
	if ((nexttiled(c->ws->clients) == c && !nexttiled(c->next)) && !c->floating && !c->fullscreen) {
		v[2] = W(c);
		v[3] = H(c);
		v[4] = 0;
	}
	xcb_configure_window(con, c->win, mask, v);
	configure(c);
	xcb_aux_sync(con);
}

static void resizehint(Client *c, int x, int y, int w, int h, int interact)
{
	if (setsizehints(c, &x, &y, &w, &h, interact))
		resize(c, x, y, w, h);
}

static void restack(Workspace *ws)
{
	Client *c;
	
	if (!(c = ws->sel))
		return;
	DBG("restacking clients on workspace: %d", ws->num);
	if (c->floating || !ws->layout)
		xcb_configure_window(con, c->win, XCB_CONFIG_WINDOW_STACK_MODE, (uint32_t []){ XCB_STACK_MODE_ABOVE });
	if (ws->layout) {
		for (c = ws->stack; c; c = c->snext)
			if (!c->floating && c->ws == c->ws->mon->ws)
				xcb_configure_window(con, c->win, XCB_CONFIG_WINDOW_STACK_MODE, (uint32_t []){ XCB_STACK_MODE_BELOW });
	}
}

static void runcmd(const Arg *arg)
{
	DBG("user run command: %s", ((char **)arg->v)[0]);
	if (fork())
		return;
	if (con)
		close(xcb_get_file_descriptor(con));
	setsid();
	execvp(((char **)arg->v)[0], (char **)arg->v);
	errx(0, "execvp: %s", ((char **)arg->v)[0]);
}

static void send(const Arg *arg)
{
	Client *c;

	if ((c = selws->sel) && arg->ui != selws->num) {
		unfocus(c, 1);
		detach(c, 0);
		detachstack(c);
		setclientdesktop(c, arg->ui);
		attach(c, 0);
		attachstack(c);
		focus(NULL);
		layoutws(NULL);
	}
}

static int sendevent(Client *c, int wmproto)
{
	int n, exists = 0;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t rpc;
	xcb_client_message_event_t cme;
	xcb_icccm_get_wm_protocols_reply_t proto;

	rpc = xcb_icccm_get_wm_protocols(con, c->win, wmatoms[WMProtocols]);
	if (xcb_icccm_get_wm_protocols_reply(con, rpc, &proto, &e)) {
		n = proto.atoms_len;
		while (!exists && n--)
			exists = proto.atoms[n] == wmatoms[wmproto];
		xcb_icccm_get_wm_protocols_reply_wipe(&proto);
	} else if (e) {
		warnx("unable to get wm protocol: %s - X11 error: %d: %s", wmatomnames[wmproto], e->error_code, xcb_event_get_error_label(e->error_code));
		free(e);
	}

	if (exists) {
		cme.response_type = XCB_CLIENT_MESSAGE;
		cme.window = c->win;
		cme.type = wmatoms[WMProtocols];
		cme.format = 32;
		cme.data.data32[0] = wmatoms[wmproto];
		cme.data.data32[1] = XCB_TIME_CURRENT_TIME;
		xcb_send_event(con, 0, c->win, XCB_EVENT_MASK_NO_EVENT, (const char *)&cme);
	}
	return exists;
}

static void sendmon(Client *c, Monitor *m)
{
	if (c->ws->mon == m)
		return;
	unfocus(c, 1);
	detach(c, 0);
	detachstack(c);
	setclientdesktop(c, m->ws->num);
	attach(c, 0);
	attachstack(c);
	focus(NULL);
	layoutws(NULL);
}

static inline void setclientdesktop(Client *c, uint num)
{
	c->ws = itows(num);
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, c->win, netatoms[NetWMDesktop], XCB_ATOM_CARDINAL, 32, 1, &num);
}

static inline void setclientstate(Client *c, long state)
{
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, c->win, wmatoms[WMState], wmatoms[WMState], 32, 2, (long []){state, XCB_ATOM_NONE});
}

static inline void setfield(int *dst, int val, int *old)
{
	if (old)
		*old = *dst;
	*dst = val;
}

static void setfocus(Client *c)
{
	if (!c->nofocus) {
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, c->win, XCB_CURRENT_TIME);
		xcb_change_property(con, XCB_PROP_MODE_REPLACE, root, netatoms[NetActiveWindow], XCB_ATOM_WINDOW, 32, 1, &c->win);
	}
	sendevent(c, WMTakeFocus);
}

static void setfullscreen(Client *c, int fullscreen)
{
	if (fullscreen && !c->fullscreen) {
		xcb_change_property(con, XCB_PROP_MODE_REPLACE, c->win, netatoms[NetWMState], XCB_ATOM_ATOM, 32, 1, (uchar *)&netatoms[NetWMFullscreen]);
		c->oldstate = c->floating;
		c->fullscreen = 1;
		c->old_bw = c->bw;
		c->bw = 0;
		c->floating = 1;
		resize(c, c->ws->mon->x, c->ws->mon->y, c->ws->mon->w, c->ws->mon->h);
		xcb_configure_window(con, c->win, XCB_CONFIG_WINDOW_STACK_MODE, (uint32_t []){ XCB_STACK_MODE_ABOVE });
	} else if (!fullscreen && c->fullscreen) {
		xcb_change_property(con, XCB_PROP_MODE_REPLACE, c->win, netatoms[NetWMState], XCB_ATOM_ATOM, 32, 0, (uchar *)0);
		c->floating = c->oldstate;
		c->fullscreen = 0;
		c->bw = c->old_bw;
		c->x = c->old_x;
		c->y = c->old_y;
		c->w = c->old_w;
		c->h = c->old_h;
		resize(c, c->x, c->y, c->w, c->h);
		layoutws(c->ws);
	}
}

static void setlayout(const Arg *arg)
{
	DBG("setting current monitor layout");
	if (arg && arg->v)	
		selws->layout = (void (*)(Workspace *))arg->v;
	if (selws->sel)
		layoutws(selws);
}

static void setnmaster(const Arg *arg)
{
	selws->nmaster = MAX(selws->nmaster + arg->i, 0);
	layoutws(selws);
}

static int setsizehints(Client *c, int *x, int *y, int *w, int *h, int interact)
{
	int baseismin;
	Monitor *m = c->ws->mon;

	/* set minimum possible */
	*w = MAX(1, *w);
	*h = MAX(1, *h);
	if (interact) { /* don't confine */
		if (*x > scr_w)                 *x = scr_w - W(c);
		if (*y > scr_h)                 *y = scr_h - H(c);
		if (*x + *w + 2 * c->bw < 0)    *x = 0;
		if (*y + *h + 2 * c->bw < 0)    *y = 0;
	} else { /* confine to monitor */
		if (*x > m->x + m->w)           *x = m->x + m->w - W(c);
		if (*y > m->y + m->h)           *y = m->y + m->h - H(c);
		if (*x + *w + 2 * c->bw < m->x) *x = m->x;
		if (*y + *h + 2 * c->bw < m->y) *y = m->y;
	}
	if (c->floating || !c->ws->layout) {
		if (!(baseismin = c->base_w == c->min_w && c->base_h == c->min_h)) { /* temporarily remove base dimensions */
			*w -= c->base_w;
			*h -= c->base_h;
		}
		if (c->min_aspect > 0 && c->max_aspect > 0) { /* adjust for aspect limits */
			if (c->max_aspect < (float)*w / *h)
				*w = *h * c->max_aspect + 0.5;
			else if (c->min_aspect < (float)*h / *w)
				*h = *w * c->min_aspect + 0.5;
		}
		if (baseismin) { /* increment calculation requires this */
			*w -= c->base_w;
			*h -= c->base_h;
		}
		/* adjust for increment value */
		if (c->increment_w)  *w -= *w % c->increment_w;
		if (c->increment_h)  *h -= *h % c->increment_h;
		/* restore base dimensions */
		*w += c->base_w;
		*h += c->base_h;
		*w = MAX(*w, c->min_w);
		*h = MAX(*h, c->min_h);
		if (c->max_w) *w = MIN(*w, c->max_w);
		if (c->max_h) *h = MIN(*h, c->max_h);
	}
	return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

static void setsplit(const Arg *arg)
{
	float f;

	if (!arg || !selws->layout)
		return;
	f = arg->f < 1.0 ? arg->f + selws->ratio : arg->f - 1.0;
	if (f < 0.1 || f > 0.9)
		return;
	DBG("setting split ratio: %f -> %f", selws->ratio, f);
	selws->ratio = f;
	layoutws(selws);
}

static void showhide(Client *c)
{
	if (!c)
		return;
	if (c->ws == c->ws->mon->ws) {
		DBG("showing client: %d - workspace: %d", c->win, c->ws->num);
		xcb_configure_window(con, c->win, XCB_CONFIG_WINDOW_X|XCB_CONFIG_WINDOW_Y, (uint32_t []){ c->x, c->y });
		if ((!c->ws->layout || c->floating) && !c->fullscreen)
			resizehint(c, c->x, c->y, c->w, c->h, 0);
		showhide(c->snext);
	} else {
		showhide(c->snext);
		DBG("hiding client: %d - workspace: %d", c->win, c->ws->num);
		xcb_configure_window(con, c->win, XCB_CONFIG_WINDOW_X|XCB_CONFIG_WINDOW_Y, (uint32_t []){ W(c) * -2, c->y });
	}
}

static void sigchld(int unused)
{
	(void)(unused);
	if (signal(SIGCHLD, sigchld) == SIG_ERR)
		errx(1, "can't install SIGCHLD handler");
	while(0 < waitpid(-1, NULL, WNOHANG))
		;
}

static void sizehints(Client *c)
{
	xcb_size_hints_t s;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t pc;

	pc = xcb_icccm_get_wm_normal_hints(con, c->win);
	DBG("setting client size hints");
	c->min_w = c->min_h = 0;
	c->max_w = c->max_h = 0;
	c->base_w = c->base_h = 0;
	c->max_aspect = c->min_aspect = 0.0;
	c->increment_w = c->increment_h = 0;

	if (!xcb_icccm_get_wm_normal_hints_reply(con, pc, &s, &e)) {
		if (e) {
			warnx("unable to get wm normal hints - X11 error: %d: %s", e->error_code, xcb_event_get_error_label(e->error_code));
			free(e);
		}
		s.flags = XCB_ICCCM_SIZE_HINT_P_SIZE;
	}
	if (s.flags & XCB_ICCCM_SIZE_HINT_P_ASPECT) {
		c->min_aspect = (float)s.min_aspect_den / s.min_aspect_num;
		c->max_aspect = (float)s.max_aspect_num / s.max_aspect_den;
		DBG("set min/max aspect: min = %f, max = %f", c->min_aspect, c->max_aspect);
	}
	if (s.flags & XCB_ICCCM_SIZE_HINT_P_MAX_SIZE) {
		c->max_w = s.max_width;
		c->max_h = s.max_height;
		DBG("set max size: %dx%d", c->max_w, c->max_h);
	}
	if (s.flags & XCB_ICCCM_SIZE_HINT_P_RESIZE_INC) {
		c->increment_w = s.width_inc;
		c->increment_h = s.height_inc;
		DBG("set increment size: %dx%d", c->increment_w, c->increment_h);
	}
	if (s.flags & XCB_ICCCM_SIZE_HINT_BASE_SIZE) {
		c->base_w = s.base_width;
		c->base_h = s.base_height;
		DBG("set base size: %dx%d", c->base_w, c->base_h);
	} else if (s.flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) {
		c->base_w = s.min_width;
		c->base_h = s.min_height;
		DBG("set base size: %dx%d", c->base_w, c->base_h);
	}
	if (s.flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) {
		c->min_w = s.min_width;
		c->min_h = s.min_height;
		DBG("set min size: %dx%d", c->min_w, c->min_h);
	} else if (s.flags & XCB_ICCCM_SIZE_HINT_BASE_SIZE) {
		c->min_w = s.base_width;
		c->min_h = s.base_height;
		DBG("set min size: %dx%d", c->min_w, c->min_h);
	}
	c->fixed = (c->max_w && c->max_h && c->max_w == c->min_w && c->max_h == c->min_h);
	DBG("client is %s size", c->fixed ? "fixed" : "variable");
}

static int sstrlen(const char *str)
{
	int r = 0;

	while (str[r])
		++r;
	return r;
}

static void swapclient(const Arg *arg)
{
	Client *c = selws->sel;

	(void)(arg);
	if (!selws->layout || (c && c->floating) || (c == nexttiled(selws->clients) && (!c || !(c = nexttiled(c->next)))))
		return;
	DBG("swapping current client window: %d", c->win);
	detach(c, 1);
	focus(c);
	layoutws(c->ws);
}

static void tile(Workspace *ws)
{
	Client *c;
	Monitor *m = ws->mon;
	uint i, n, h, mw, my, ty;

	for (n = 0, c = nexttiled(ws->clients); c; c = nexttiled(c->next), n++)
		;
	if (n == 0)
		return;
	DBG("tiling workspace: %d", ws->num);
	if (n > ws->nmaster)
		mw = ws->nmaster ? m->winarea_w * ws->ratio : 0;
	else
		mw = m->winarea_w;
	for (i = my = ty = 0, c = nexttiled(ws->clients); c; c = nexttiled(c->next), i++)
		if (i < ws->nmaster) {
			h = (m->winarea_h - my) / (MIN(n, ws->nmaster) - i);
			resize(c, m->winarea_x, m->winarea_y + my, mw - (2*c->bw), h - (2*c->bw));
			my += H(c);
		} else {
			h = (m->winarea_h - ty) / (n - i);
			resize(c, m->winarea_x + mw, m->winarea_y + ty, m->winarea_w - mw - (2*c->bw), h - (2*c->bw));
			ty += H(c);
		}
}

static void togglefloat(const Arg *arg)
{
	Client *c;

	if (!(c = selws->sel) || c->fullscreen)
		return;
	DBG("toggling selected window floating state: %d -> %d", c->floating, !c->floating);
	(void)(arg);
	if ((c->floating = !c->floating || c->fixed)) {
		resizehint(c, (c->x = c->old_x), (c->y = c->old_y), (c->w = c->old_w), (c->h = c->old_h), 0);
	} else {
		c->old_x = c->x;
		c->old_y = c->y;
		c->old_w = c->w;
		c->old_h = c->h;
	}
	layoutws(selws);
}

static void unfocus(Client *c, int focusroot)
{
	if (!c)
		return;
	DBG("unfocusing client: %d", c->win);
	xcb_change_window_attributes(con, c->win, XCB_CW_BORDER_PIXEL, &borders[Unfocus]);
	if (focusroot) {
		DBG("focusing root window");
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
		xcb_delete_property(con, root, netatoms[NetActiveWindow]);
	}
}

static void view(const Arg *arg)
{
	Workspace *ws;

	if (arg->ui != selws->num) {
		DBG("viewing workspace: %d", arg->ui);
		for (ws = wses; ws && ws->num != arg->ui; ws = ws->next)
			;
		if (!ws)
			errx(1, "fatal error: reached the end of workspaces without finding a match for %d", arg->ui);
		selws = ws;
		selws->mon->ws = ws;
		xcb_change_property(con, XCB_PROP_MODE_REPLACE, root, netatoms[NetCurrentDesktop], XCB_ATOM_CARDINAL, 32, 1, (uchar *)&arg->ui);
		focus(NULL);
		layoutws(NULL);
		if (!ws->stack)
			xcb_aux_sync(con);
	}
}

static xcb_get_window_attributes_reply_t *windowattr(xcb_window_t win)
{
	xcb_generic_error_t *e;
	xcb_get_window_attributes_cookie_t c;
	xcb_get_window_attributes_reply_t *wa;

	c = xcb_get_window_attributes(con, win);
	wa = NULL;
	DBG("getting window attributes from window: %d", win);
	if (!(wa = xcb_get_window_attributes_reply(con, c, &e)) && e) {
		warnx("unable to get window attributes - X11 error: %d: %s", e->error_code, xcb_event_get_error_label(e->error_code));
		free(e);
	}
	return wa;
}

static void windowhints(Client *c)
{
	xcb_generic_error_t *e;
	xcb_icccm_wm_hints_t wmh;
	xcb_get_property_cookie_t pc;
	
	pc = xcb_icccm_get_wm_hints(con, c->win);
	DBG("checking and setting wm hints for window: %d", c->win);
	if (xcb_icccm_get_wm_hints_reply(con, pc, &wmh, &e)) {
		if (c == selws->sel && wmh.flags & XCB_ICCCM_WM_HINT_X_URGENCY) {
			wmh.flags &= ~XCB_ICCCM_WM_HINT_X_URGENCY;
			xcb_icccm_set_wm_hints(con, c->win, &wmh);
		} else
			c->urgent = (wmh.flags & XCB_ICCCM_WM_HINT_X_URGENCY) ? 1 : 0;
		if (wmh.flags & XCB_ICCCM_WM_HINT_INPUT)
			c->nofocus = !wmh.input;
		else
			c->nofocus = 0;
	} else if (e) {
		warnx("unable to get wm hints - X11 error: %d: %s", e->error_code, xcb_event_get_error_label(e->error_code));
		free(e);
	}
}

static xcb_atom_t windowprop(xcb_window_t win, xcb_atom_t prop)
{
	xcb_atom_t ret;
	xcb_generic_error_t *e;
	xcb_get_property_reply_t *r;
	xcb_get_property_cookie_t c;

	c = xcb_get_property(con, 0, win, prop, XCB_ATOM_ANY, 0, sizeof(ret));
	ret = -1;
	DBG("getting window property from window: %d", win);
	if ((r = xcb_get_property_reply(con, c, &e))) {
		if (xcb_get_property_value_length(r))
			ret = *(xcb_atom_t *)xcb_get_property_value(r);
		free(r);
	} else if (e) {
		warnx("unable to get window property - X11 error: %d: %s", e->error_code, xcb_event_get_error_label(e->error_code));
		free(e);
	}
	return ret;
}

static xcb_window_t windowtrans(xcb_window_t win)
{
	xcb_window_t trans;
	xcb_get_property_cookie_t pc;
	xcb_generic_error_t *e = NULL;

	pc = xcb_icccm_get_wm_transient_for(con, win);
	trans = XCB_WINDOW_NONE;
	DBG("getting transient for hint - window: %d", win);
	if (!xcb_icccm_get_wm_transient_for_reply(con, pc, &trans, &e) && e) {
		warnx("unable to get wm transient for hint - X11 error: %d: %s", e->error_code, xcb_event_get_error_label(e->error_code));
		free(e);
	}
	return trans;
}

static void windowtype(Client *c)
{
	DBG("checking window type for window: %d", c->win);
	if (windowprop(c->win, netatoms[NetWMState]) == netatoms[NetWMFullscreen])
		setfullscreen(c, 1);
	else if (windowprop(c->win, netatoms[NetWMWindowType]) == netatoms[NetWMWindowTypeDialog])
		c->floating = 1;
}

static Client *wintoclient(xcb_window_t win)
{
	Client *c;
	Workspace *ws;

	if (win != root)
		for (ws = wses; ws; ws = ws->next)
			for (c = ws->clients; c; c = c->next)
				if (c->win == win)
					return c;
	return NULL;
}

static Workspace *wintows(xcb_window_t win)
{
	Client *c;
	Workspace *ws;

	if (win != root)
		for (ws = wses; ws; ws = ws->next)
			for (c = ws->clients; c; c = c->next)
				if (c->win == win)
					return ws;
	return selws;
}

int main(int argc, char *argv[])
{
	xcb_void_cookie_t c;

	argv0 = argv[0];
	if (argc > 1) {
		fprintf(stderr, argv[1][0] == '-' && argv[1][1] == 'v' && argv[1][2] == '\0' ? "%s v0.01\n" : "usage: %s [-v]\n", argv0);
		exit(1);
	}
	if (!setlocale(LC_CTYPE, ""))
		errx(1, "no locale support");
	if (xcb_connection_has_error((con = xcb_connect(NULL, NULL))))
		errx(1, "error connecting to X");
	atexit(freewm);
	initscreen();
	c = xcb_change_window_attributes_checked(con, root, XCB_CW_EVENT_MASK, (uint32_t []){ XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT });
	if (xcb_request_check(con, c))
		errx(1, "is another window manager already running?");
	initwm();
	initexisting();
	eventloop();

	return 0;
}
