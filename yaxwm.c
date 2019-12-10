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

#define EVENTMASK     (0x7f)
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
#define EVENTTYPE(e)  (e->response_type &  EVENTMASK)
#define EVENTSENT(e)  (e->response_type & ~EVENTMASK)
#define BUTTONMASK    XCB_EVENT_MASK_BUTTON_PRESS|XCB_EVENT_MASK_BUTTON_RELEASE
#define MOTIONMASK    XCB_EVENT_MASK_BUTTON_RELEASE|XCB_EVENT_MASK_BUTTON_MOTION|XCB_EVENT_MASK_POINTER_MOTION_HINT

typedef union Arg Arg;
typedef struct Key Key;
typedef struct Client Client;
typedef struct Monitor Monitor;

enum Cursors {
	Normal, Move, Resize, CurLast
};

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
static xcb_connection_t *con;
static xcb_key_symbols_t *keysyms;

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
static void initbinds(int bindmouse);
static void initclient(xcb_window_t win);
static void initscreen(void);
static Monitor *initmon(void);
static int initmons(void);
static void initwm(void);
static void killc(const Arg *arg);
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
	{ MODKEY,                    XK_q,      killc,    {0} },
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
	else
		for (m = mons; m; m = m->next)
			restack(m);
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

	for (m = mons; m; m = m->next)
		while (m->stack)
			freeclient(m->stack, 0);
	xcb_ungrab_key(con, XCB_GRAB_ANY, root, XCB_MOD_MASK_ANY);
	xcb_key_symbols_free(keysyms);
	while (mons)
		freemon(mons);
	for (i = 0; i < LEN(cursors); i++)
		xcb_free_cursor(con, cursor[i]);
	xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, XCB_INPUT_FOCUS_POINTER_ROOT, XCB_CURRENT_TIME);
	xcb_aux_sync(con);
	xcb_disconnect(con);
}

static void clientgeom(Client *c)
{
	xcb_get_geometry_reply_t *g;

	if ((g = xcb_get_geometry_reply(con, xcb_get_geometry_unchecked(con, c->win), NULL))) {
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

	pc = xcb_icccm_get_wm_transient_for_unchecked(con, c->win);
	if (xcb_icccm_get_wm_transient_for_reply(con, pc, &trans, NULL) && (t = wintoclient(trans))) {
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
	xcb_send_event(con, 0, c->win, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (char *)ce);
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
	Monitor *m;
	unsigned int i;
	Client *c, *ptrc = NULL;
	xcb_generic_event_t *ev;

	xcb_aux_sync(con);
	while (running && (ev = xcb_wait_for_event(con))) {
		switch (EVENTTYPE(ev)) {
		case XCB_FOCUS_IN:
			{
				xcb_focus_in_event_t *e = (xcb_focus_in_event_t *)ev;

				if (selmon->sel && e->event != selmon->sel->win) {
					DBG("focus in event for unfocused window: %d", selmon->sel->win);
					focusclient(selmon->sel);
				}
				break;
			}
		case XCB_MAPPING_NOTIFY:
			{
				xcb_mapping_notify_event_t *e = (xcb_mapping_notify_event_t *)ev;

				if (!(keysyms = xcb_key_symbols_alloc(con)))
					errx(1, "error unable to get keysyms from X connection");
				if (e->request == XCB_MAPPING_KEYBOARD)
					initbinds(0);
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
						if (VIS(c))
							resize(c, c->x, c->y, c->w, c->h);
					} else {
						DBG("configure request client is tiled");
						config(c);
					}
				} else {
					DBG("configure request event for unmanaged client");
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

				DBG("enter notify event");
				if (e->event != root && (e->mode != XCB_NOTIFY_MODE_NORMAL || e->detail == XCB_NOTIFY_DETAIL_INFERIOR)) {
					break;
				} else if ((m = (c = wintoclient(e->event)) ? c->mon : wintomon(e->event)) != selmon) {
					unfocus(selmon->sel, 1);
					selmon = m;
				} else if (!c || c == selmon->sel) {
					break;
				}
				focusclient(c);
				break;
			}
		case XCB_BUTTON_PRESS:
			{
				xcb_button_press_event_t *b = (xcb_button_press_event_t *)ev;
				
				if (!b->child || b->child == root || !(ptrc = wintoclient(b->child)))
					break;
				if (b->detail == XCB_BUTTON_INDEX_1 || b->detail == XCB_BUTTON_INDEX_3) {
					DBG("button press event for button: %d", ptrc->win);
					focusclient(ptrc);
					restack(ptrc->mon);
					if ((mousebtn = b->detail) == XCB_BUTTON_INDEX_1)
						xcb_warp_pointer(con, XCB_NONE, ptrc->win, 0, 0, 0, 0, ptrc->w/2, ptrc->h/2);
					else {
						mousebtn = 3;
						xcb_warp_pointer(con, XCB_NONE, ptrc->win, 0, 0, 0, 0, ptrc->w, ptrc->h);
					}
					xcb_grab_pointer(con, 0, root, MOTIONMASK, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
							root, cursor[b->detail == 1 ? Move : Resize], XCB_CURRENT_TIME);
				}
				break;
			}
		case XCB_BUTTON_RELEASE:
			{
				if (ptrc) {
					DBG("button release event");
					xcb_ungrab_pointer(con, XCB_CURRENT_TIME);
					ptrc = NULL;
					mousebtn = 0;
				}
				break;
			}
		case XCB_MOTION_NOTIFY:
			{
				xcb_query_pointer_reply_t *p;
				xcb_query_pointer_cookie_t pc;

				if (ptrc && (mousebtn == 1 || mousebtn == 3)) {
					pc = xcb_query_pointer_unchecked(con, root);
					DBG("motion notify event - button: %d", mousebtn);
					if (!(p = xcb_query_pointer_reply(con, pc, NULL))) {
						DBG("unable to get pointer location");
						break;
					}
					if (mousebtn == 1)
						resize(ptrc, p->root_x - ptrc->w / 2, p->root_y - ptrc->h / 2, ptrc->w, ptrc->h);
					else
						resize(ptrc, ptrc->x, ptrc->y, p->root_x - ptrc->x, p->root_y - ptrc->y);
					free(p);
				}
				break;
			}
		case XCB_KEY_PRESS:
			{
				xcb_keysym_t sym;
				xcb_key_press_event_t *e = (xcb_key_press_event_t*)ev;

				sym = xcb_key_press_lookup_keysym(keysyms, e, 0);
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
				xcb_get_window_attributes_cookie_t wacookie;
				xcb_map_request_event_t *e = (xcb_map_request_event_t *)ev;

				wacookie = xcb_get_window_attributes_unchecked(con, e->window);
				DBG("map request event for window: %i", e->window);
				if (!(wa = xcb_get_window_attributes_reply(con, wacookie, NULL))) {
					DBG("unable to get window attributes or window has override_redirect set, not managing");
					break;
				}
				if (!wa->override_redirect && !wintoclient(e->window))
					initclient(e->window);
				free(wa);
				break;
			}
		case XCB_UNMAP_NOTIFY:
			{
				xcb_unmap_notify_event_t *e = (xcb_unmap_notify_event_t *)ev;

				if ((c = wintoclient(e->window))) {
					DBG("unmap notify event for managed window: %i", e->window);
					freeclient(c, 0);
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
		xcb_flush(con);
		free(ev);
	}
}

static void existing(void)
{
	unsigned int i, num;
	xcb_generic_error_t *err;
	xcb_window_t *wins = NULL;

	xcb_query_tree_reply_t *query_reply = xcb_query_tree_reply(con, xcb_query_tree(con, root), &err);
	num = query_reply->children_len;
	wins = xcb_query_tree_children(query_reply);

	for (i = 0; i < num; i++) {
		xcb_get_window_attributes_reply_t *ga_reply = xcb_get_window_attributes_reply(con, xcb_get_window_attributes(con, wins[i]), &err);
		checkerr(err, __LINE__);
		if (ga_reply->override_redirect)
			continue;
		xcb_window_t trans_reply = XCB_NONE;
		xcb_icccm_get_wm_transient_for_reply(con, xcb_icccm_get_wm_transient_for(con, wins[i]), &trans_reply, &err);
		checkerr(err, __LINE__);
		if (trans_reply != XCB_NONE)
			continue;
		if (ga_reply->map_state == XCB_MAP_STATE_VIEWABLE || windowstate(wins[i]) == XCB_ICCCM_WM_STATE_ICONIC)
			initclient(wins[i]);
		free(ga_reply);
	}
	for (i = 0; i < num; i++) { /* now the transients */
		xcb_get_window_attributes_reply_t *ga_reply = xcb_get_window_attributes_reply(con, xcb_get_window_attributes(con, wins[i]), &err);
		checkerr(err, __LINE__);
		xcb_window_t trans_reply = XCB_NONE;
		xcb_icccm_get_wm_transient_for_reply(con, xcb_icccm_get_wm_transient_for(con, wins[i]), &trans_reply, &err);
		checkerr(err, __LINE__);
		if (trans_reply != XCB_NONE && (ga_reply->map_state == XCB_MAP_STATE_VIEWABLE || windowstate(wins[i]) == XCB_ICCCM_WM_STATE_ICONIC))
			initclient(wins[i]);
		free(ga_reply);
	}
	if (query_reply)
		free(query_reply);
}

static void focusclient(Client *c)
{
	uint32_t v[] = { FOCUSCOL };

	if (!c || !VIS(c))
		for (c = selmon->stack; c && !VIS(c); c = c->snext);
	if (selmon->sel)
		unfocus(selmon->sel, 0);
	if (c) {
		DBG("focusing client: %d", c->win);
		if (c->mon != selmon)
			selmon = c->mon;
		xcb_change_window_attributes(con, c->win, XCB_CW_BORDER_PIXEL, v);
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, c->win, XCB_CURRENT_TIME);
	} else {
		DBG("focusing root window");
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
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
		xcb_grab_server(con);
		xcb_configure_window(con, c->win, XCB_CONFIG_WINDOW_BORDER_WIDTH, v);
		xcb_ungrab_button(con, XCB_BUTTON_INDEX_ANY, c->win, XCB_GRAB_ANY);
		xcb_change_property(con, XCB_PROP_MODE_REPLACE, c->win, WMStateAtom, WMStateAtom, 32, 2, (unsigned char *)d);
		xcb_aux_sync(con);
		xcb_ungrab_server(con);
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
	if ((r = xcb_intern_atom_reply(con, xcb_intern_atom_unchecked(con, 0, strlen(name), name), NULL))) {
		atom = r->atom;
		free(r);
	}
	return atom;
}

static void initbinds(int bindmouse)
{
	xcb_keycode_t *c;
	unsigned int i, j;
	uint8_t async = XCB_GRAB_MODE_ASYNC;

	numlock();
	{ /* new scope to use updated numlockmask */
		unsigned int btns[] = { XCB_BUTTON_INDEX_1, XCB_BUTTON_INDEX_2, XCB_BUTTON_INDEX_3 };
		unsigned int mods[] = { 0, XCB_MOD_MASK_LOCK, numlockmask, numlockmask|XCB_MOD_MASK_LOCK };

		if (bindmouse) {
			xcb_ungrab_button(con, XCB_BUTTON_INDEX_ANY, root, XCB_MOD_MASK_ANY);
			DBG("window: %d - ungrabbing all buttons and keys with any modifier", root);
		} else {
			DBG("window: %d - ungrabbing all keys with any modifier", root);
		}
		xcb_ungrab_key(con, XCB_GRAB_ANY, root, XCB_MOD_MASK_ANY);
		for (i = 0; i < LEN(mods); i++) {
			if (bindmouse) {
				for (j = 0; j < LEN(btns); j++) {
					DBG("window: %d - grabbing button: %u modifier: %u", root, btns[j], MODKEY|mods[i]);
					xcb_grab_button(con, 0, root, BUTTONMASK, async, async, 0, XCB_NONE, btns[j], MODKEY|mods[i]);
				}
			}
			for (j = 0; j < LEN(keys); j++) {
				if ((c = xcb_key_symbols_get_keycode(keysyms, keys[j].keysym))) {
					DBG("window: %d - grabbing key: %u modifier: %u", root, *c, keys[j].mod|mods[i]);
					xcb_grab_key(con, 1, root, keys[j].mod|mods[i], *c, async, async);
					free(c);
				}
			}
		}
	}
}

static void initclient(xcb_window_t win)
{
	Client *c;
	uint32_t borderwidth[] = { border };
	uint32_t stackmode[] = { XCB_STACK_MODE_ABOVE };
	long data[] = { XCB_ICCCM_WM_STATE_NORMAL, XCB_ATOM_NONE };
	uint32_t windowchanges[] = { FOCUSCOL,
		XCB_EVENT_MASK_ENTER_WINDOW|XCB_EVENT_MASK_FOCUS_CHANGE|
			XCB_EVENT_MASK_PROPERTY_CHANGE|XCB_EVENT_MASK_STRUCTURE_NOTIFY
	};

	DBG("initializing new client from window: %d", win);
	c = (Client *)malloc(sizeof(Client));
	c->win = win;
	c->tiled = 0;
	c->mon = selmon;
	c->workspace = 1;
	clientgeom(c);  /* set initial geom */
	c->bw = border;
	xcb_window_t trans = clienttrans(c); /* is this a transient of another window */
	if (c->x <= c->mon->x || c->x + W(c) >= c->mon->x + c->mon->w)
		c->x = (c->mon->x + c->mon->w - W(c)) / 2;
	if (c->y <= c->mon->y || c->y + H(c) >= c->mon->y + c->mon->h)
		c->y = (c->mon->y + c->mon->h - H(c)) / 2;
	xcb_configure_window(con, c->win, XCB_CONFIG_WINDOW_BORDER_WIDTH, borderwidth);
	config(c);
	sizehints(c);
	xcb_change_window_attributes(con, c->win, XCB_CW_EVENT_MASK|XCB_CW_BORDER_PIXEL, windowchanges);
	if (!c->tiled || !(c->tiled = c->oldstate = trans == XCB_WINDOW_NONE || !c->fixed))
		xcb_configure_window(con, c->win, XCB_CONFIG_WINDOW_STACK_MODE, stackmode);
	attach(c);
	attach_stack(c);
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, c->win, WMStateAtom, WMStateAtom, 32, 2, (unsigned char *)data);
	resize(c, c->x, c->y, c->w, c->h);
	if (c->mon == selmon)
		unfocus(selmon->sel, 0);
	c->mon->sel = c;
	arrange(c->mon);
	xcb_map_window(con, c->win);
	DBG("new client mapped: %d,%d @ %dx%d - tiled: %d", c->x, c->y, c->w, c->h, c->tiled);
	focusclient(NULL);
}

static Monitor *initmon(void)
{
	Monitor *m = (Monitor *)malloc(sizeof(Monitor));
	m->next = NULL;
	m->workspace = 1;
	m->sel = m->stack = m->clients = NULL;
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
	if (!(scr = xcb_setup_roots_iterator(xcb_get_setup(con)).data))
		errx(1, "error getting default screen from X connection");
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
	uint32_t mask = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT|XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY|
		XCB_EVENT_MASK_BUTTON_PRESS|XCB_EVENT_MASK_POINTER_MOTION|XCB_EVENT_MASK_ENTER_WINDOW|
		XCB_EVENT_MASK_LEAVE_WINDOW|XCB_EVENT_MASK_STRUCTURE_NOTIFY|XCB_EVENT_MASK_PROPERTY_CHANGE;

	DBG("initializing yaxwm")
	sigchld(0);
	initmons();
	if (xcb_cursor_context_new(con, scr, &ctx) >= 0) {
		for (i = 0; i < LEN(cursors); i++) {
			cursor[i] = xcb_cursor_load_cursor(ctx, cursors[i]);
			DBG("initialized cursor: %s", cursors[i]);
		}
		xcb_cursor_context_free(ctx);
	}
	DBG("setting root window event mask and cursor");
	uint32_t v[] = { mask, cursor[Normal] };
	cookie = xcb_change_window_attributes_checked(con, root, XCB_CW_EVENT_MASK|XCB_CW_CURSOR, v);
	e = xcb_request_check(con, cookie);
	checkerr(e, __LINE__);
	WMStateAtom = initatom("WM_STATE");
	if (!(keysyms = xcb_key_symbols_alloc(con)))
		errx(1, "error unable to get keysyms from X connection");
	initbinds(1);
	focusclient(NULL);
}

static void killc(const Arg *arg)
{
	if (!selmon->sel)
		return;
	xcb_grab_server(con);
	xcb_set_close_down_mode(con, XCB_CLOSE_DOWN_DESTROY_ALL);
	xcb_kill_client(con, selmon->sel->win);
	xcb_ungrab_server(con);
	xcb_flush(con);
}

static void numlock(void)
{
	unsigned int i, j;
	xcb_keycode_t code, *t, *codes;
	xcb_get_modifier_mapping_reply_t *mods;
	xcb_get_modifier_mapping_cookie_t cookie;

	cookie = xcb_get_modifier_mapping_unchecked(con);
	DBG("updating numlock modifier mask");
	if ((mods = xcb_get_modifier_mapping_reply(con, cookie, NULL))) {
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
	uint16_t mask = XCB_CONFIG_WINDOW_X|XCB_CONFIG_WINDOW_Y|XCB_CONFIG_WINDOW_WIDTH|
		XCB_CONFIG_WINDOW_HEIGHT|XCB_CONFIG_WINDOW_BORDER_WIDTH;

	DBG("resizing client: %i,%i - %ix%i", x, y, w, h);
	xcb_configure_window(con, c->win, mask, v);
	setfield(&c->x, x, &c->old_x);
	setfield(&c->y, y, &c->old_y);
	setfield(&c->w, w, &c->old_w);
	setfield(&c->h, h, &c->old_h);
	config(c);
	xcb_aux_sync(con);
}

static void restack(Monitor *m)
{
	xcb_generic_event_t *ev;
	uint32_t v[] = { XCB_STACK_MODE_ABOVE };

	DBG("restacking monitor");
	if (!m->sel)
		return;
	if (!m->sel->tiled) {
		DBG("focused client is floating, setting stack mode: STACK_MODE_ABOVE");
		xcb_configure_window(con, m->sel->win, XCB_CONFIG_WINDOW_STACK_MODE, v);
	}
	xcb_aux_sync(con);
	while ((ev = xcb_poll_for_event(con)) && EVENTTYPE(ev) == XCB_ENTER_NOTIFY);
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
		xcb_configure_window(con, c->win, mask, v);
		if (!c->tiled)
			resize(c, c->x, c->y, c->w, c->h);
		showhide(c->snext);
	} else {
		DBG("hiding clients bottom up");
		showhide(c->snext);
		uint32_t v[] = { W(c) * -2, c->y };
		xcb_configure_window(con, c->win, mask, v);
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
		if (con)
			close(xcb_get_file_descriptor(con));
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

	if (!xcb_icccm_get_wm_normal_hints_reply(con, xcb_icccm_get_wm_normal_hints_unchecked(con, c->win), &s, NULL))
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
	c->fixed = (c->max_w && c->max_h && c->max_w == c->min_w && c->max_h == c->min_h);
	DBG("client is %s size", c->fixed ? "fixed" : "variable");
}

static void unfocus(Client *c, int setfocus)
{
	uint32_t g[] = { UNFOCUSCOL };
	if (!c)
		return;
	DBG("unfocusing client: %d", c->win);
	xcb_change_window_attributes(con, c->win, XCB_CW_BORDER_PIXEL, g);
	if (setfocus) {
		DBG("focusing root window");
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
	}
}

static xcb_atom_t windowstate(xcb_window_t win)
{
	xcb_atom_t ret = -1;
	xcb_get_property_reply_t *r;
	xcb_get_property_cookie_t rc;
	xcb_generic_error_t *e = NULL;

	rc = xcb_get_property(con, 0, win, WMStateAtom, XCB_ATOM_ATOM, 0, 0);
	DBG("getting WMState atom from window: %i", win);
	r = xcb_get_property_reply(con, rc, &e);
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

	DBG("finding monitor from window: %d", win);
	if (win == root) {
		DBG("window: %d is root window, using pointer location", win);
		if (!(p = xcb_query_pointer_reply(con, xcb_query_pointer(con, win), NULL))) {
			DBG("unable to get pointer location, returning selected monitor");
			return selmon;
		}
		x = p->root_x;
		y = p->root_y;
		free(p);
		return ptrtomon(x, y);
	}
	if ((c = wintoclient(win))) {
		DBG("found matching monitor from managed client");
		return c->mon;
	}
	DBG("unable to find monitor from window: %d - returning selected monitor", win);
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
	if (xcb_connection_has_error((con = xcb_connect(NULL, NULL))))
		errx(1, "error connecting to X");
	atexit(cleanup);
	initscreen();
	if (xcb_request_check(con, xcb_change_window_attributes_checked(con, root, XCB_CW_EVENT_MASK, v)))
		errx(1, "is another window manager already running?");
	xcb_aux_sync(con);
	initwm();
	existing();
	eventloop();

	return 0;
}
