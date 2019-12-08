/*
* In order to compile you will need the xcb headers, then run
*     cc wm.c -Wall -o wm -lxcb -lxcb-keysyms -lxcb-util
*/

#include <err.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <locale.h>
#include <sys/wait.h>

#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xcb_util.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_cursor.h>
#include <xcb/xcb_keysyms.h>
/* #include <xcb/randr.h> */

/* for whatever reason xcb_keysyms.h doesn't have any actual keysym definitions */
#include <X11/keysym.h>

#define FOCUSCOL      0x999999
#define UNFOCUSCOL    0x000000
#define MODKEY        XCB_MOD_MASK_1
#define W(X)          ((X)->w + 2 * (X)->bw)
#define H(X)          ((X)->h + 2 * (X)->bw)
#define MAX(A, B)     ((A) > (B) ? (A) : (B))
#define MIN(A, B)     ((A) < (B) ? (A) : (B))
#define LEN(x)        (sizeof(x) / sizeof(x[0]))
#define VIS(C)        (C->workspace == C->mon->workspace)
#define KEY(k)        (k & ~(numlockmask|XCB_MOD_MASK_LOCK))
#define BUTTONMASK    XCB_EVENT_MASK_BUTTON_PRESS|XCB_EVENT_MASK_BUTTON_RELEASE
#define MOTIONMASK    XCB_EVENT_MASK_BUTTON_RELEASE|XCB_EVENT_MASK_BUTTON_MOTION|XCB_EVENT_MASK_POINTER_MOTION_HINT

typedef union Arg Arg;
typedef struct Key Key;
typedef struct Client Client;
typedef struct Monitor Monitor;

enum Cursors {
	Normal, Move, Resize, CurLast
};

/* enum WMAtoms { */
/* 	WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast */
/* }; */

/* enum EWMHAtoms { */
/* 	NetSupported, NetWMName, NetWMState, NetWMCheck, NetWMFullscreen, NetNumDesktops, */
/* 	NetCurrentDesktop, NetActiveWindow, NetWMWindowType, NetWMWindowTypeDialog, NetClientList, NetLast */
/* }; */


union Arg {
	int i;
	unsigned int ui;
	float f;
	const void *v;
};

struct Key {
	unsigned int mod;
	xcb_keysym_t keysym;
	void (*func)(const Arg *);
	const Arg arg;
};

struct Client {
	float min_aspect, max_aspect;
	int x, y, w, h, bw, workspace;
	int old_x, old_y, old_w, old_h, old_bw;
	int base_w, base_h, increment_w, increment_h, max_w, max_h, min_w, min_h;
	int fixed, tiled, urgent, oldstate;
	Client *next, *snext;
	Monitor *mon;
	xcb_window_t win;
};

struct Monitor {
	int num, workspace;
	int x, y, w, h;
	int winarea_x, winarea_y, winarea_w, winarea_h;
	Client *clients, *stack, *sel;
	Monitor *next;
};

/* static const char *WMAtomNames[] = { */
/* 	[WMState] = "WM_STATE", */
/* 	[WMDelete] = "WM_DELETE_WINDOW", */
/* 	[WMProtocols] = "WM_PROTOCOLS", */
/* 	[WMTakeFocus] = "WM_TAKE_FOCUS" */
/* }; */

/* static const char *EWMHAtomNames[] = { */
/* 	[NetWMName] = "_NET_WM_NAME", */
/* 	[NetWMState] = "_NET_WM_STATE", */
/* 	[NetSupported] = "_NET_SUPPORTED", */
/* 	[NetClientList] = "_NET_CLIENT_LIST", */
/* 	[NetActiveWindow] = "_NET_ACTIVE_WINDOW", */
/* 	[NetWMCheck] = "_NET_SUPPORTING_WM_CHECK", */
/* 	[NetWMWindowType] = "_NET_WM_WINDOW_TYPE", */
/* 	[NetCurrentDesktop] = "_NET_CURRENT_DESKTOP", */
/* 	[NetNumDesktops] = "_NET_NUMBER_OF_DESKTOPS", */
/* 	[NetWMFullscreen] = "_NET_WM_STATE_FULLSCREEN", */
/* 	[NetWMWindowTypeDialog] = "_NET_WM_WINDOW_TYPE_DIALOG", */
/* }; */

static const char *cursors[] = {
	[Move] = "fleur",
	[Normal] = "arrow",
	[Resize] = "sizing"
};

static char *argv0;
static int scr_w;
static int scr_h;
static int running = 1;
static unsigned int border = 1;
static unsigned int numlockmask = 0;
static unsigned int mousebtn = 0;

static xcb_window_t root;
static xcb_screen_t *scr;
static Monitor *mons, *selmon;
static xcb_cursor_t cursor[CurLast];
static xcb_connection_t *conn = NULL;
static xcb_key_symbols_t *keysyms = NULL;

static xcb_atom_t WMStateAtom;

static void arrange(Monitor *m);
static void attach(Client *c);
static void attach_stack(Client *c);
static void checkerr(xcb_generic_error_t *e, int line);
static void cleanup(void);
static void clientgeom(Client *c);
static xcb_window_t clienttrans(Client *c);
static void config(Client *c);
static void detach(Client *c);
static void detachstack(Client *c);
static void eventloop(void);
static void existing(void);
static void focusclient(Client *c);
static void freeclient(Client *c, int destroyed);
static void freemon(Monitor *m);
static xcb_atom_t initatom(const char *name);
static void initbinds(void);
static void initclient(xcb_window_t win);
static void initscreen(void);
static Monitor *initmon(void);
static int initmons(void);
static void initwm(void);
static void numlock(void);
static Monitor *ptrtomon(int x, int y);
static void quit(const Arg *arg);
static void reset(const Arg *arg);
static void resize(Client *c, int x, int y, int w, int h);
static void restack(Monitor *m);
static void setfield(int *dst, int val, int *old);
static void showhide(Client *c);
static void sigchld(int unused);
static void sizehints(Client *c);
static void spawn(const Arg *arg);
static void unfocus(Client *c, int setfocus);
static xcb_atom_t windowstate(xcb_window_t win);
static Client *wintoclient(xcb_window_t win);
static Monitor *wintomon(xcb_window_t win);

#ifdef DEBUG
static void debug(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
}
#define DBG(fmt, ...) debug("%s:%d - " fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__);
#else
#define DBG(fmt, ...)
#endif

const Key keys[] = {
	/* mod(s)                    keysym    function   arg */
	{ MODKEY|XCB_MOD_MASK_SHIFT, XK_q,      quit,     {0} },
	{ MODKEY|XCB_MOD_MASK_SHIFT, XK_r,      reset,    {0} },
	{ MODKEY|XCB_MOD_MASK_SHIFT, XK_Return, spawn,    {.v = (char *[]){"st", NULL}} },
	{ MODKEY,                    XK_p,      spawn,    {.v = (char *[]){"dmenu_run", NULL}} },
};

static void arrange(Monitor *m)
{
	DBG("arranging monitor");
	if (m)
		showhide(m->stack);
	else for (m = mons; m; m = m->next)
		showhide(m->stack);
	if (m)
		restack(m);
	/* else */
	/* 	for (m = mons; m; m = m->next) */
	/* 		restack(m); */
}

static void attach(Client *c)
{
	c->next = c->mon->clients;
	c->mon->clients = c;
}

static void attach_stack(Client *c)
{
	c->snext = c->mon->stack;
	c->mon->stack = c;
}

static void checkerr(xcb_generic_error_t *e, int line)
{
	if (!e)
		return;
	const char *err = xcb_event_get_error_label(e->error_code);
	fprintf(stderr, "%s:%d - request error %i, \"%s\"\n", __FILE__, line, (int)e->error_code, err);
	free(e);
	exit(1);
}

static void cleanup(void)
{
	Monitor *m;
	unsigned int i;

	if (conn) {
		for (m = mons; m; m = m->next)
			while (m->stack)
				freeclient(m->stack, 0);
		xcb_ungrab_key(conn, XCB_GRAB_ANY, root, XCB_MOD_MASK_ANY);
		xcb_key_symbols_free(keysyms);
		while (mons)
			freemon(mons);
		for (i = 0; i < LEN(cursors); i++)
			xcb_free_cursor(conn, cursor[i]);
		xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, XCB_INPUT_FOCUS_POINTER_ROOT, XCB_CURRENT_TIME);
		xcb_flush(conn);
		xcb_disconnect(conn);
	}
}

static void clientgeom(Client *c)
{
	xcb_get_geometry_reply_t *g;

	if ((g = xcb_get_geometry_reply(conn, xcb_get_geometry(conn, c->win), NULL))) {
		setfield(&c->w, g->width, &c->old_w);
		setfield(&c->h, g->height, &c->old_h);
		setfield(&c->x, g->x + c->mon->x > 0 ? g->x + c->mon->x : c->mon->x + (c->mon->w - c->w / 2), &c->old_x);
		setfield(&c->y, g->y + c->mon->y > 0 ? g->y + c->mon->y : c->mon->y + (c->mon->h - c->h / 2), &c->old_y);
		setfield(&c->bw, g->border_width, &c->old_bw);
		free(g);
	} else {
		setfield(&c->w, c->mon->w / 2, &c->old_w);
		setfield(&c->h, c->mon->h / 2, &c->old_h);
		setfield(&c->x, c->mon->x + (c->mon->w - c->w / 2), &c->old_x);
		setfield(&c->y, c->mon->y + (c->mon->h - c->h / 2), &c->old_y);
		setfield(&c->bw, border, &c->old_bw);
	}
}

static xcb_window_t clienttrans(Client *c)
{
	Client *t;
	xcb_get_property_cookie_t pc;
	xcb_window_t trans = XCB_WINDOW_NONE;

	pc = xcb_icccm_get_wm_transient_for_unchecked(conn, c->win);
	if (xcb_icccm_get_wm_transient_for_reply(conn, pc, &trans, NULL) && (t = wintoclient(trans))) {
		c->workspace = t->workspace;		
		c->mon = t->mon;
	}
	return trans;
}

static void config(Client *c)
{
	xcb_configure_notify_event_t *ce;

	DBG("sending configure notify event to managed client");

	/* allocate size for the event struct, see xcb_send_event(3) */
	ce = calloc(32, 1);
	ce->event = c->win;
	ce->window = c->win;
	ce->response_type = XCB_CONFIGURE_NOTIFY;
	ce->x = c->x;
	ce->y = c->y;
	ce->width = c->w;
	ce->height = c->h;
	ce->border_width = c->bw;
	ce->above_sibling = XCB_NONE;
	ce->override_redirect = 0;
	xcb_send_event(conn, 0, c->win, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (char *)ce);
	xcb_flush(conn);
	free(ce);
}

static void detach(Client *c)
{
	Client **tc;

	for (tc = &c->mon->clients; *tc && *tc != c; tc = &(*tc)->next);
	*tc = c->next;
}

static void detachstack(Client *c)
{
	Client **tc, *t;

	for (tc = &c->mon->stack; *tc && *tc != c; tc = &(*tc)->snext);
	*tc = c->snext;

	if (c == c->mon->sel) {
		for (t = c->mon->stack; t && !VIS(t); t = t->snext);
		c->mon->sel = t;
	}
}

static void eventloop(void)
{
	Client *c;
	Monitor *m;
	unsigned int i;
	xcb_window_t win = 0;
	xcb_generic_event_t *ev;
	xcb_get_geometry_reply_t *g;

	while (running && (ev = xcb_wait_for_event(conn))) {
		switch ((ev->response_type & ~0x80)) {
		case XCB_FOCUS_IN:
			{
				xcb_focus_in_event_t *e = (xcb_focus_in_event_t *)ev;
				if (selmon->sel && e->event != selmon->sel->win) {
					DBG("focus in event for unfocused window: %d", e->event);
					if ((c = wintoclient(e->event)))
						focusclient(c);
					else
						xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, e->event, XCB_CURRENT_TIME);
				}
				break;
			}
		case XCB_FOCUS_OUT:
			{
				xcb_focus_out_event_t *e = (xcb_focus_out_event_t *)ev;
				if (selmon->sel && e->event == selmon->sel->win) {
					DBG("focus out event for focused window: %d", e->event);
					unfocus(selmon->sel, 1);
				}
				break;
			}
		case XCB_CONFIGURE_NOTIFY:
			{
				xcb_configure_notify_event_t *e = (xcb_configure_notify_event_t *)ev;
				if (e->window == root) {
					DBG("root window configure notify event");
					scr_w = e->width;
					scr_h = e->height;
					if (initmons()) {
						focusclient(NULL);
						arrange(NULL);
					}
				}
				break;
			}
		case XCB_CONFIGURE_REQUEST:
			{
				xcb_params_configure_window_t wc;
				uint16_t xy = XCB_CONFIG_WINDOW_X|XCB_CONFIG_WINDOW_Y;
				uint16_t wh = XCB_CONFIG_WINDOW_WIDTH|XCB_CONFIG_WINDOW_HEIGHT;
				xcb_configure_request_event_t *e = (xcb_configure_request_event_t *)ev;

				if ((c = wintoclient(e->window))) {
					DBG("configure request event for managed client");
					if (e->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH)
						c->bw = e->border_width;
					else if (!c->tiled) {
						DBG("configure request client is floating");
						m = c->mon;
						if (e->value_mask & XCB_CONFIG_WINDOW_X)       setfield(&c->x, m->x + e->x, &c->old_x);
						if (e->value_mask & XCB_CONFIG_WINDOW_Y)       setfield(&c->y, m->y + e->y, &c->old_y);
						if (e->value_mask & XCB_CONFIG_WINDOW_WIDTH)   setfield(&c->w, e->width, &c->old_w);
						if (e->value_mask & XCB_CONFIG_WINDOW_HEIGHT)  setfield(&c->h, e->height, &c->old_h);
						if ((c->x + c->w) > m->x + m->w)               c->x = m->x + (m->w / 2 - c->w / 2);
						if ((c->y + c->h) > m->y + m->h)               c->y = m->y + (m->h / 2 - c->h / 2);
						if ((e->value_mask & (xy)) && !(e->value_mask & (wh)))
							config(c);
						if (VIS(c)) {
							uint32_t g[] = { c->x, c->y, c->w, c->h };
							xcb_configure_window(conn, c->win, xy|wh, g);
						}
					} else {
						DBG("configure request client is tiled");
						config(c);
					}
				} else {
					DBG("configure request event for unmanaged client");
					wc.x = e->x;
					wc.y = e->y;
					wc.width = e->width;
					wc.height = e->height;
					wc.sibling = e->sibling;
					wc.stack_mode = e->stack_mode;
					wc.border_width = e->border_width;
					xcb_configure_window(conn, e->window, e->value_mask, &wc);
				}
				break;
			}
		case XCB_DESTROY_NOTIFY:
			{
				xcb_destroy_notify_event_t *e = (xcb_destroy_notify_event_t *)ev;
				if ((c = wintoclient(e->window))) {
					DBG("freeing managed client");
					freeclient(c, 1);
				} else {
					DBG("destroy notify event for unmanaged client");
				}
				break;
			}
		case XCB_ENTER_NOTIFY:
			{
				xcb_enter_notify_event_t *e = (xcb_enter_notify_event_t *)ev;
				DBG("enter notify event");
				if (e->event != root && (e->mode != XCB_NOTIFY_MODE_NORMAL || e->detail == XCB_NOTIFY_DETAIL_INFERIOR))
					break;
				c = wintoclient(e->event);
				if ((m = c ? c->mon : wintomon(e->event)) != selmon) {
					unfocus(selmon->sel, 1);
					selmon = m;
				} else if (!c || c == selmon->sel)
					break;
				focusclient(c);
				break;
			}
		case XCB_BUTTON_PRESS:
			{
				xcb_button_press_event_t *b = (xcb_button_press_event_t *)ev;
				win = b->child;
				if (!(c = wintoclient(win)))
					break;
				if (b->detail == XCB_BUTTON_INDEX_1 || b->detail == XCB_BUTTON_INDEX_3) {
					DBG("button press event for button: %d", win);
					focusclient(c);
					restack(selmon);
					if (!(g = xcb_get_geometry_reply(conn, xcb_get_geometry_unchecked(conn, win), NULL))) {
						DBG("unable to get window geometry");
						break;
					}
					if (b->detail == XCB_BUTTON_INDEX_1) {
						mousebtn = 1;
						xcb_warp_pointer(conn, XCB_NONE, win, 0, 0, 0, 0, g->width/2, g->height/2);
					} else {
						mousebtn = 3;
						xcb_warp_pointer(conn, XCB_NONE, win, 0, 0, 0, 0, g->width, g->height);
					}
					xcb_grab_pointer(conn, 0, root, MOTIONMASK, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, root, cursor[b->detail == 1 ? Move : Resize], XCB_CURRENT_TIME);
					free(g);
				}
				break;
			}
		case XCB_BUTTON_RELEASE:
			{
				DBG("button release event");
				xcb_ungrab_pointer(conn, XCB_CURRENT_TIME);
				win = 0;
				mousebtn = 0;
				break;
			}
		case XCB_KEY_PRESS:
			{
				xcb_key_press_event_t *e = (xcb_key_press_event_t*)ev;
				xcb_keysym_t sym = xcb_key_press_lookup_keysym(keysyms, e, 0);
				for (i = 0; i < LEN(keys); i++)
					if (sym == keys[i].keysym && KEY(keys[i].mod) == KEY(e->state) && keys[i].func) {
						DBG("key press event for key: %u - modifier: %u", e->detail, KEY(keys[i].mod));
						keys[i].func(&(keys[i].arg));
						break;
					}
				break;
			}
		case XCB_MAP_REQUEST:
			{
				xcb_get_window_attributes_reply_t *wa;
				xcb_map_request_event_t *e = (xcb_map_request_event_t *)ev;
				DBG("map request event for window: %i", e->window);
				if (!(wa = xcb_get_window_attributes_reply(conn, xcb_get_window_attributes_unchecked(conn, e->window), NULL)) || wa->override_redirect) {
					DBG("unable to get window attributes or window has override_redirect set, not managing");
					break;
				}
				if (!wintoclient(e->window))
					initclient(e->window);
				free(wa);
				break;
			}
		case XCB_UNMAP_NOTIFY:
			{
				xcb_unmap_notify_event_t *e = (xcb_unmap_notify_event_t *)ev;
				DBG("unmap notify event for window: %i", e->window);
				if ((c = wintoclient(e->window))) {
					DBG("window is a managed client, freeing")
					freeclient(c, 0);
				}
				break;
			}
		case XCB_MOTION_NOTIFY:
			{
				xcb_motion_notify_event_t *m = (xcb_motion_notify_event_t *)ev;
				if (win && (mousebtn == 1 || mousebtn == 3)) {
					DBG("motion notify event button: %d", mousebtn);
					if (!(g = xcb_get_geometry_reply(conn, xcb_get_geometry_unchecked(conn, win), NULL))) {
						DBG("unable to get window geometry");
						break;
					}
					uint32_t v[] = { g->x, g->y, g->width, g->height };
					if (mousebtn == 1) {
						v[0] = m->root_x - g->width / 2;
						v[1] = m->root_y - g->height / 2;
					} else {
						v[2] = m->root_x - g->x;
						v[3] = m->root_y - g->y;
					}
					xcb_configure_window(conn, win, XCB_CONFIG_WINDOW_X|XCB_CONFIG_WINDOW_Y|XCB_CONFIG_WINDOW_WIDTH|XCB_CONFIG_WINDOW_HEIGHT, v);
					DBG("%s window to: %d%s%d", mousebtn == 1 ? "moved" : "resized", v[0], mousebtn == 1 ? "," : "x", v[1]);
					free(g);
				}
				break;
			}
		/* case XCB_CLIENT_MESSAGE: */
		/* 	{ */
		/* 		/1* handle fullscreen and active window *1/ */
		/* 	} */
		/* case XCB_PROPERTY_NOTIFY: */
		/* 	{ */
		/* 		/1* update window hints and type window *1/ */
		/* 	} */
		}
		xcb_flush(conn);
		free(ev);
	}
}

static void existing(void)
{
	unsigned int i, num;
	xcb_generic_error_t *err;
	xcb_window_t *wins = NULL;

	xcb_query_tree_reply_t *query_reply = xcb_query_tree_reply(conn, xcb_query_tree(conn, root), &err);
	num = query_reply->children_len;
	wins = xcb_query_tree_children(query_reply);

	for (i = 0; i < num; i++) {
		xcb_get_window_attributes_reply_t *ga_reply = xcb_get_window_attributes_reply(conn, xcb_get_window_attributes(conn, wins[i]), &err);
		checkerr(err, __LINE__);
		if (ga_reply->override_redirect)
			continue;
		xcb_window_t trans_reply = XCB_NONE;
		xcb_icccm_get_wm_transient_for_reply(conn, xcb_icccm_get_wm_transient_for(conn, wins[i]), &trans_reply, &err);
		checkerr(err, __LINE__);
		if (trans_reply != XCB_NONE)
			continue;
		if(ga_reply->map_state == XCB_MAP_STATE_VIEWABLE || windowstate(wins[i]) == XCB_ICCCM_WM_STATE_ICONIC)
			initclient(wins[i]);
		free(ga_reply);
	}
	for (i = 0; i < num; i++) { /* now the transients */
		xcb_get_window_attributes_reply_t *ga_reply = xcb_get_window_attributes_reply(conn, xcb_get_window_attributes(conn, wins[i]), &err);
		checkerr(err, __LINE__);
		xcb_window_t trans_reply = XCB_NONE;
		xcb_icccm_get_wm_transient_for_reply(conn, xcb_icccm_get_wm_transient_for(conn, wins[i]), &trans_reply, &err);
		checkerr(err, __LINE__);
		if (trans_reply != XCB_NONE && (ga_reply->map_state == XCB_MAP_STATE_VIEWABLE || windowstate(wins[i]) == XCB_ICCCM_WM_STATE_ICONIC))
			initclient(wins[i]);
		free(ga_reply);
	}
	if (query_reply)
		free(query_reply);	// this frees the whole thing, including wins
}

static void focusclient(Client *c)
{
	uint32_t v[] = { FOCUSCOL };

	if (!c || !VIS(c)) {
		DBG("focusclient passed null or client is not visible, searching for next closest");
		for (c = selmon->stack; c && !VIS(c); c = c->snext);
	}
	if (selmon->sel)
		unfocus(selmon->sel, 0);
	if (c) {
		DBG("focusing a client window: %d", c->win);
		if (c->mon != selmon)
			selmon = c->mon;
		xcb_change_window_attributes(conn, c->win, XCB_CW_BORDER_PIXEL, v);
		xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, c->win, XCB_CURRENT_TIME);
	} else {
		DBG("focusing the root window");
		xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
	}
	selmon->sel = c;
}

static void freeclient(Client *c, int destroyed)
{
	Monitor *m = c->mon;
	uint32_t v[] = { c->old_bw };
	long d[] = { XCB_ICCCM_WM_STATE_WITHDRAWN, XCB_ATOM_NONE };

	DBG("freeing client: %d - destroyed: %i", c->win, destroyed);
	detach(c);
	detachstack(c);
	if (!destroyed) {
		xcb_grab_server(conn);
		xcb_configure_window(conn, c->win, XCB_CONFIG_WINDOW_BORDER_WIDTH, v);
		xcb_ungrab_button(conn, XCB_BUTTON_INDEX_ANY, c->win, XCB_GRAB_ANY);
		xcb_change_property(conn, XCB_PROP_MODE_REPLACE, c->win, WMStateAtom, WMStateAtom, 32, 2, (unsigned char *)d);
		xcb_flush(conn);
		xcb_ungrab_server(conn);
	}
	free(c);
	focusclient(NULL);
	arrange(m);
}

static void freemon(Monitor *m)
{
	Monitor *mon;
	
	DBG("freeing monitor");
	if (m == mons)
		mons = mons->next;
	else {
		for (mon = mons; mon && mon->next != m; mon = mon->next);
		mon->next = m->next;
	}
	free(m);
}

static xcb_atom_t initatom(const char *name)
{
	xcb_atom_t atom = 0;
	xcb_intern_atom_reply_t *r;

	DBG("initializing atom: %s", name);
	if ((r = xcb_intern_atom_reply(conn, xcb_intern_atom_unchecked(conn, 0, strlen(name), name), NULL))) {
		atom = r->atom;
		free(r);
	}
	return atom;
}

static void initbinds(void)
{
	xcb_keycode_t *c;
	unsigned int i, j;

	numlock();
	{ /* new scope to use updated numlockmask */
		unsigned int btns[] = { XCB_BUTTON_INDEX_1, XCB_BUTTON_INDEX_2, XCB_BUTTON_INDEX_3 };
		unsigned int mods[] = { 0, XCB_MOD_MASK_LOCK, numlockmask, numlockmask|XCB_MOD_MASK_LOCK };

		xcb_ungrab_button(conn, XCB_BUTTON_INDEX_ANY, root, XCB_GRAB_ANY);
		xcb_ungrab_key(conn, XCB_GRAB_ANY, root, XCB_MOD_MASK_ANY);
		for (i = 0; i < LEN(mods); i++) {
			for (j = 0; j < LEN(btns); j++) {
				xcb_grab_button(conn, 0, root, BUTTONMASK, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, 0, XCB_NONE, btns[j], MODKEY|mods[i]);
				DBG("grabbing button: %u - modifier: %u", btns[j], MODKEY|mods[i]);
			}
			for (j = 0; j < LEN(keys); j++) {
				if ((c = xcb_key_symbols_get_keycode(keysyms, keys[j].keysym))) {
					xcb_grab_key(conn, 1, root, keys[j].mod|mods[i], *c, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
					DBG("grabbing key: %u - modifier: %u", *c, keys[j].mod|mods[i]);
					free(c);
				}
			}
		}
	}
}

static void initclient(xcb_window_t win)
{
	Client *c;
	long d[] = { XCB_ICCCM_WM_STATE_NORMAL, XCB_ATOM_NONE };
	uint16_t mask = XCB_CONFIG_WINDOW_X|XCB_CONFIG_WINDOW_Y|XCB_CONFIG_WINDOW_WIDTH|XCB_CONFIG_WINDOW_HEIGHT|XCB_CONFIG_WINDOW_BORDER_WIDTH;
	uint32_t v[] = { XCB_EVENT_MASK_ENTER_WINDOW|XCB_EVENT_MASK_FOCUS_CHANGE|XCB_EVENT_MASK_PROPERTY_CHANGE|XCB_EVENT_MASK_STRUCTURE_NOTIFY, FOCUSCOL };

	DBG("initializing new client from window: %d", win);
	c = (Client *)malloc(sizeof(Client));
	c->workspace = 1;
	c->win = win;
	c->tiled = 0;
	c->bw = border;
	c->mon = selmon;
	clientgeom(c);
	xcb_window_t trans = clienttrans(c);
	if (c->x + W(c) > c->mon->x + c->mon->w)
		c->x = c->mon->x + c->mon->w - W(c);
	if (c->y + H(c) > c->mon->y + c->mon->h)
		c->y = c->mon->y + c->mon->h - H(c);
	c->x = MAX(c->x, c->mon->x);
	c->y = MAX(c->y, c->mon->y);
	xcb_change_window_attributes(conn, c->win, XCB_CW_EVENT_MASK|XCB_CW_BORDER_PIXEL, v);
	config(c);
	sizehints(c);
	if (c->tiled)
		c->tiled = c->oldstate = trans == XCB_WINDOW_NONE || !c->fixed;
	attach(c);
	attach_stack(c);
	uint32_t wa[] = { c->x, c->y, c->w, c->h, c->bw, 0 };
	if (!c->tiled) {
		wa[5] = XCB_STACK_MODE_ABOVE;
		mask |= XCB_CONFIG_WINDOW_STACK_MODE;
	}
	xcb_configure_window(conn, c->win, mask, wa);
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, c->win, WMStateAtom, WMStateAtom, 32, 2, (unsigned char *)d);
	if (c->mon == selmon)
		unfocus(selmon->sel, 0);
	c->mon->sel = c;
	arrange(c->mon);
	xcb_map_window(conn, c->win);
	DBG("new client mapped: %d,%d @ %dx%d - tiled: %d", c->x, c->y, c->w, c->h, c->tiled);
	focusclient(NULL);
}

static Monitor *initmon(void)
{
	Monitor *m = (Monitor *)malloc(sizeof(Monitor));
	m->workspace = 1;
	DBG("initialized new monitor");
	return m;
}

static int initmons(void)
{
	int dirty = 0;

	DBG("updating monitor(s)");
	if (!mons)
		mons = initmon();
	if (mons->w != scr_w || mons->h != scr_h) {
		dirty = 1;
		mons->w = mons->winarea_w = scr_w;
		mons->h = mons->winarea_h = scr_h;
	}
	if (dirty) {
		selmon = mons;
		selmon = wintomon(root);
	}
	return dirty;
}

static void initscreen(void)
{
	scr = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
	root = scr->root;
	scr_w = scr->width_in_pixels;
	scr_h = scr->height_in_pixels;
	DBG("initialized root window: %i - size: %dx%d", root, scr_w, scr_h);
}

static void initwm(void)
{
	unsigned int i;
	xcb_generic_error_t *e;
	xcb_void_cookie_t cookie;
	xcb_cursor_context_t *ctx;
	uint32_t eventmask = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT|XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY|
		XCB_EVENT_MASK_BUTTON_PRESS|XCB_EVENT_MASK_POINTER_MOTION|XCB_EVENT_MASK_ENTER_WINDOW|
		XCB_EVENT_MASK_LEAVE_WINDOW|XCB_EVENT_MASK_STRUCTURE_NOTIFY|XCB_EVENT_MASK_PROPERTY_CHANGE;

	sigchld(0);
	initmons();
	if (xcb_cursor_context_new(conn, scr, &ctx) >= 0) {
		for (i = 0; i < LEN(cursors); i++) {
			cursor[i] = xcb_cursor_load_cursor(ctx, cursors[i]);
			DBG("initialized cursor: %s", cursors[i]);
		}
		xcb_cursor_context_free(ctx);
	}
	uint32_t v[] = { eventmask, cursor[Normal] };
	cookie = xcb_change_window_attributes_checked(conn, root, XCB_CW_EVENT_MASK|XCB_CW_CURSOR, v);
	e = xcb_request_check(conn, cookie);
	checkerr(e, __LINE__);
	WMStateAtom = initatom("WM_STATE");
	keysyms = xcb_key_symbols_alloc(conn);
	initbinds();
}

static void numlock(void)
{
	unsigned int i, j;
	xcb_generic_error_t *e = NULL;
	xcb_keycode_t code, *t, *codes;
	xcb_get_modifier_mapping_reply_t *mods;
	xcb_get_modifier_mapping_cookie_t cookie;

	cookie = xcb_get_modifier_mapping(conn);
	DBG("updating numlock modifier mask");
	mods = xcb_get_modifier_mapping_reply(conn, cookie, &e);
	checkerr(e, __LINE__);
	if ((t = xcb_key_symbols_get_keycode(keysyms, XK_Num_Lock))) {
		codes = xcb_get_modifier_mapping_keycodes(mods);
		code = *t;
		free(t);
		for (i = 0; i < 8; i++)
			for (j = 0; j < mods->keycodes_per_modifier; j++)
				if (codes[i * mods->keycodes_per_modifier + j] == code)
					numlockmask = (1 << i);
	}
	free(mods);
}

static Monitor *ptrtomon(int x, int y)
{
	Monitor *m;

	DBG("finding monitor at pointer location: %d,%d", x, y);
	for (m = mons; m; m = m->next)
		if (x >= m->winarea_x && x < m->winarea_x + m->winarea_w && y >= m->winarea_y && y < m->winarea_y + m->winarea_h)
			return m;
	return selmon;
}

static void quit(const Arg *arg)
{
	(void)(arg);
	DBG("user requested quit");
	running = 0;
}

static void reset(const Arg *arg)
{
	(void)(arg);
	char *const argv[] = { argv0, NULL };
	DBG("user requested restart, running command: %s", argv0);
	execvp(argv[0], argv);
}

static void resize(Client *c, int x, int y, int w, int h)
{
	uint32_t v[] = { x, y, w, h, c->bw };
	uint16_t mask = XCB_CONFIG_WINDOW_X|XCB_CONFIG_WINDOW_Y|XCB_CONFIG_WINDOW_WIDTH|XCB_CONFIG_WINDOW_HEIGHT|XCB_CONFIG_WINDOW_BORDER_WIDTH;

	DBG("resizing client: %i,%i - %ix%i", x, y, w, h);
	xcb_configure_window(conn, c->win, mask, v);
	setfield(&c->x, x, &c->old_x);
	setfield(&c->y, y, &c->old_y);
	setfield(&c->w, w, &c->old_w);
	setfield(&c->h, h, &c->old_h);
	config(c);
	xcb_flush(conn);
}

static void restack(Monitor *m)
{
	xcb_generic_event_t *e = NULL;
	uint32_t v[] = { XCB_STACK_MODE_ABOVE };

	DBG("restacking monitor");
	if (!m->sel)
		return;
	if (!m->sel->tiled) {
		DBG("focused client is floating, setting stack mode: STACK_MODE_ABOVE");
		xcb_configure_window(conn, m->sel->win, XCB_CONFIG_WINDOW_STACK_MODE, v);
	}
	xcb_flush(conn);
	while ((e = xcb_poll_for_queued_event(conn)) && e->response_type == XCB_ENTER_NOTIFY);
	free(e);
}

static void setfield(int *dst, int val, int *old)
{
	if (old)
		*old = *dst;
	*dst = val;
}

static void showhide(Client *c)
{
	uint16_t mask = XCB_CONFIG_WINDOW_X|XCB_CONFIG_WINDOW_Y;

	if (!c)
		return;
	if (VIS(c)) {
		DBG("showing clients top down");
		uint32_t v[] = { c->x, c->y };
		xcb_configure_window(conn, c->win, mask, v);
		if (!c->tiled)
			resize(c, c->x, c->y, c->w, c->h);
		showhide(c->snext);
	} else {
		DBG("hiding clients bottom up");
		showhide(c->snext);
		uint32_t v[] = { c->x + 2 * scr_w, c->y };
		xcb_configure_window(conn, c->win, mask, v);
	}
}

static void sigchld(int unused)
{
	(void)(unused);
	if (signal(SIGCHLD, sigchld) == SIG_ERR)
		errx(1, "can't install SIGCHLD handler");
	while(0 < waitpid(-1, NULL, WNOHANG));
}

static void spawn(const Arg *arg)
{
	DBG("user requested spawn using command: %s", ((char **)arg->v)[0]);
	if (fork() == 0) {
		if (conn)
			close(xcb_get_file_descriptor(conn));
		setsid();
		execvp(((char **)arg->v)[0], (char **)arg->v);
		fprintf(stderr, "dwm: execvp %s", ((char **)arg->v)[0]);
		perror(" failed");
		exit(0);
	}
}

static void sizehints(Client *c)
{
	xcb_size_hints_t s;

	DBG("setting client size hints");
	c->min_w = c->min_h = 0;	
	c->max_w = c->max_h = 0;
	c->base_w = c->base_h = 0;
	c->max_aspect = c->min_aspect = 0.0;	
	c->increment_w = c->increment_h = 0;

	if (!xcb_icccm_get_wm_normal_hints_reply(conn, xcb_icccm_get_wm_normal_hints_unchecked(conn, c->win), &s, NULL))
		s.flags = XCB_ICCCM_SIZE_HINT_P_SIZE;	

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
		DBG("set increment size: w = %d, h = %d", c->increment_w, c->increment_h);
	}
	if (s.flags & XCB_ICCCM_SIZE_HINT_BASE_SIZE) {
		c->base_w = s.base_width;
		c->base_h = s.base_height;
		DBG("set base size to base size: %dx%d", c->base_w, c->base_h);
	} else if (s.flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) {
		c->base_w = s.min_width;
		c->base_h = s.min_height;
		DBG("set base size to min size: %dx%d", c->base_w, c->base_h);
	}
	if (s.flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) {
		c->min_w = s.min_width;	
		c->min_h = s.min_height;	
		DBG("set min size to min size: %dx%d", c->min_w, c->min_h);
	} else if (s.flags & XCB_ICCCM_SIZE_HINT_BASE_SIZE) {
		c->min_w = s.base_width;
		c->min_h = s.base_height;
		DBG("set min size to base size: %dx%d", c->min_w, c->min_h);
	}
	if ((c->fixed = (c->max_w && c->max_h && c->max_w == c->min_w && c->max_h == c->min_h))) {
		DBG("client is fixed size");
	}
}

static void unfocus(Client *c, int setfocus)
{
	uint32_t g[] = { UNFOCUSCOL };
	if (!c)
		return;
	DBG("unfocusing client: %d", c->win);
	xcb_change_window_attributes(conn, c->win, XCB_CW_BORDER_PIXEL, g);
	if (setfocus) {
		DBG("focusing root window");
		xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
	}
}

static xcb_atom_t windowstate(xcb_window_t win)
{
	xcb_atom_t ret = -1;
	xcb_get_property_reply_t *r;
	xcb_get_property_cookie_t rc;
	xcb_generic_error_t *e = NULL;

	rc = xcb_get_property(conn, 0, win, WMStateAtom, XCB_ATOM_ATOM, 0, 0);
	DBG("getting WMState atom from window: %i", win);
	r = xcb_get_property_reply(conn, rc, &e);
	checkerr(e, __LINE__);
	if (xcb_get_property_value_length(r)) {
		DBG("reply was non-zero, returning it");
		ret = *(xcb_atom_t *)xcb_get_property_value(r);
	} else {
		DBG("reply was zero, returning -1");
	}
	free(r);
	return ret;
}

static Client *wintoclient(xcb_window_t win)
{
	Client *c;
	Monitor *m;
	
	DBG("finding matching client for window: %d", win);
	for (m = mons; m; m = m->next) {
		for (c = m->clients; c; c = c->next) {
			if (c->win == win) {
				DBG("found matching client for window: %d", win);
				return c;
			}
		}
	}
	DBG("unable to find a matching client for window: %d", win);
	return NULL;
}

static Monitor *wintomon(xcb_window_t win)
{
	int x, y;
	Client *c;
	xcb_query_pointer_reply_t *p;
	xcb_query_pointer_cookie_t pc;
	xcb_generic_error_t *e = NULL;

	if (win == root) {
		pc = xcb_query_pointer(conn, win);
		DBG("finding monitor from root window");
		p = xcb_query_pointer_reply(conn, pc, &e);
		checkerr(e, __LINE__);
		x = p->root_x;
		y = p->root_y;
		free(p);
		return ptrtomon(x, y);
	}
	if ((c = wintoclient(win))) {
		DBG("finding monitor from managed client");
		return c->mon;
	}
	return selmon;
}

int main(int argc, char *argv[])
{
	argv0 = argv[0];
	uint32_t v[] = { XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT };

	if (argc > 1) {
		fprintf(stderr, strcmp(argv[1], "-v") ? "usage: %s [-v]\n" : "%s v0.01\n", argv0);
		exit(1);
	}
	if (!setlocale(LC_CTYPE, ""))
		errx(1, "no locale support");
	if (xcb_connection_has_error((conn = xcb_connect(NULL, NULL))))
		errx(1, "error connecting to X");
	atexit(cleanup);
	initscreen();
	if (xcb_request_check(conn, xcb_change_window_attributes_checked(conn, root, XCB_CW_EVENT_MASK, v)))
		errx(1, "is another window manager already running?");
	initwm();
	existing();
	eventloop();

	return 0;
}
