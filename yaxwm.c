/* yet another X window manager
 *
 * vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
 */

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
#include <xcb/xproto.h>
#include <xcb/xcb_util.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_cursor.h>
#include <xcb/xcb_keysyms.h>
#include <X11/keysym.h>

#ifdef RANDR
#include <xcb/randr.h>
#endif

#ifdef DEBUG
#include <xkbcommon/xkbcommon.h>
#endif

#define W(x)                   ((x)->w + 2 * (x)->bw)
#define H(x)                   ((x)->h + 2 * (x)->bw)
#define MAX(a, b)              ((a) > (b) ? (a) : (b))
#define MIN(a, b)              ((a) < (b) ? (a) : (b))
#define LEN(x)                 (sizeof(x) / sizeof(x[0]))
#define CLNMOD(m)              (m & ~(numlockmask|XCB_MOD_MASK_LOCK))
#define FOR_EACH(v, head)      for ((v) = (head); (v); (v) = (v)->next)
#define FOR_TAIL(v, head)      for ((v) = (head); (v) && (v)->next; (v) = (v)->next)
#define FOR_MATCH(v, m, head)  for ((v) = (head); (v) && (v)->next && (v)->next != (m); (v) = (v)->next)
#define FOR_WSCLIENTS(c, ws)   FOR_EACH((ws), workspaces) FOR_EACH((c), (ws)->clients)

#ifndef RANDR
typedef int xcb_randr_output_t;
#endif
typedef unsigned int uint;
typedef unsigned char uchar;
typedef union Arg Arg;
typedef struct Bind Bind;
typedef struct Rule Rule;
typedef struct Client Client;
typedef struct Monitor Monitor;
typedef struct Workspace Workspace;
typedef struct WorkspaceDefault WorkspaceDefault;

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

struct Client {
	int x, y, w, h, bw;
	int old_x, old_y, old_w, old_h, old_bw;
	int max_w, max_h, min_w, min_h;
	int base_w, base_h, increment_w, increment_h;
	float min_aspect, max_aspect;
	int fixed, floating, fullscreen, urgent, nofocus, oldstate;
	Client *next, *snext;
	Workspace *ws;
	xcb_window_t win;
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

struct WorkspaceDefault {
	char *name;
	uint nmaster;
	float splitratio;
	void (*layout)(Workspace *);
};

static const char *cursors[] = {
	[Move] = "fleur",
	[Normal] = "arrow",
	[Resize] = "sizing"
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

static char *argv0; /* program name */
static Monitor *monitors; /* monitor linked list head */
static int scr_w, scr_h; /* root window size */
static int randr = -1; /* randr extension response */
static uint running = 1; /* exit cleanly when 0 */
static xcb_screen_t *scr; /* the X screen */
static uint numlockmask = 0; /* numlock modifier bit mask */
static uint numws = 0; /* number of workspaces currently allocated */
static xcb_connection_t *con; /* xcb connection to the X server */
static Workspace *workspaces, *selws; /* selected workspace and workspace linked list head */
static xcb_window_t root, wmcheck; /* root window and _NET_SUPPORTING_WM_CHECK window */
static xcb_key_symbols_t *keysyms; /* current keymap symbols */
static xcb_atom_t wmatoms[WMLast]; /* array of _WM atoms used mostly internally */
static xcb_cursor_t cursor[CurLast]; /* array of cursors for moving, resizing, and normal */
static xcb_atom_t netatoms[NetLast]; /* array of _NET atoms used both internally and by other clients */

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
static int sendevent(Client *c, int wmproto);
static int setsizehints(Client *c, int *x, int *y, int *w, int *h, int interact);
static int windowgeom(xcb_window_t win, int *x, int *y, int *w, int *h, int *bw);
static size_t strlcpy(char *dst, const char *src, size_t size);
static void *ecalloc(size_t nmemb, size_t size);
static void assignworkspaces(void);
static void attach(Client *c, int tohead);
static void changefocus(const Arg *arg);
static void checkerror(char *prompt, xcb_generic_error_t *e);
static void clientrules(Client *c);
static void configure(Client *c);
static void detach(Client *c, int reattach);
static void eventloop(void);
static void fixupworkspaces(void);
static void focus(Client *c);
static void follow(const Arg *arg);
static void freeclient(Client *c, int destroyed);
static void freemon(Monitor *m);
static void freewm(void);
static void freews(Workspace *ws);
static void initatoms(xcb_atom_t *atoms, const char **names, int num);
static void initbinds(int onlykeys);
static void initclient(xcb_window_t win, xcb_window_t trans);
static void initexisting(void);
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
static void setclientframeextents(Client *c, uint width);
static void setwinstate(xcb_window_t win, long state);
static void setclientws(Client *c, uint num);
static void setfocus(Client *c);
static void setfullscreen(Client *c, int fullscreen);
static void setlayout(const Arg *arg);
static void setnmaster(const Arg *arg);
static void setsplit(const Arg *arg);
static void showhide(Client *c);
static void sigchld(int unused);
static void sizehints(Client *c);
static void swapclient(const Arg *arg);
static void tile(Workspace *ws);
static void togglefloat(const Arg *arg);
static void unfocus(Client *c, int focusroot);
static void view(const Arg *arg);
static void windowhints(Client *c);
static void windowtype(Client *c);
static xcb_atom_t windowprop(xcb_window_t win, xcb_atom_t prop);
static xcb_get_window_attributes_reply_t *windowattr(xcb_window_t win);
static xcb_window_t windowtrans(xcb_window_t win);

#ifdef PANEL
typedef struct Panel Panel;
struct Panel {
	int x, y, w, h;
	int strut_l, strut_r, strut_t, strut_b;
	Panel *next;
	Monitor *mon;
	xcb_window_t win;
};
static Panel *panels; /* panel linked list head */

static Panel *initpanel(xcb_window_t win);
static Panel *wintopanel(xcb_window_t win);
static void freepanel(Panel *panel, int destroyed);
static void updatestruts(Panel *p, int apply);
#endif

#ifdef RANDR
static Monitor *randrclone(xcb_randr_output_t id, int x, int y, int w, int h);
static Monitor *randrtomon(xcb_randr_output_t id);
static int initmons(xcb_randr_output_t *outputs, int len);
static int initrandr(void);
#endif

#ifdef DEBUG
static size_t strlcat(char *dst, const char *src, size_t size);
static void print(const char *fmt, ...);
static char *masktomods(uint mask, char *out, int outsize);
#define DBG(fmt, ...) print("%s:%d - " fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#else
#define DBG(fmt, ...)
#endif

#include "config.h"

int main(int argc, char *argv[])
{
	xcb_void_cookie_t c;
	uint mask = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT;

	argv0 = argv[0];
	if (argc > 1) {
		fprintf(stderr, !strcmp(argv[1], "-v") ? "%s v0.01\n" : "usage: %s [-v]\n", argv0);
		exit(1);
	}
	if (!setlocale(LC_CTYPE, ""))
		errx(1, "no locale support");
	if (xcb_connection_has_error((con = xcb_connect(NULL, NULL))))
		errx(1, "error connecting to X");
	atexit(freewm);
	if (!(scr = xcb_setup_roots_iterator(xcb_get_setup(con)).data))
		errx(1, "error getting default screen from X connection");
	root = scr->root;
	scr_w = scr->width_in_pixels;
	scr_h = scr->height_in_pixels;
	DBG("initialized root window: %i - size: %dx%d", root, scr_w, scr_h);
	c = xcb_change_window_attributes_checked(con, root, XCB_CW_EVENT_MASK, &mask);
	if (xcb_request_check(con, c))
		errx(1, "is another window manager already running?");
	initwm();
	initexisting();
	eventloop();

	return 0;
}

void attach(Client *c, int tohead)
{
	Client *n;

	if (!c->ws)
		c->ws = selws;
	if (!tohead && (n = nexttiled(c->ws->clients))) {
		c->next = n->next;
		n->next = c;
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

void assignworkspaces(void)
{
	Monitor *m;
	Workspace *ws;
	uint i, j, n = 0;
	char name[4]; /* we're never gonna have more than 999 workspaces */

	FOR_EACH(m, monitors)
		n++;
	if (n > numws) {
		DBG("warning: more monitors than workspaces, allocating enough to assign one per monitor");
		while (n > numws) {
			FOR_TAIL(ws, workspaces);
			if (ws)
				ws->next = initws(numws, itoa(numws, name), ws->nmaster, ws->splitratio, ws->layout);
			else
				workspaces = initws(numws, itoa(numws, name), 1, 0.5, tile);
			numws++;
		}
	}

	j = numws / MAX(1, n);
	ws = workspaces;
	DBG("%d workspaces - %d per monitor", numws, j);
	FOR_EACH(m, monitors)
		for (i = 0; ws && i < j; i++, ws = ws->next) { /* assign up to j or until the end of workspaces */
			ws->mon = m;
			DBG("workspace: %d - monitor: %s", ws->num, m->name);
			if (!i)
				m->ws = ws;
		}

	if (j * n != numws) {
		DBG("leftovers after dividing between monitors, assigning one per monitor until exhausted");
		for (m = monitors; ws; m = monitors)
			while (ws && m) {
				DBG("workspace: %d - monitor: %s", ws->num, m->name);
				ws->mon = m;
				ws = ws->next;
				m = m->next;
			}
	}
}

void changefocus(const Arg *arg)
{
	Client *c;

	if (!selws->sel || selws->sel->fullscreen)
		return;
	if (arg->i > 0)
		c = selws->sel->next ? selws->sel->next : selws->clients;
	else
		FOR_MATCH(c, selws->sel, selws->clients);
	if (c) {
		DBG("focusing %s client", arg->i > 0 ? "next" : "previous");
		focus(c);
		restack(c->ws);
	}
}

void checkerror(char *prompt, xcb_generic_error_t *e)
{
	if (!e)
		return;
	warnx("%s -- X11 error: %d: %s", prompt, e->error_code, xcb_event_get_error_label(e->error_code));
	free(e);
}

void configure(Client *c)
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

void clientrules(Client *c)
{
	uint i;
	Monitor *m;
	int ws, n, num = -1;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t pc;
	xcb_icccm_get_wm_class_reply_t prop;

	DBG("setting client defaults and rule matching");
	pc = xcb_icccm_get_wm_class(con, c->win);
	c->floating = 0;
	if ((ws = windowprop(c->win, netatoms[Desktop])) < 0)
		ws = selws->num;
	if (xcb_icccm_get_wm_class_reply(con, pc, &prop, &e) && prop.class_name && prop.instance_name) {
		DBG("window class: %s - instance: %s", prop.class_name, prop.instance_name);
		for (i = 0; i < LEN(rules); i++)
			if (!regexec(&rules[i].regcomp, prop.class_name, 0, NULL, 0) || !regexec(&rules[i].regcomp, prop.instance_name, 0, NULL, 0)) {
				DBG("client matched rule regex: %s", rules[i].regex);
				c->floating = rules[i].floating;
				if (rules[i].workspace >= 0)
					ws = rules[i].workspace;
				else if (rules[i].monitor) {
					if (strlen(rules[i].monitor) == 1 && isdigit(rules[i].monitor[0]))
						num = atoi(rules[i].monitor);
					for (n = 0, m = monitors; m; m = m->next, n++)
						if ((num >= 0 && num == n) || !strcmp(rules[i].monitor, m->name)) {
							ws = m->ws->num; /* monitors are really just their current workspace */
							break;
						}
				}
				break;
			}
		xcb_icccm_get_wm_class_reply_wipe(&prop);
	} else {
		checkerror("failed to get window class", e);
	}
	setclientws(c, ws);
	DBG("set client values - workspace: %d, floating: %d, monitor: %s", c->ws->num, c->floating, c->ws->mon->name);
}

void detach(Client *c, int reattach)
{
	Client **tc = &c->ws->clients;

	while (*tc && *tc != c)
		tc = &(*tc)->next;
	*tc = c->next;
	if (reattach)
		attach(c, 1);
}

void detachstack(Client *c)
{
	Client **tc = &c->ws->stack;

	while (*tc && *tc != c)
		tc = &(*tc)->snext;
	*tc = c->snext;
	if (c == c->ws->sel)
		c->ws->sel = c->ws->stack;
}

void *ecalloc(size_t nmemb, size_t size)
{
	void *p;

	if (!(p = calloc(nmemb, size))) /* calloc(3) is crucial because it zeroes the memory, linked list ->next will always be NULL */
		err(1, "unable to allocate space");
	return p;
}

void eventloop(void)
{
	int x, y;
	Client *c;
#ifdef PANEL
	Panel *p;
#endif
	Monitor *m;
	Workspace *ws;
	uint i, mousebtn = 0;
	xcb_generic_event_t *ev;
	xcb_generic_error_t *err;

	xcb_aux_sync(con);
	while (running && (ev = xcb_wait_for_event(con)) != NULL) {
#ifdef RANDR
		if (randr >= 0 && ev->response_type == randr + XCB_RANDR_SCREEN_CHANGE_NOTIFY) {
			DBG("RANDR screen change notify, updating monitors");
			if (initrandr() > 0)
				fixupworkspaces();
			free(ev);
			continue;
		}
#endif
		switch (XCB_EVENT_RESPONSE_TYPE(ev)) {
			case XCB_FOCUS_IN:
			{
				xcb_focus_in_event_t *e = (xcb_focus_in_event_t *)ev;

				if (selws->sel && e->event != selws->sel->win && (c = wintoclient(e->event))) {
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
					if (randr < 0 && i) {
						monitors->w = monitors->winarea_w = scr_w;
						monitors->h = monitors->winarea_h = scr_h;
						fixupworkspaces();
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
						if (e->value_mask & XCB_CONFIG_WINDOW_X)
							c->x = c->old_x = m->x + e->x;
						if (e->value_mask & XCB_CONFIG_WINDOW_Y)
							c->y = c->old_y = m->y + e->y;
						if (e->value_mask & XCB_CONFIG_WINDOW_WIDTH)
							c->w = c->old_w = e->width;
						if (e->value_mask & XCB_CONFIG_WINDOW_HEIGHT)
							c->h = c->old_h = e->height;
						if ((c->x + c->w) > m->x + m->w)
							c->x = c->old_x = m->x + (m->w / 2 - c->w / 2);
						if ((c->y + c->h) > m->y + m->h)
							c->y = c->old_y = m->y + (m->h / 2 - c->h / 2);
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
				break;
			}
			case XCB_DESTROY_NOTIFY:
			{
				xcb_destroy_notify_event_t *e = (xcb_destroy_notify_event_t *)ev;

				if ((c = wintoclient(e->window))) {
					DBG("destroy notify event for managed client: %d -- freeing", e->window);
					freeclient(c, 1);
#ifdef PANEL
				} else if ((p = wintopanel(e->window))) {
					DBG("destroy notify event for managed panel: %d -- freeing", e->window);
					freepanel(p, 1);
#endif
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
				if (mousebtn == XCB_BUTTON_INDEX_3) {
					xcb_aux_sync(con);
					while ((ev = xcb_poll_for_queued_event(con)))
						free(ev);
				}
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
			case XCB_KEY_PRESS:
			{
				xcb_keysym_t sym;
				xcb_key_press_event_t *e = (xcb_key_press_event_t *)ev;

				sym = xcb_key_symbols_get_keysym(keysyms, e->detail, 0);
				for (i = 0; i < LEN(binds); i++)
					if (sym == binds[i].keysym && binds[i].func && CLNMOD(binds[i].mod) == CLNMOD(e->state)) {
#ifdef DEBUG
						char mod[64], key[64];
						masktomods(binds[i].mod, mod, sizeof(mod));
						xkb_keysym_get_name(sym, key, sizeof(key));
						DBG("%s event - key: %s - mod: %s", xcb_event_get_label(ev->response_type), key, mod);
#endif
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

				if (!(wa = windowattr(e->window)))
					break;
				if (!wa->override_redirect && wintoclient(e->window)) {
					free(wa);
					break;
				}
#ifdef PANEL
				if (windowprop(e->window, netatoms[WindowType]) == netatoms[WindowTypeDock] && !wintopanel(e->window) && !wintoclient(e->window)) {
					DBG("map request window is a dock or panel");
					FOR_TAIL(p, panels);
					if (p)
						p->next = initpanel(e->window);
					else
						panels = initpanel(e->window);
				} else
#endif
					if (!wa->override_redirect && !wintoclient(e->window))
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
						setwinstate(c->win, XCB_ICCCM_WM_STATE_WITHDRAWN);
					} else {
						DBG("unmap notify event for managed window: %i - freeing", e->window);
						freeclient(c, 0);
					}
#ifdef PANEL
				} else if ((p = wintopanel(e->window))) {
					freepanel(p, 0);
#endif
				}
				break;
			}
			case XCB_CLIENT_MESSAGE:
			{
				xcb_client_message_event_t *e = (xcb_client_message_event_t *)ev;

				if (e->window == root && e->type == netatoms[CurrentDesktop]) {
					view(&(const Arg){.ui = e->data.data32[0]});
					break;
				} else if (!(c = wintoclient(e->window))) {
					break;
				} else if (e->type == netatoms[ActiveWindow]) {
					view(&(const Arg){.ui = c->ws->num});
					focus(c);
				} else if (e->type == netatoms[State] && e->data.data32[1] == netatoms[Fullscreen]) {
					setfullscreen(c, e->data.data32[0]);
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
					if (e->atom == netatoms[WindowType])
						windowtype(c);
				}
				break;
			}
			default:
			{
				xcb_generic_error_t *e = (xcb_generic_error_t *)ev;
				
				if (ev->response_type                                                             /* ignored event, not an error */
						||  e->error_code == 3                                                    /* BadWindow */
						|| (e->error_code == 8  && (e->major_code == 42 || e->major_code == 12))  /* BadMatch & SetInputFocus/ConfigureWindow */
						|| (e->error_code == 10 && (e->major_code == 28 || e->major_code == 33))) /* BadAccess & GrabButton/GrabKey */
					break;

				/* TODO: some kind of error handling for those we don't want to (or shouldn't) ignore, similar to xlib? */
				warnx("event error: %d: \"%s\" - %d: \"%s\"",
						e->error_code, xcb_event_get_error_label(e->error_code),
						e->major_code, xcb_event_get_request_label(e->major_code));
				break;
			}
		}
		free(ev);
	}
}

void fixupworkspaces(void)
{
	Client *c;
	Workspace *ws;

	assignworkspaces();
	FOR_WSCLIENTS(c, ws)
		if (c->fullscreen)
			resize(c, ws->mon->winarea_x, ws->mon->winarea_y, ws->mon->winarea_w, ws->mon->winarea_h);
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, root, netatoms[DesktopGeometry], XCB_ATOM_CARDINAL, 32, 2, (uint []){scr_w, scr_h});
	focus(NULL);
	layoutws(NULL);
}

void focus(Client *c)
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
		detachstack(c);
		attachstack(c);
		xcb_change_window_attributes(con, c->win, XCB_CW_BORDER_PIXEL, &borders[Focus]);
		setfocus(c);
	} else {
		DBG("focusing root window");
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
		xcb_delete_property(con, root, netatoms[ActiveWindow]);
	}
	selws->sel = c;
}

void follow(const Arg *arg)
{
	if (selws->sel && arg->ui != selws->num) {
		send(arg);
		view(arg);
	}
}

void freeclient(Client *c, int destroyed)
{
	if (!c)
		return;
	Workspace *ws, *cws = c->ws;

	DBG("freeing client: %d - destroyed: %i", c->win, destroyed);
	detach(c, 0);
	detachstack(c);
	if (!destroyed) {
		xcb_grab_server(con);
		xcb_configure_window(con, c->win, XCB_CONFIG_WINDOW_BORDER_WIDTH, &c->old_bw);
		setwinstate(c->win, XCB_ICCCM_WM_STATE_WITHDRAWN);
		xcb_aux_sync(con);
		xcb_ungrab_server(con);
	}
	free(c);
	focus(NULL);
	xcb_delete_property(con, root, netatoms[ClientList]);
	FOR_WSCLIENTS(c, ws)
		xcb_change_property(con, XCB_PROP_MODE_APPEND, root, netatoms[ClientList], XCB_ATOM_WINDOW, 32, 1, &c->win);
	layoutws(cws);
}

void freemon(Monitor *m)
{
	Monitor *mon;

	if (m == monitors)
		monitors = monitors->next;
	else {
		FOR_MATCH(mon, m, monitors);
		if (mon)
			mon->next = m->next;
	}
	DBG("freeing monitor: %s", m->name);
	free(m->name);
	free(m);
}

void freewm(void)
{
	uint i;
	Workspace *ws;

	FOR_EACH(ws, workspaces)
		while (ws->stack)
			freeclient(ws->stack, 0);
	xcb_ungrab_button(con, XCB_BUTTON_INDEX_ANY, root, XCB_MOD_MASK_ANY);
	xcb_ungrab_key(con, XCB_GRAB_ANY, root, XCB_MOD_MASK_ANY);
	xcb_key_symbols_free(keysyms);
#ifdef PANEL
	while (panels)
		freepanel(panels, 0);
#endif
	while (monitors)
		freemon(monitors);
	while (workspaces)
		freews(workspaces);
	for (i = 0; i < LEN(cursors); i++)
		xcb_free_cursor(con, cursor[i]);
	for (i = 0; i < LEN(rules); i++)
		regfree(&rules[i].regcomp);
	xcb_destroy_window(con, wmcheck);
	xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, XCB_INPUT_FOCUS_POINTER_ROOT, XCB_CURRENT_TIME);
	xcb_aux_sync(con);
	xcb_delete_property(con, root, netatoms[ActiveWindow]);
	xcb_disconnect(con);
}

void freews(Workspace *ws)
{
	Workspace *sel;

	if (ws == workspaces)
		workspaces = workspaces->next;
	else {
		FOR_MATCH(sel, ws, workspaces);
		if (sel)
			sel->next = ws->next;
	}
	DBG("freeing monitor: %s", ws->name);
	free(ws);
}

int grabpointer(xcb_cursor_t cursor)
{
	int r = 0;
	xcb_generic_error_t *e;
	xcb_grab_pointer_cookie_t pc;
	xcb_grab_pointer_reply_t *ptr;

	pc = xcb_grab_pointer(con, 0, root,
			XCB_EVENT_MASK_BUTTON_RELEASE|XCB_EVENT_MASK_BUTTON_MOTION|XCB_EVENT_MASK_POINTER_MOTION_HINT,
			XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, root, cursor, XCB_CURRENT_TIME);
	if ((ptr = xcb_grab_pointer_reply(con, pc, &e))) {
		r = ptr->status == XCB_GRAB_STATUS_SUCCESS;
		free(ptr);
	} else {
		checkerror("unable to grab pointer", e);
	}
	return r;
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
			DBG("initializing atom: %s - value: %d", names[i], r->atom);
			atoms[i] = r->atom;
			free(r);
		} else {
			checkerror("unable to initialize atom", e);
		}
	}
}

void initbinds(int onlykeys)
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
	} else {
		checkerror("unable to get modifier mapping for numlock", e);
	}

	DBG("setting up bindings");
	uint mods[] = { 0, XCB_MOD_MASK_LOCK, numlockmask, numlockmask|XCB_MOD_MASK_LOCK };
	if (!onlykeys)
		xcb_ungrab_button(con, XCB_BUTTON_INDEX_ANY, root, XCB_MOD_MASK_ANY);
	xcb_ungrab_key(con, XCB_GRAB_ANY, root, XCB_MOD_MASK_ANY);
	for (i = 0; i < LEN(mods); i++) {
		if (!onlykeys)
			for (j = 0; j < LEN(btns); j++)
				xcb_grab_button(con, 0, root, XCB_EVENT_MASK_BUTTON_PRESS|XCB_EVENT_MASK_BUTTON_RELEASE,
						XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_SYNC, 0, XCB_NONE, btns[j], MODKEY|mods[i]);
		for (j = 0; j < LEN(binds); j++)
			if ((c = xcb_key_symbols_get_keycode(keysyms, binds[j].keysym))) {
				xcb_grab_key(con, 1, root, binds[j].mod|mods[i], *c, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
				free(c);
			}
	}
}

void initclient(xcb_window_t win, xcb_window_t trans)
{
	Monitor *m;
	Client *c, *t;
	int x, y, w, h, bw;
	xcb_window_t transwin = trans;

	DBG("initializing new client from window: %d", win);
	c = ecalloc(1, sizeof(Client));
	c->win = win;
	if ((!transwin || transwin == XCB_WINDOW_NONE) && (transwin = windowtrans(c->win)) == XCB_WINDOW_NONE)
		clientrules(c);
	else if ((t = wintoclient(transwin))) {
		DBG("window is transient of managed client, setting workspace and monitor to match");
		c->ws = t->ws;
		c->floating = 1;
		setclientws(c, c->ws->num);
	}
	m = c->ws ? c->ws->mon : selws->mon;

	if (windowgeom(c->win, &x, &y, &w, &h, &bw)) {
		DBG("using geometry given by the window");
		c->x = c->old_x = x;
		c->y = c->old_y = y;
		c->w = c->old_w = w;
		c->h = c->old_h = h;
		c->old_bw = bw;
	} else {
		DBG("using root window geometry");
		c->x = c->old_x = m->winarea_x;
		c->y = c->old_y = m->winarea_y;
		c->w = c->old_w = m->winarea_w / 2;
		c->h = c->old_h = m->winarea_h / 2;
		c->old_bw = borders[Width];
	}
	c->bw = borders[Width];

	if (c->x <= m->winarea_x || c->x + W(c) >= m->winarea_x + m->winarea_w)
		c->x = (m->winarea_x + m->winarea_w - W(c)) / 2;
	if (c->y <= m->winarea_y || c->y + H(c) >= m->winarea_y + m->winarea_h)
		c->y = (m->winarea_y + m->winarea_h - H(c)) / 2;
	xcb_configure_window(con, c->win, XCB_CONFIG_WINDOW_BORDER_WIDTH, &c->bw);
	configure(c);
	windowtype(c);
	sizehints(c);
	windowhints(c);
	xcb_change_window_attributes(con, c->win, XCB_CW_EVENT_MASK|XCB_CW_BORDER_PIXEL, (uint []){borders[Focus],
			XCB_EVENT_MASK_ENTER_WINDOW|XCB_EVENT_MASK_FOCUS_CHANGE|XCB_EVENT_MASK_PROPERTY_CHANGE|XCB_EVENT_MASK_STRUCTURE_NOTIFY});
	if (c->floating || (c->floating = c->oldstate = c->fixed))
		xcb_configure_window(con, c->win, XCB_CONFIG_WINDOW_STACK_MODE, (uint []){XCB_STACK_MODE_ABOVE});
	attach(c, 0);
	attachstack(c);
	xcb_change_property(con, XCB_PROP_MODE_APPEND, root, netatoms[ClientList], XCB_ATOM_WINDOW, 32, 1, &c->win);
	setwinstate(c->win, XCB_ICCCM_WM_STATE_NORMAL);
	setclientframeextents(c, c->bw);
	if (selws && c->ws == selws && selws->sel)
		unfocus(selws->sel, 0);
	if (c->ws)
		c->ws->sel = c;
	else if (selws)
		selws->sel = c;
	layoutws(c->ws);
	xcb_map_window(con, c->win);
	DBG("new client mapped: %d,%d @ %dx%d - floating: %d", c->x, c->y, c->w, c->h, c->floating);
	focus(NULL);
}

void initexisting(void)
{
	uint i, num;
	xcb_window_t *win;
	xcb_generic_error_t *e;
	xcb_query_tree_cookie_t c;
	xcb_query_tree_reply_t *tree;

	c = xcb_query_tree(con, root);
	DBG("getting root window tree");
	if ((tree = xcb_query_tree_reply(con, c, &e))) {
		num = tree->children_len;
		win = xcb_query_tree_children(tree);
		xcb_atom_t state[num];
		xcb_window_t trans[num];
		xcb_get_window_attributes_reply_t *wa[num];

		for (i = 0; i < num; i++) { /* non transient */
			trans[i] = state[i] = XCB_WINDOW_NONE;
			if (!(wa[i] = windowattr(win[i]))) {
				win[i] = 0;
#ifdef PANEL
			} else if (windowprop(win[i], netatoms[WindowType]) == netatoms[WindowTypeDock] && wa[i]->map_state != XCB_MAP_STATE_UNMAPPED) {
				Panel *p;
				FOR_TAIL(p, panels);
				if (p)
					p->next = initpanel(win[i]);
				else
					panels = initpanel(win[i]);
				win[i] = 0;
#endif
			} else if (!wa[i]->override_redirect && (trans[i] = windowtrans(win[i])) == XCB_WINDOW_NONE
					&& (wa[i]->map_state == XCB_MAP_STATE_VIEWABLE || (state[i] = windowprop(win[i], wmatoms[WMState])) == XCB_ICCCM_WM_STATE_ICONIC)) {
				initclient(win[i], 0);
				win[i] = 0;
			}
		}
		for (i = 0; i < num; i++) { /* transients */
			if (win[i] && trans[i] && !wa[i]->override_redirect && (wa[i]->map_state == XCB_MAP_STATE_VIEWABLE || state[i] == XCB_ICCCM_WM_STATE_ICONIC))
				initclient(win[i], trans[i]);
			free(wa[i]);
		}
		free(tree);
	} else {
		checkerror("FATAL: unable to query tree from root window", e);
		exit(1);
	}
}

Monitor *initmon(char *name, xcb_randr_output_t id, int x, int y, int w, int h)
{
	Monitor *m;
	uint len = strlen(name) + 1;

	DBG("initializing new monitor: %s @ %d,%d - %dx%d", name, x, y, w, h);
	m = ecalloc(1, sizeof(Monitor));
	m->x = m->winarea_x = x;
	m->y = m->winarea_y = y;
	m->w = m->winarea_w = w;
	m->h = m->winarea_h = h;
	m->id = id;
	m->name = ecalloc(1, len);
	strlcpy(m->name, name, len);
	return m;
}

#ifdef RANDR
int initmons(xcb_randr_output_t *outs, int len)
{
	uint n;
    char name[256];
	int i, changed = 0;
	Monitor *m, *e, *clone;
    xcb_randr_get_output_info_reply_t *o;
    xcb_randr_get_crtc_info_reply_t *crtc;
    xcb_randr_get_output_info_cookie_t oc[len];

	DBG("%d outputs, requesting info for each");
	for (i = 0; i < len; i++)
		oc[i] = xcb_randr_get_output_info(con, outs[i], XCB_CURRENT_TIME);
	for (i = 0; i < len; i++) {
		if (!(o = xcb_randr_get_output_info_reply(con, oc[i], NULL))) {
			DBG("unable to get monitor info for output: %d", outs[i]);
			continue;
		}
		FOR_TAIL(m, monitors);
		if (o->crtc != XCB_NONE && (crtc = xcb_randr_get_crtc_info_reply(con, xcb_randr_get_crtc_info(con, o->crtc, XCB_CURRENT_TIME), NULL))) {
			n = xcb_randr_get_output_info_name_length(o) + 1;
			strlcpy(name, (const char *)xcb_randr_get_output_info_name(o), MIN(sizeof(name), n));
			DBG("crtc: %s -- location: %d,%d -- size: %dx%d -- status: %d", name, crtc->x, crtc->y, crtc->width, crtc->height, crtc->status);
			if ((clone = randrclone(outs[i], crtc->x, crtc->y, crtc->width, crtc->height))) {
				DBG("monitor %s, id %d is a clone of %s, id %d, skipping", name, outs[i], clone->name, clone->id);
			} else if ((e = randrtomon(outs[i]))) {
				DBG("previously initialized monitor: %s -- current location and size: %d,%d @ %dx%d", e->name, e->x, e->y, e->w, e->h);
				changed = crtc->x != e->x || crtc->y != e->y || crtc->width != e->w || crtc->height != e->h;
				if (crtc->x != e->x)
					e->x = e->winarea_x = crtc->x;
				if (crtc->y != e->y)
					e->y = e->winarea_y = crtc->y;
				if (crtc->width != e->w)
					e->w = e->winarea_w = crtc->width;
                if (crtc->height != e->h)
					e->h = e->winarea_h = crtc->height;
				if (m)
					m->next = e;
				else
					monitors = e;
				if (changed) {
					DBG("new size and location for monitor: %s -- %d,%d @ %dx%d", e->name, e->x, e->y, e->w, e->h);
				} else {
					DBG("size and location of monitor: %s -- unchanged", e->name);
				}
			} else {
				if (m)
					m->next = initmon(name, outs[i], crtc->x, crtc->y, crtc->width, crtc->height);
				else
					monitors = initmon(name, outs[i], crtc->x, crtc->y, crtc->width, crtc->height);
				changed = 1;
            }
            free(crtc);
		} else if (o->crtc == XCB_NONE && (e = randrtomon(outs[i]))) {
			DBG("previously initialized monitor is now inactive: %s -- freeing", e->name);
			freemon(e);
			changed = 1;
		}
        free(o);
    }
	if (!monitors) {
		DBG("RANDR extension is active but no monitors were initialized, using entire screen");
		return -1;
	}
	return changed;
}

int initrandr(void)
{
	int changed;
    const xcb_query_extension_reply_t *ext;
    xcb_randr_get_screen_resources_current_reply_t *r;
    xcb_randr_get_screen_resources_current_cookie_t rc;

	ext = xcb_get_extension_data(con, &xcb_randr_id);
	rc = xcb_randr_get_screen_resources_current(con, root);
	if (!ext || !ext->present) {
		warnx("RANDR extension is not available, using entire screen");
		return (randr = -1);
	} else if (!(r = xcb_randr_get_screen_resources_current_reply(con, rc, NULL))) {
		warnx("unable to get screen resources, using entire screen");
		return (randr = -1);
	}
	changed = initmons(xcb_randr_get_screen_resources_current_outputs(r), xcb_randr_get_screen_resources_current_outputs_length(r));
	free(r);
	if (changed < 0)
		return (randr = changed);
	randr = ext->first_event;
	xcb_randr_select_input(con, root, XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE|XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE
			|XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE|XCB_RANDR_NOTIFY_MASK_OUTPUT_PROPERTY);
	DBG("RANDR extension active and monitor(s) initialized -- extension base: %d", randr);
	return changed;
}
#endif

#ifdef PANEL
void freepanel(Panel *p, int destroyed)
{
	Panel **pp = &panels;

	DBG("freeing panel: %d", p->win);
	while (*pp && *pp != p)
		pp = &(*pp)->next;
	*pp = p->next;
	if (!destroyed) {
		xcb_grab_server(con);
		setwinstate(p->win, XCB_ICCCM_WM_STATE_WITHDRAWN);
		xcb_aux_sync(con);
		xcb_ungrab_server(con);
	}
	updatestruts(p, 0);
	free(p);
	layoutws(NULL);
}

Panel *initpanel(xcb_window_t win)
{
	int *s;
	Panel *p;
	xcb_generic_error_t *e;
	int x = 0, y = 0, w, h, bw;
	xcb_get_property_cookie_t rc;
	xcb_get_property_reply_t *r = NULL;

	DBG("initializing new panel from window: %d", win);
	p = ecalloc(1, sizeof(Panel));
	p->win = win;
	if (windowgeom(p->win, &x, &y, &w, &h, &bw))
		p->x = x, p->y = y, p->w = w, p->h = h;
	p->mon = ptrtomon(x, y);
	rc = xcb_get_property(con, 0, p->win, netatoms[StrutPartial], XCB_ATOM_CARDINAL, 0, 4);
	DBG("checking panel for _NET_WM_STRUT_PARTIAL or _NET_WM_STRUT");
	if (!(r = xcb_get_property_reply(con, rc, &e)) || r->type == XCB_NONE) {
		checkerror("unable to get _NET_WM_STRUT_PARTIAL from window", e);
		r = xcb_get_property_reply(con, xcb_get_property(con, 0, p->win, netatoms[Strut], XCB_ATOM_CARDINAL, 0, 4), &e);
		checkerror("unable to get _NET_WM_STRUT or _NET_WM_STRUT_PARTIAL from window", e);
	}
	if (r->value_len && (s = xcb_get_property_value(r))) {
		DBG("panel window has struts: left %d, right %d, top %d, bottom %d", s[0], s[1], s[2], s[3]);
		p->strut_l = s[0], p->strut_r = s[1], p->strut_t = s[2], p->strut_b = s[3];
		updatestruts(p, 1);
	}
	if (r)
		free(r);
	xcb_change_window_attributes(con, p->win, XCB_CW_EVENT_MASK,
			(uint []){XCB_EVENT_MASK_PROPERTY_CHANGE|XCB_EVENT_MASK_STRUCTURE_NOTIFY});
	xcb_map_window(con, p->win);
	layoutws(NULL);
	DBG("new panel mapped -- monitor: %s -- geometry: %d,%d @ %dx%d", p->mon->name, p->x, p->y, p->w, p->h);
	return p;
}
#endif

void initwm(void)
{
	int r;
	Workspace *ws;
	uint i, j, cws;
	size_t len = 1;
	char errbuf[256];
	xcb_void_cookie_t c;
	xcb_generic_error_t *e;
	xcb_cursor_context_t *ctx;

	sigchld(0);

	/* monitor(s) & workspaces */
#ifdef RANDR
	if (initrandr() < 0)
#endif
		monitors = initmon("default", 0, 0, 0, scr_w, scr_h);
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
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, wmcheck, netatoms[Check], XCB_ATOM_WINDOW, 32, 1, &wmcheck);
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, wmcheck, netatoms[Name],  wmatoms[Utf8Str], 8, 5, "yaxwm");
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, root, netatoms[Check], XCB_ATOM_WINDOW, 32, 1, &wmcheck);
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, root, netatoms[NumDesktops], XCB_ATOM_CARDINAL, 32, 1, &numws);
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, root, netatoms[DesktopViewport], XCB_ATOM_CARDINAL, 32, 2, (uint []){0, 0});
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, root, netatoms[DesktopGeometry], XCB_ATOM_CARDINAL, 32, 2, (uint []){scr_w, scr_h});
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, root, netatoms[Supported], XCB_ATOM_ATOM, 32, NetLast, netatoms);
	xcb_delete_property(con, root, netatoms[ClientList]);

	/* CurrentDesktop */
	cws = (r = windowprop(root, netatoms[CurrentDesktop])) >= 0 ? r : 0;
	selws = (ws = itows(cws)) ? ws : workspaces;
	selws->mon->ws = ws;
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, root, netatoms[CurrentDesktop], XCB_ATOM_CARDINAL, 32, 1, &selws->num);

	/* DesktopNames */
	FOR_EACH(ws, workspaces)
		len += strlen(ws->name) + 1;
	char names[len];
	len = 0;
	FOR_EACH(ws, workspaces)
		for (j = 0; (names[len++] = ws->name[j]); j++)
			;
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, root, netatoms[DesktopNames], wmatoms[Utf8Str], 8, --len, names);

	/* root window event mask & cursor */
	c = xcb_change_window_attributes_checked(con, root, XCB_CW_EVENT_MASK|XCB_CW_CURSOR,
			(uint []){ XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT|XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY
				|XCB_EVENT_MASK_ENTER_WINDOW|XCB_EVENT_MASK_STRUCTURE_NOTIFY|XCB_EVENT_MASK_PROPERTY_CHANGE,
			cursor[Normal] });
	if ((e = xcb_request_check(con, c))) {
		free(e);
		errx(1, "unable to change root window event mask and cursor");
	}

	/* binds */
	if (!(keysyms = xcb_key_symbols_alloc(con)))
		errx(1, "error unable to get keysyms from X connection");
	initbinds(0);
	focus(NULL);
}

void initworkspaces(void)
{
	Workspace *ws;
	WorkspaceDefault *def;

	for (numws = 0; numws < LEN(default_workspaces); numws++) {
		def = &default_workspaces[numws];
		FOR_TAIL(ws, workspaces);
		if (ws)
			ws->next = initws(numws, def->name, def->nmaster, def->splitratio, def->layout);
		else
			workspaces = initws(numws, def->name, def->nmaster, def->splitratio, def->layout);
	}
}

Workspace *initws(uint num, char *name, uint nmaster, float splitratio, void (*layout)(Workspace *))
{
	Workspace *ws;

	DBG("initializing new workspace: '%s': %d", name, num);
	ws = ecalloc(1, sizeof(Workspace));
	ws->num = num;
	ws->name = name;
	ws->nmaster = nmaster;
	ws->splitratio = splitratio;
	ws->layout = layout;
	return ws;
}

char *itoa(int n, char *s)
{ /* convert n to chars in s */
	char c;
	int j, i = 0, sign = n;

	if (sign < 0)
		n = -n;
	do { /* convert digits to chars in reverse */
		s[i++] = n % 10 + '0';
	} while ((n /= 10) > 0);
	if (sign < 0)
		s[i++] = '-';
	s[i] = '\0';
	for (j = i - 1, i = 0; i < j; i++, j--) { /* un-reverse s */
		c = s[i];
		s[i] = s[j];
		s[j] = c;
	}
	return s;
}

Workspace *itows(uint num)
{ /* return workspace matching num, otherwise NULL */
	Workspace *ws;

	for (ws = workspaces; ws && ws->num != num; ws = ws->next)
		;
	return ws;
}

void killclient(const Arg *arg)
{
	if (!selws->sel)
		return;
	DBG("user requested kill current client");
	(void)(arg);
	if (!sendevent(selws->sel, Delete)) {
		xcb_grab_server(con);
		xcb_set_close_down_mode(con, XCB_CLOSE_DOWN_DESTROY_ALL);
		xcb_kill_client(con, selws->sel->win);
		xcb_aux_sync(con);
		xcb_ungrab_server(con);
	} else {
		xcb_aux_sync(con);
	}
}

void layoutws(Workspace *ws)
{
	if (ws) {
		showhide(ws->stack);
		if (ws->layout)
			ws->layout(ws);
		restack(ws);
	} else {
		FOR_EACH(ws, workspaces) {
			showhide(ws->stack);
			if (ws == ws->mon->ws && ws->layout) {
				ws->layout(ws);
				restack(ws);
			}
		}
	}
	xcb_aux_sync(con);
}

#ifdef DEBUG
char *masktomods(uint mask, char *out, int outsize)
{ /* convert mask to modifier names in out, eg. "Shift, Mod4\0" */
	const char **mod, *mods[] = {
		"Shift", "Lock", "Ctrl", "Mod1", "Mod2", "Mod3", "Mod4",
		"Mod5", "Button1", "Button2", "Button3", "Button4", "Button5"
	};

	*out = '\0';
	for (mod = mods; mask; mask >>= 1, ++mod)
		if (mask & 1) {
			if (*out) {
				strlcat(out, ", ", outsize);
				strlcat(out, *mod, outsize);
			} else
				strlcpy(out, *mod, outsize);
		}
	return out;
}

void print(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
}
#endif

Client *nexttiled(Client *c)
{
	while (c && c->floating)
		c = c->next;
	return c;
}

int pointerxy(int *x, int *y)
{
	xcb_generic_error_t *e;
	xcb_query_pointer_reply_t *p;

	if ((p = xcb_query_pointer_reply(con, xcb_query_pointer(con, root), &e))) {
		*x = p->root_x;
		*y = p->root_y;
		free(p);
		return 1;
	} else {
		checkerror("unable to query pointer", e);
	}
	return 0;
}

Monitor *ptrtomon(int x, int y)
{
	Monitor *m;

	FOR_EACH(m, monitors)
		if (x >= m->x && x < m->x + m->w && y >= m->y && y < m->y + m->h)
			return m;
	return selws->mon;
}

#ifdef RANDR
Monitor *randrclone(xcb_randr_output_t id, int x, int y, int w, int h)
{
	Monitor *m;

	FOR_EACH(m, monitors)
		if (id != m->id && m->x == x && m->y == y && m->w == w && m->h == h)
			return m;
	return m;
}

Monitor *randrtomon(xcb_randr_output_t id)
{
	Monitor *m;

	FOR_EACH(m, monitors)
		if (m->id == id)
			return m;
	return m;
}
#endif

void resetorquit(const Arg *arg)
{
	if ((running = arg->i)) {
		char *const argv[] = { argv0, NULL };
		execvp(argv[0], argv);
	}
}

void resize(Client *c, int x, int y, int w, int h)
{
	uint v[] = { x, y, w, h, c->bw };
	uint16_t mask = XCB_CONFIG_WINDOW_X|XCB_CONFIG_WINDOW_Y|XCB_CONFIG_WINDOW_WIDTH
		|XCB_CONFIG_WINDOW_HEIGHT|XCB_CONFIG_WINDOW_BORDER_WIDTH;

	DBG("window: %d - location: %i,%i - size: %ix%i", c->win, x, y, w, h);
	c->x = c->old_x = x, c->y = c->old_y = y, c->w = c->old_w = w, c->h = c->old_h = h;
	if (c->ws && (nexttiled(c->ws->clients) == c && !nexttiled(c->next)) && !c->floating && !c->fullscreen) {
		DBG("only one tiled client on this workspace, removing borders");
		v[2] = W(c), v[3] = H(c), v[4] = 0;
	}
	xcb_configure_window(con, c->win, mask, v);
	configure(c);
	xcb_aux_sync(con);
}

void resizehint(Client *c, int x, int y, int w, int h, int interact)
{
	if (setsizehints(c, &x, &y, &w, &h, interact))
		resize(c, x, y, w, h);
}

void restack(Workspace *ws)
{
	Client *c;

	if (!(c = ws->sel))
		return;
	DBG("restacking clients on workspace: %d", ws->num);
	if (c->floating || !ws->layout)
		xcb_configure_window(con, c->win, XCB_CONFIG_WINDOW_STACK_MODE, (uint []){ XCB_STACK_MODE_ABOVE });
	if (ws->layout) {
		FOR_EACH(c, ws->stack)
			if (!c->floating && c->ws == c->ws->mon->ws)
				xcb_configure_window(con, c->win, XCB_CONFIG_WINDOW_STACK_MODE, (uint []){ XCB_STACK_MODE_BELOW });
	}
}

void runcmd(const Arg *arg)
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

void send(const Arg *arg)
{
	Client *c;

	if ((c = selws->sel) && arg->ui != selws->num) {
		unfocus(c, 1);
		detach(c, 0);
		detachstack(c);
		setclientws(c, arg->ui);
		attach(c, 0);
		attachstack(c);
		focus(NULL);
		layoutws(NULL);
	}
}

int sendevent(Client *c, int wmproto)
{
	int n, exists = 0;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t rpc;
	xcb_client_message_event_t cme;
	xcb_icccm_get_wm_protocols_reply_t proto;

	rpc = xcb_icccm_get_wm_protocols(con, c->win, wmatoms[Protocols]);
	if (xcb_icccm_get_wm_protocols_reply(con, rpc, &proto, &e)) {
		n = proto.atoms_len;
		while (!exists && n--)
			exists = proto.atoms[n] == wmatoms[wmproto];
		xcb_icccm_get_wm_protocols_reply_wipe(&proto);
	} else {
		checkerror("unable to get wm protocol for requested send event", e);
	}

	if (exists) {
		cme.response_type = XCB_CLIENT_MESSAGE;
		cme.window = c->win;
		cme.type = wmatoms[Protocols];
		cme.format = 32;
		cme.data.data32[0] = wmatoms[wmproto];
		cme.data.data32[1] = XCB_TIME_CURRENT_TIME;
		xcb_send_event(con, 0, c->win, XCB_EVENT_MASK_NO_EVENT, (const char *)&cme);
	}
	return exists;
}

void sendmon(Client *c, Monitor *m)
{
	if (c->ws->mon == m)
		return;
	unfocus(c, 1);
	detach(c, 0);
	detachstack(c);
	setclientws(c, m->ws->num);
	attach(c, 0);
	attachstack(c);
	focus(NULL);
	layoutws(NULL);
}

void setclientframeextents(Client *c, uint width)
{
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, c->win, netatoms[FrameExtents], XCB_ATOM_CARDINAL, 32, 4, (uint []){ width, width, width, width });
}

void setwinstate(xcb_window_t win, long state)
{
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, win, wmatoms[WMState], wmatoms[WMState], 32, 2, (long []){state, XCB_ATOM_NONE});
}

void setclientws(Client *c, uint num)
{
	DBG("setting client atom -- _NET_WM_DESKTOP: %d", num);
	c->ws = itows(num);
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, c->win, netatoms[Desktop], XCB_ATOM_CARDINAL, 32, 1, &num);
}

void setfocus(Client *c)
{
	if (!c->nofocus) {
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, c->win, XCB_CURRENT_TIME);
		xcb_change_property(con, XCB_PROP_MODE_REPLACE, root, netatoms[ActiveWindow], XCB_ATOM_WINDOW, 32, 1, &c->win);
	}
	sendevent(c, TakeFocus);
}

void setfullscreen(Client *c, int fullscreen)
{
	Monitor *m;

	if (!c->ws || !(m = c->ws->mon))
		m = selws->mon;
	if (fullscreen && !c->fullscreen) {
		xcb_change_property(con, XCB_PROP_MODE_REPLACE, c->win, netatoms[State], XCB_ATOM_ATOM, 32, 1, (uchar *)&netatoms[Fullscreen]);
		c->oldstate = c->floating;
		c->fullscreen = 1;
		c->old_bw = c->bw;
		c->bw = 0;
		c->floating = 1;
		resize(c, m->x, m->y, m->w, m->h);
		xcb_configure_window(con, c->win, XCB_CONFIG_WINDOW_STACK_MODE, (uint []){ XCB_STACK_MODE_ABOVE });
	} else if (!fullscreen && c->fullscreen) {
		xcb_change_property(con, XCB_PROP_MODE_REPLACE, c->win, netatoms[State], XCB_ATOM_ATOM, 32, 0, (uchar *)0);
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

void setlayout(const Arg *arg)
{
	DBG("setting current monitor layout");
	if (arg && arg->v)
		selws->layout = (void (*)(Workspace *))arg->v;
	if (selws->sel)
		layoutws(selws);
}

void setnmaster(const Arg *arg)
{
	selws->nmaster = MAX(selws->nmaster + arg->i, 0);
	layoutws(selws);
}

int setsizehints(Client *c, int *x, int *y, int *w, int *h, int interact)
{
	int baseismin;
	Monitor *m = c->ws->mon;

	/* set minimum possible */
	*w = MAX(1, *w);
	*h = MAX(1, *h);
	if (interact) { /* don't confine */
		if (*x > scr_w)
			*x = scr_w - W(c);
		if (*y > scr_h)
			*y = scr_h - H(c);
		if (*x + *w + 2 * c->bw < 0)
			*x = 0;
		if (*y + *h + 2 * c->bw < 0)
			*y = 0;
	} else { /* confine to monitor */
		if (*x > m->winarea_x + m->winarea_w)
			*x = m->winarea_x + m->winarea_w - W(c);
		if (*y > m->winarea_y + m->winarea_h)
			*y = m->winarea_y + m->winarea_h - H(c);
		if (*x + *w + 2 * c->bw < m->winarea_x)
			*x = m->winarea_x;
		if (*y + *h + 2 * c->bw < m->winarea_y)
			*y = m->winarea_y;
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
		if (c->increment_w)
			*w -= *w % c->increment_w;
		if (c->increment_h)
			*h -= *h % c->increment_h;
		/* restore base dimensions */
		*w += c->base_w;
		*h += c->base_h;
		*w = MAX(*w, c->min_w);
		*h = MAX(*h, c->min_h);
		if (c->max_w)
			*w = MIN(*w, c->max_w);
		if (c->max_h)
			*h = MIN(*h, c->max_h);
	}
	return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

void setsplit(const Arg *arg)
{
	float f;

	if (!arg || !selws->layout)
		return;
	f = arg->f < 1.0 ? arg->f + selws->splitratio : arg->f - 1.0;
	if (f < 0.1 || f > 0.9)
		return;
	DBG("setting split splitratio: %f -> %f", selws->splitratio, f);
	selws->splitratio = f;
	layoutws(selws);
}

void showhide(Client *c)
{
	xcb_generic_event_t *ev;

	if (!c)
		return;
	if (c->ws == c->ws->mon->ws) {
		DBG("showing client: %d - workspace: %d", c->win, c->ws->num);
		xcb_configure_window(con, c->win, XCB_CONFIG_WINDOW_X|XCB_CONFIG_WINDOW_Y, (uint []){ c->x, c->y });
		if ((!c->ws->layout || c->floating) && !c->fullscreen)
			resizehint(c, c->x, c->y, c->w, c->h, 0);
		showhide(c->snext);
	} else {
		showhide(c->snext);
		DBG("hiding client: %d - workspace: %d", c->win, c->ws->num);
		xcb_configure_window(con, c->win, XCB_CONFIG_WINDOW_X|XCB_CONFIG_WINDOW_Y, (uint []){ W(c) * -2, c->y });
	}
	xcb_aux_sync(con);
	while ((ev = xcb_poll_for_queued_event(con)))
		free(ev);
}

void sigchld(int unused)
{
	(void)(unused);
	if (signal(SIGCHLD, sigchld) == SIG_ERR)
		errx(1, "can't install SIGCHLD handler");
	while (waitpid(-1, NULL, WNOHANG) > 0)
		;
}

void sizehints(Client *c)
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
		checkerror("unable to get wm normal hints", e);
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

size_t strlcat(char *dst, const char *src, size_t size)
{
	size_t n = size, dlen;
	const char *odst = dst;
	const char *osrc = src;

	while (n-- != 0 && *dst != '\0')
		dst++;
	dlen = dst - odst;
	n = size - dlen;

	if (n-- == 0)
		return dlen + strlen(src);
	while (*src != '\0') {
		if (n != 0) {
			*dst++ = *src;
			n--;
		}
		src++;
	}
	*dst = '\0';

	return dlen + (src - osrc);
}

size_t strlcpy(char *dst, const char *src, size_t size)
{
	size_t n = size;
	const char *osrc = src;

	if (n != 0)
		while (--n != 0)
			if ((*dst++ = *src++) == '\0')
				break;
	if (n == 0) {
		if (size != 0)
			*dst = '\0';
		while (*src++);
	}
	return src - osrc - 1;
}

void swapclient(const Arg *arg)
{
	Client *c;

	(void)(arg);
	if (!(c = selws->sel) || c->floating || !selws->layout
			|| (c == nexttiled(selws->clients) && (!c || !(c = nexttiled(c->next)))))
		return;
	DBG("swapping current client window: %d", c->win);
	detach(c, 1);
	focus(c);
	layoutws(c->ws);
}

void tile(Workspace *ws)
{
	Client *c;
	Monitor *m = ws->mon;
	uint i, n, h, mw, my, ty, iter;

	for (n = 0, c = nexttiled(ws->clients); c; c = nexttiled(c->next), n++)
		;
	if (!n)
		return;
	DBG("tiling workspace: %d", ws->num);
	if (n > ws->nmaster)
		mw = ws->nmaster ? m->winarea_w * ws->splitratio : 0;
	else
		mw = m->winarea_w;
	for (i = my = ty = 0, c = nexttiled(ws->clients); c; c = nexttiled(c->next), ++i)
		if (i < ws->nmaster) {
			iter = MIN(n, ws->nmaster) - i;
			h = (m->winarea_h - my) / MAX(1, iter);
			resize(c, m->winarea_x, m->winarea_y + my, mw - (2*c->bw), h - (2*c->bw));
			my += H(c);
		} else {
			iter = n - i;
			h = (m->winarea_h - ty) / MAX(1, iter);
			resize(c, m->winarea_x + mw, m->winarea_y + ty, m->winarea_w - mw - (2*c->bw), h - (2*c->bw));
			ty += H(c);
		}
}

void togglefloat(const Arg *arg)
{
	Client *c;

	if (!(c = selws->sel) || c->fullscreen)
		return;
	(void)(arg);
	DBG("toggling selected window floating state: %d -> %d", c->floating, !c->floating);
	if ((c->floating = !c->floating || c->fixed))
		resizehint(c, (c->x = c->old_x), (c->y = c->old_y), (c->w = c->old_w), (c->h = c->old_h), 0);
	else
		c->old_x = c->x, c->old_y = c->y, c->old_w = c->w, c->old_h = c->h;
	layoutws(selws);
}

void unfocus(Client *c, int focusroot)
{
	if (!c)
		return;
	DBG("unfocusing client: %d", c->win);
	xcb_change_window_attributes(con, c->win, XCB_CW_BORDER_PIXEL, &borders[Unfocus]);
	if (focusroot) {
		DBG("focusing root window");
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
		xcb_delete_property(con, root, netatoms[ActiveWindow]);
	}
}

#ifdef PANEL
void applypanelstrut(Panel *p)
{
	DBG("%s window area before: %d,%d @ %dx%d",
			p->mon->name, p->mon->winarea_x, p->mon->winarea_y, p->mon->winarea_w, p->mon->winarea_h);
	if (p->mon->x + p->strut_l > p->mon->winarea_x)
		p->mon->winarea_x = p->strut_l;
	if (p->mon->y + p->strut_t > p->mon->winarea_y)
		p->mon->winarea_y = p->strut_t;
	if (p->mon->w - (p->strut_r + p->strut_l) < p->mon->winarea_w)
		p->mon->winarea_w = p->mon->w - (p->strut_r + p->strut_l);
	if (p->mon->h - (p->strut_b + p->strut_t) < p->mon->winarea_h)
		p->mon->winarea_h = p->mon->h - (p->strut_b + p->strut_t);
	DBG("%s window area after: %d,%d @ %dx%d",
			p->mon->name, p->mon->winarea_x, p->mon->winarea_y, p->mon->winarea_w, p->mon->winarea_h);
}

void updatestruts(Panel *p, int apply)
{
	Panel *n;
	Monitor *m;
	
	DBG("resetting struts for each monitor");
	FOR_EACH(m, monitors) {
		m->winarea_x = m->x, m->winarea_y = m->y, m->winarea_w = m->w, m->winarea_h = m->h;
	}
	if (!p)
		return;
	if (apply && !panels)
		applypanelstrut(p);
	DBG("applying each panel strut where needed");
	FOR_EACH(n, panels)
		if ((apply || n != p) && (n->strut_l || n->strut_r || n->strut_t || n->strut_b))
			applypanelstrut(p);
}

Panel *wintopanel(xcb_window_t win)
{
	Panel *p;

	if (win == root)
		return NULL;
	FOR_EACH(p, panels)
		if (p->win == win)
			return p;
	return NULL;
}
#endif

void view(const Arg *arg)
{
	Workspace *ws;

	if (arg->ui == selws->num || !(ws = itows(arg->ui)))
		return;
	DBG("viewing workspace: %d", arg->ui);
	selws = ws;
	selws->mon->ws = ws;
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, root, netatoms[CurrentDesktop], XCB_ATOM_CARDINAL, 32, 1, (uchar *)&arg->ui);
	focus(NULL);
	layoutws(NULL);
}

xcb_get_window_attributes_reply_t *windowattr(xcb_window_t win)
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

int windowgeom(xcb_window_t win, int *x, int *y, int *w, int *h, int *bw)
{
	xcb_generic_error_t *e;
	xcb_get_geometry_reply_t *g;

	if (!(g = xcb_get_geometry_reply(con, xcb_get_geometry(con, win), &e))) {
		checkerror("failed to get window geometry", e);
		return 0;
	}
	if (x)
		*x = g->x;
	if (y)
		*y = g->y;
	if (w)
		*w = g->width;
	if (h)
		*h = g->height;
	if (bw)
		*bw = g->border_width;
	return 1;
}

void windowhints(Client *c)
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
	} else {
		checkerror("unable to get wm window hints", e);
	}
}

xcb_atom_t windowprop(xcb_window_t win, xcb_atom_t prop)
{
	xcb_atom_t ret;
	xcb_generic_error_t *e;
	xcb_get_property_reply_t *r;
	xcb_get_property_cookie_t c;

	c = xcb_get_property(con, 0, win, prop, XCB_ATOM_ANY, 0, 1);
	ret = -1;
	DBG("getting window property atom %d from window: %d", prop, win);
	if ((r = xcb_get_property_reply(con, c, &e))) {
		if (xcb_get_property_value_length(r))
			ret = *(xcb_atom_t *)xcb_get_property_value(r);
		free(r);
	} else {
		checkerror("unable to get window property", e);
	}
	return ret;
}

xcb_window_t windowtrans(xcb_window_t win)
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

void windowtype(Client *c)
{
	DBG("checking window type for window: %d", c->win);
	if (windowprop(c->win, netatoms[State]) == netatoms[Fullscreen])
		setfullscreen(c, 1);
	else if (windowprop(c->win, netatoms[WindowType]) == netatoms[WindowTypeDialog])
		c->floating = 1;
}

Client *wintoclient(xcb_window_t win)
{
	Client *c;
	Workspace *ws;

	if (win == root)
		return NULL;
	FOR_WSCLIENTS(c, ws)
		if (c->win == win)
			return c;
	return NULL;
}

Workspace *wintows(xcb_window_t win)
{
	Client *c;
	Workspace *ws;

	if (win == root)
		return selws;
	FOR_WSCLIENTS(c, ws)
		if (c->win == win)
			return ws;
	return selws;
}
