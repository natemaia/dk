#include <err.h>
#include <stdio.h>
#include <regex.h>
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
#include <X11/keysym.h>

#include "yaxwm.h"
#include "config.h"

static void attach(Client *c)
{
	Client *n = c->mon->clients;

	for (; n && (n->floating || n->workspace != n->mon->workspace); n = n->next);
	if (n) {
		c->next = n->next;
		n->next = c;
	} else {
		c->next = c->mon->clients;
		c->mon->clients = c;
	}
}

static void attachstack(Client *c)
{
	c->snext = c->mon->stack;
	c->mon->stack = c;
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
	uint i;
	Monitor *m;
	xcb_get_property_cookie_t pc;
	xcb_icccm_get_wm_class_reply_t prop;

	pc = xcb_icccm_get_wm_class(con, c->win);
	DBG("setting client defaults and rule matching");
	c->floating = 0;
	setclientdesktop(c, (i = windowprop(c->win, netatoms[NetWMDesktop])) ? i : selmon->workspace);
	if (xcb_icccm_get_wm_class_reply(con, pc, &prop, NULL)) {
		DBG("got window class: %s", prop.class_name);
		for (i = 0; i < LEN(rules); i++)
			if (!regexec(&rules[i].regcomp, prop.class_name, 0, NULL, 0)) {
				DBG("found matching rule");
				c->floating = rules[i].floating;
				if (rules[i].workspace >= 0)
					setclientdesktop(c, rules[i].workspace);
				for (m = mons; m && m->num != rules[i].mon; m = m->next);
				if (m)
					c->mon = m;
				break;
			}
		xcb_icccm_get_wm_class_reply_wipe(&prop);
	}
}

static void detach(Client *c, int reattach)
{
	Client **tc;

	for (tc = &c->mon->clients; *tc && *tc != c; tc = &(*tc)->next);
	*tc = c->next;
	if (reattach) {
		c->next = c->mon->clients;
		c->mon->clients = c;
	}
}

static void detachstack(Client *c)
{
	Client **tc, *t;

	for (tc = &c->mon->stack; *tc && *tc != c; tc = &(*tc)->snext);
	*tc = c->snext;
	if (c == c->mon->sel) {
		for (t = c->mon->stack; t && t->workspace != t->mon->workspace; t = t->snext);
		c->mon->sel = t;
	}
}

static int eventerr(xcb_generic_event_t *ev)
{
	xcb_generic_error_t *e = (xcb_generic_error_t *)ev;

	if (e->error_code == 3 /* BadWindow */
			|| (e->error_code == 8  && (e->major_code == 42 || e->major_code == 12))  /* BadMatch & SetInputFocus/ConfigureWindow */
			|| (e->error_code == 10 && (e->major_code == 28 || e->major_code == 33))) /* BadAccess & GrabButton/GrabKey */
		return 0;
	warnx("event error: %d: \"%s\" - %d: \"%s\"",
			e->error_code, xcb_event_get_error_label(e->error_code),
			e->major_code, xcb_event_get_request_label(e->major_code));
	return 1;
}

static void eventloop(void)
{
	int x, y;
	Client *c;
	Monitor *m;
	uint i, mousebtn = 0;
	xcb_generic_event_t *ev;
	xcb_generic_error_t *err;
	static Monitor *mon = NULL;

	xcb_aux_sync(con);
	while (running && (ev = xcb_wait_for_event(con)) != NULL) {
		switch (EVTYPE(ev)) {
			case XCB_FOCUS_IN:
			{
				xcb_focus_in_event_t *e = (xcb_focus_in_event_t *)ev;

				if (selmon->sel && e->event != selmon->sel->win) {
					DBG("broken focusin event for window: %d - focusing selected window: %d", e->event, selmon->sel->win);
					focus(selmon->sel);
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
						for (m = mons; m; m = m->next)
							for (c = m->clients; c; c = c->next)
								if (c->fullscreen)
									resize(c, m->x, m->y, m->w, m->h);
						focusclient(NULL);
						layoutmon(NULL);
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
					else if (c->floating || !selmon->layout->func) {
						m = c->mon;
						if (e->value_mask & XCB_CONFIG_WINDOW_X)      setfield(&c->x, m->x + e->x, &c->old_x);
						if (e->value_mask & XCB_CONFIG_WINDOW_Y)      setfield(&c->y, m->y + e->y, &c->old_y);
						if (e->value_mask & XCB_CONFIG_WINDOW_WIDTH)  setfield(&c->w, e->width, &c->old_w);
						if (e->value_mask & XCB_CONFIG_WINDOW_HEIGHT) setfield(&c->h, e->height, &c->old_h);
						if ((c->x + c->w) > m->x + m->w)              setfield(&c->y, m->x + (m->w / 2 - c->w / 2), &c->old_y);
						if ((c->y + c->h) > m->y + m->h)              setfield(&c->y, m->y + (m->h / 2 - c->h / 2), &c->old_y);
						if ((e->value_mask & (xy)) && !(e->value_mask & (wh)))
							configure(c);
						if (c->workspace == c->mon->workspace)
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

				if ((e->event != root && (e->mode != XCB_NOTIFY_MODE_NORMAL || e->detail == XCB_NOTIFY_DETAIL_INFERIOR)))
					break;
				DBG("enter notify event - window: %d", e->event);
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

				if (!b->child || b->child == root || !(c = selmon->sel))
					break;
				if (b->detail == XCB_BUTTON_INDEX_1 || b->detail == XCB_BUTTON_INDEX_3) {
					DBG("button press event - button: %d", b->detail);
					restack(selmon);
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
				mousebtn = 0;
				if ((err = xcb_request_check(con, xcb_ungrab_pointer_checked(con, XCB_CURRENT_TIME)))) {
					free(err);
					errx(1, "failed to ungrab pointer");
				}
				break;
			}
			case XCB_MOTION_NOTIFY:
			{
				xcb_motion_notify_event_t *e = (xcb_motion_notify_event_t *)ev;

				DBG("motion notify event - window: %d - child: %d - cord: %d,%d", e->event, e->child, e->root_x, e->root_y);
				if (e->child == root) {
					if ((m = ptrtomon(e->root_x, e->root_y)) != mon && mon) {
						unfocus(selmon->sel, 1);
						selmon = m;
						focusclient(NULL);
					}
					mon = m;
					break;
				}
				if (!(c = selmon->sel) || !mousebtn || c->fullscreen)
					break;
				if (!c->floating && selmon->layout->func)
					togglefloat(NULL);
				if (pointerxy(&x, &y) && (!selmon->layout->func || c->floating)) {
					if (mousebtn == 1)
						resizehint(c, x - c->w / 2, y - c->h / 2, c->w, c->h, 1);
					else
						resizehint(c, c->x, c->y, x - c->x, y - c->y, 1);
				}
				break;
			}
			case XCB_KEY_RELEASE: /* fallthrough */
			case XCB_KEY_PRESS:
			{
				xcb_keysym_t sym;
				xcb_key_press_event_t *press = (xcb_key_press_event_t *)ev;
				xcb_key_release_event_t *release = (xcb_key_release_event_t *)ev;

				if (EVTYPE(ev) == XCB_KEY_PRESS)
					sym = xcb_key_press_lookup_keysym(keysyms, press, 0);
				else
					sym = xcb_key_release_lookup_keysym(keysyms, release, 0);
				for (i = 0; i < LEN(binds); i++)
					if (sym == binds[i].keysym && binds[i].type == EVTYPE(ev) && binds[i].func
							&& ((EVTYPE(ev) == XCB_KEY_PRESS && CLNMOD(binds[i].mod) == CLNMOD(press->state))
								|| (EVTYPE(ev) == XCB_KEY_RELEASE && CLNMOD(binds[i].mod) == CLNMOD(release->state)))) {
						DBG("key %s event for key: %u - modifier: %u", EVTYPE(ev) == XCB_KEY_PRESS ? "press" : "release",
								EVTYPE(ev) == XCB_KEY_PRESS ? press->detail : release->detail, CLNMOD(binds[i].mod));
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
				if ((wa = windowattr(e->window)) && !wa->override_redirect && !wintoclient(e->window))
					initclient(e->window);
				free(wa);
				break;
			}
			case XCB_UNMAP_NOTIFY:
			{
				xcb_unmap_notify_event_t *e = (xcb_unmap_notify_event_t *)ev;

				if ((c = wintoclient(e->window))) {
					if (EVISSEND(e)) {
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
					unfocus(selmon->sel, 1);
					view(&(const Arg){.ui = c->workspace});
					focusclient(c);
					restack(selmon);
				}
				break;
			}
			case XCB_PROPERTY_NOTIFY:
			{
				xcb_window_t trans;
				xcb_property_notify_event_t *e = (xcb_property_notify_event_t *)ev;

				if (e->state != XCB_PROPERTY_DELETE && (c = wintoclient(e->window))) {
					if (e->atom == XCB_ATOM_WM_TRANSIENT_FOR) {
						if (!c->floating && (trans = windowtrans(c->win, c)) != XCB_NONE && (c->floating = (wintoclient(trans) != NULL)))
							layoutmon(c->mon);
					} else if (e->atom == XCB_ATOM_WM_NORMAL_HINTS) {
						sizehints(c);
					} else if (e->atom == XCB_ATOM_WM_HINTS) {
						wmhints(c);
					}
					if (e->atom == netatoms[NetWMWindowType])
						windowtype(c);
				}
				break;
			}
			default:
			{
				DBG("unhandled event: %s", xcb_event_get_label(ev->response_type));
				if (!ev->response_type)
					eventerr(ev);
			}
		}
		free(ev);
	}
}

static void focus(Client *c)
{
	if (!c->nofocus) {
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, c->win, XCB_CURRENT_TIME);
		xcb_change_property(con, XCB_PROP_MODE_REPLACE, c->win, netatoms[NetActiveWindow], XCB_ATOM_WINDOW, 32, 1, (uchar*)&(c->win));
	}
	sendevent(c, wmatoms[WMTakeFocus]);
}

static void focusclient(Client *c)
{
	if (!c || c->workspace != c->mon->workspace)
		for (c = selmon->stack; c && c->workspace != c->mon->workspace; c = c->snext);
	if (selmon->sel && selmon->sel != c)
		unfocus(selmon->sel, 0);
	if (c) {
		DBG("focusing client: %d", c->win);
		if (c->mon != selmon)
			selmon = c->mon;
		detachstack(c);
		attachstack(c);
		xcb_change_window_attributes(con, c->win, XCB_CW_BORDER_PIXEL, (uint32_t []){ focuscol });
		focus(c);
	} else {
		DBG("focusing root window");
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
		xcb_delete_property(con, root, netatoms[NetActiveWindow]);
	}
	selmon->sel = c;
}

static void follow(const Arg *arg)
{
	if (selmon->sel && arg->ui != selmon->sel->workspace) {
		send(arg);
		view(arg);
	}
}

static void freeclient(Client *c, int destroyed)
{
	Monitor *m = c->mon;

	DBG("freeing client: %d - destroyed: %i", c->win, destroyed);
	detach(c, 0);
	detachstack(c);
	if (!destroyed) {
		xcb_grab_server(con);
		xcb_configure_window(con, c->win, XCB_CONFIG_WINDOW_BORDER_WIDTH, (uint32_t []){ c->old_bw });
		setclientstate(c, XCB_ICCCM_WM_STATE_WITHDRAWN);
		xcb_aux_sync(con);
		xcb_ungrab_server(con);
	}
	free(c);
	focusclient(NULL);
	updateclientlist();
	layoutmon(m);
}

static void freemonitor(Monitor *m)
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
	Monitor *m;

	for (m = mons; m; m = m->next)
		while (m->stack)
			freeclient(m->stack, 0);
	xcb_ungrab_button(con, XCB_BUTTON_INDEX_ANY, root, XCB_MOD_MASK_ANY);
	xcb_ungrab_key(con, XCB_GRAB_ANY, root, XCB_MOD_MASK_ANY);
	xcb_key_symbols_free(keysyms);
	while (mons)
		freemonitor(mons);
	for (i = 0; i < LEN(cursors); i++)
		xcb_free_cursor(con, cursor[i]);
	xcb_destroy_window(con, wmcheck);
	xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, XCB_INPUT_FOCUS_POINTER_ROOT, XCB_CURRENT_TIME);
	xcb_aux_sync(con);
	xcb_delete_property(con, root, netatoms[NetActiveWindow]);
	xcb_disconnect(con);
}

static void geometry(Client *c)
{
	xcb_get_geometry_reply_t *g;

	if ((g = xcb_get_geometry_reply(con, xcb_get_geometry(con, c->win), NULL))) {
		DBG("using geometry given by the window");
		setfield(&c->w, g->width, &c->old_w);
		setfield(&c->h, g->height, &c->old_h);
		setfield(&c->x, g->x + c->mon->x > 0 ? g->x + c->mon->x : c->mon->x + (c->mon->w - c->w / 2), &c->old_x);
		setfield(&c->y, g->y + c->mon->y > 0 ? g->y + c->mon->y : c->mon->y + (c->mon->h - c->h / 2), &c->old_y);
		setfield(&c->bw, g->border_width, &c->old_bw);
		free(g);
	} else {
		DBG("failed to get window geometry, centering half monitor width/height");
		setfield(&c->w, c->mon->w / 2, &c->old_w);
		setfield(&c->h, c->mon->h / 2, &c->old_h);
		setfield(&c->x, c->mon->x + (c->mon->w - c->w / 2), &c->old_x);
		setfield(&c->y, c->mon->y + (c->mon->h - c->h / 2), &c->old_y);
		setfield(&c->bw, border, &c->old_bw);
	}
	c->bw = border;
}

static int grabpointer(xcb_cursor_t cursor)
{
	xcb_grab_pointer_cookie_t pc;
	xcb_grab_pointer_reply_t *ptr;

	pc = xcb_grab_pointer(con, 0, root, MOTIONMASK, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, root, cursor, XCB_CURRENT_TIME);
	if ((ptr = xcb_grab_pointer_reply(con, pc, NULL))) {
		if (ptr->status != XCB_GRAB_STATUS_SUCCESS) {
			warnx("unable to grab the mouse pointer");
			return 0;
		}
		free(ptr);
	}
	return 1;
}

static int hasproto(Client *c, xcb_atom_t proto)
{
	int n, exists = 0;
	xcb_get_property_cookie_t rpc;
	xcb_icccm_get_wm_protocols_reply_t rproto;

	rpc = xcb_icccm_get_wm_protocols(con, c->win, wmatoms[WMProtocols]);
	if (xcb_icccm_get_wm_protocols_reply(con, rpc, &rproto, NULL)) {
		n = rproto.atoms_len;
		while (!exists && n--)
			exists = rproto.atoms[n] == proto;
		xcb_icccm_get_wm_protocols_reply_wipe(&rproto);
	}
	return exists;
}

static void initatoms(xcb_atom_t *atoms, const char **names, int num)
{
	int i;
	xcb_intern_atom_reply_t *r;
    xcb_intern_atom_cookie_t c[num];

    for (i = 0; i < num; ++i)
        c[i] = xcb_intern_atom(con, 0, strlen(names[i]), names[i]);
    for (i = 0; i < num; ++i) {
		if ((r = xcb_intern_atom_reply(con, c[i], NULL))) {
			DBG("initializing atom: %s - value: %d", names[i], r->atom)
			atoms[i] = r->atom;
			free(r);
		} else {
			warnx("unable to initialize atom: %s", names[i]);
		}
	}
}

static void initbinds(int onlykeys)
{
	xcb_keycode_t *c, *t, *cd;
	xcb_get_modifier_mapping_reply_t *m;
	uint i, j, mods[] = { 0, XCB_MOD_MASK_LOCK, numlockmask, numlockmask|XCB_MOD_MASK_LOCK };
	static const uint btns[] = { XCB_BUTTON_INDEX_1, XCB_BUTTON_INDEX_2, XCB_BUTTON_INDEX_3 };

	DBG("updating numlock modifier mask");
	if ((m = xcb_get_modifier_mapping_reply(con, xcb_get_modifier_mapping(con), NULL))) {
		if ((t = xcb_key_symbols_get_keycode(keysyms, XK_Num_Lock)) && (cd = xcb_get_modifier_mapping_keycodes(m))) {
			for (i = 0; i < 8; i++)
				for (j = 0; j < m->keycodes_per_modifier; j++)
					if (cd[i * m->keycodes_per_modifier + j] == *t)
						numlockmask = (1 << i);
			free(t);
		}
		free(m);
	}

	DBG("window: %d - ungrabbing all%s keys with any modifier", root, onlykeys ? "" : " buttons and");
	if (!onlykeys)
		xcb_ungrab_button(con, XCB_BUTTON_INDEX_ANY, root, XCB_MOD_MASK_ANY);
	xcb_ungrab_key(con, XCB_GRAB_ANY, root, XCB_MOD_MASK_ANY);
	for (i = 0; i < LEN(mods); i++) {
		if (!onlykeys)
			for (j = 0; j < LEN(btns); j++) {
				DBG("window: %d - grabbing button: %u modifier: %u", root, btns[j], MODKEY|mods[i]);
				xcb_grab_button(con, 0, root, BUTTONMASK, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_SYNC, 0, XCB_NONE, btns[j], MODKEY|mods[i]);
			}
		for (j = 0; j < LEN(binds); j++) {
			if ((c = xcb_key_symbols_get_keycode(keysyms, binds[j].keysym))) {
				DBG("window: %d - grabbing key: %u modifier: %u", root, *c, binds[j].mod|mods[i]);
				xcb_grab_key(con, 1, root, binds[j].mod|mods[i], *c, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
				free(c);
			}
		}
	}
}

static void initclient(xcb_window_t win)
{
	Client *c;

	DBG("initializing new client from window: %d", win);
	if (!(c = (Client *)calloc(1, sizeof(Client))))
		errx(1, "unable to allocate space for new client");
	c->win = win;
	c->mon = selmon;
	if (windowtrans(c->win, c) == XCB_WINDOW_NONE)
		clientrules(c);
	geometry(c);
	if (c->x <= c->mon->x || c->x + W(c) >= c->mon->x + c->mon->w)
		c->x = (c->mon->x + c->mon->w - W(c)) / 2;
	if (c->y <= c->mon->y || c->y + H(c) >= c->mon->y + c->mon->h)
		c->y = (c->mon->y + c->mon->h - H(c)) / 2;
	xcb_configure_window(con, c->win, XCB_CONFIG_WINDOW_BORDER_WIDTH, (uint32_t []){ c->bw });
	configure(c);
	windowtype(c);
	sizehints(c);
	wmhints(c);
	xcb_change_window_attributes(con, c->win, XCB_CW_EVENT_MASK|XCB_CW_BORDER_PIXEL, (uint32_t []){ focuscol,
			XCB_EVENT_MASK_ENTER_WINDOW|XCB_EVENT_MASK_FOCUS_CHANGE|XCB_EVENT_MASK_PROPERTY_CHANGE|XCB_EVENT_MASK_STRUCTURE_NOTIFY });
	if (c->floating || (c->floating = c->oldstate = c->fixed))
		xcb_configure_window(con, c->win, XCB_CONFIG_WINDOW_STACK_MODE, (uint32_t []){ XCB_STACK_MODE_ABOVE });
	attach(c);
	attachstack(c);
	xcb_change_property(con, XCB_PROP_MODE_APPEND, root, netatoms[NetClientList], XCB_ATOM_WINDOW, 32, 1, (uchar *)&(c->win));
	setclientstate(c, XCB_ICCCM_WM_STATE_NORMAL);
	if (c->mon == selmon && selmon->sel)
		unfocus(selmon->sel, 0);
	c->mon->sel = c;
	layoutmon(c->mon);
	xcb_map_window(con, c->win);
	DBG("new client mapped: %d,%d @ %dx%d - floating: %d", c->x, c->y, c->w, c->h, c->floating);
	focusclient(NULL);
}

static void initexisting(void)
{
	uint i, num;
	xcb_window_t trans;
	xcb_window_t *wins = NULL;
	xcb_query_tree_reply_t *tree = NULL;
	xcb_get_window_attributes_reply_t *wa;

	if (!(tree = xcb_query_tree_reply(con, xcb_query_tree(con, root), NULL)))
		errx(1, "unable to get tree reply from the root window");
	num = tree->children_len;
	wins = xcb_query_tree_children(tree);
	for (i = 0; i < num; i++) {
		wa = windowattr(wins[i]);
		if (!(wa->override_redirect || (trans = windowtrans(wins[i], NULL)) != XCB_NONE) &&
				(wa->map_state == XCB_MAP_STATE_VIEWABLE || windowprop(wins[i], wmatoms[WMState]) == XCB_ICCCM_WM_STATE_ICONIC))
			initclient(wins[i]);
		free(wa);
	}
	for (i = 0; i < num; i++) { /* now the transients */
		wa = windowattr(wins[i]);
		if ((trans = windowtrans(wins[i], NULL)) != XCB_NONE &&
				(wa->map_state == XCB_MAP_STATE_VIEWABLE || windowprop(wins[i], wmatoms[WMState]) == XCB_ICCCM_WM_STATE_ICONIC))
			initclient(wins[i]);
		free(wa);
	}
	free(tree);
}

static Monitor *initmon(int num)
{
	Monitor *m;

	DBG("initializing new monitor: %d", num);
	if (!(m = calloc(1, sizeof(Monitor))))
		errx(1, "unable to allocate space for new monitor: %d", num);
	m->num = num;
	m->workspace = 0;
	m->nmaster = nmaster;
	m->layout = &layouts[0];
	m->splitratio = splitratio;
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
	int r, len = 0;
	char errbuf[256];
	xcb_atom_t utf8str;
	xcb_void_cookie_t c;
	xcb_generic_error_t *e;
	xcb_cursor_context_t *ctx;
	uint i, j, nworkspace = LEN(workspaces);
	uint32_t mask = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT|XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY|
		XCB_EVENT_MASK_BUTTON_PRESS|XCB_EVENT_MASK_POINTER_MOTION|XCB_EVENT_MASK_ENTER_WINDOW|
		XCB_EVENT_MASK_LEAVE_WINDOW|XCB_EVENT_MASK_STRUCTURE_NOTIFY|XCB_EVENT_MASK_PROPERTY_CHANGE;

	DBG("initializing %s", argv0);
	sigchld(0);
	initmons();
	if (xcb_cursor_context_new(con, scr, &ctx) < 0)
		errx(1, "unable to create cursor context");
	for (i = 0; i < LEN(cursors); i++) {
		cursor[i] = xcb_cursor_load_cursor(ctx, cursors[i]);
		DBG("initialized cursor: %s", cursors[i]);
	}
	xcb_cursor_context_free(ctx);

	for (i = 0; i < LEN(rules); i++) {
		DBG("compiling rules[%d] regex: %s", i, rules[i].regex);
		if ((r = regcomp(&rules[i].regcomp, rules[i].regex, REG_NOSUB|REG_EXTENDED|REG_ICASE))) {
			regerror(r, &rules[i].regcomp, errbuf, sizeof(errbuf));
			errx(1, "invalid regex rules[%d]: %s: %s\n", i, rules[i].regex, errbuf);
		}
	}

	DBG("initializing atoms");
	initatoms(&utf8str, &(const char *){"UTF8_STRING"}, 1);
	initatoms(wmatoms, wmatomnames, LEN(wmatomnames));
	initatoms(netatoms, netatomnames, LEN(netatomnames));

	DBG("creating wm check window");
	wmcheck = xcb_generate_id(con);
	xcb_create_window(con, XCB_COPY_FROM_PARENT, wmcheck, root, -1, -1, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_ONLY, scr->root_visual, 0, NULL);

	DBG("setting wm check window atoms: _NET_SUPPORTING_WM_CHECK, _NET_WM_NAME")
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, wmcheck, netatoms[NetWMCheck], XCB_ATOM_WINDOW, 32, 1, (uchar *)&wmcheck);
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, wmcheck, netatoms[NetWMName],  utf8str, 8, 5, (uchar *)"yaxwm");

	DBG("setting root window atoms: _NET_SUPPORTING_WM_CHECK, _NET_NUMBER_OF_DESKTOPS, _NET_DESKTOP_VIEWPORT,\n"
		"                           _NET_DESKTOP_GEOMETRY, _NET_CURRENT_DESKTOP, _NET_DESKTOP_NAMES, _NET_SUPPORTED")
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, root, netatoms[NetWMCheck], XCB_ATOM_WINDOW, 32, 1, &wmcheck);
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, root, netatoms[NetNumDesktops], XCB_ATOM_CARDINAL, 32, 1, &nworkspace);
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, root, netatoms[NetDesktopViewport], XCB_ATOM_CARDINAL, 32, 2, (uint32_t []){0, 0});
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, root, netatoms[NetDesktopGeometry], XCB_ATOM_CARDINAL, 32, 2, (uint32_t []){scr_w, scr_h});
	if ((i = windowprop(root, netatoms[NetCurrentDesktop])))
		xcb_change_property(con, XCB_PROP_MODE_REPLACE, root, netatoms[NetCurrentDesktop], XCB_ATOM_CARDINAL, 32, 1, (uchar *)&i);
	else
		xcb_change_property(con, XCB_PROP_MODE_REPLACE, root, netatoms[NetCurrentDesktop], XCB_ATOM_CARDINAL, 32, 1, (uchar *)&selmon->workspace);

	for (i = 0; i < LEN(workspaces); i++)
		len += strlen(workspaces[i]) + 1;
	char wsnames[len];
	for (i = 0, len = 0; i < LEN(workspaces); i++)
		for (j = 0; (wsnames[len++] = workspaces[i][j++]););
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, root, netatoms[NetDesktopNames], utf8str, 8, --len, wsnames);
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, root, netatoms[NetSupported], XCB_ATOM_ATOM, 32, NetLast, (uchar *)netatoms);
	xcb_delete_property(con, root, netatoms[NetClientList]);

	DBG("setting root window event mask and cursor");
	c = xcb_change_window_attributes_checked(con, root, XCB_CW_EVENT_MASK|XCB_CW_CURSOR, (uint32_t []){ mask, cursor[Normal] });
	if ((e = xcb_request_check(con, c))) {
		free(e);
		errx(1, "unable to change root window event mask and cursor");
	}
	if (!(keysyms = xcb_key_symbols_alloc(con)))
		errx(1, "error unable to get keysyms from X connection");
	initbinds(0);
	focusclient(NULL);
}

static void killclient(const Arg *arg)
{
	if (!selmon->sel)
		return;
	DBG("user requested kill current client");
	(void)(arg);
	if (!sendevent(selmon->sel, wmatoms[WMDelete])) {
		xcb_grab_server(con);
		xcb_set_close_down_mode(con, XCB_CLOSE_DOWN_DESTROY_ALL);
		xcb_kill_client(con, selmon->sel->win);
		xcb_aux_sync(con);
		xcb_ungrab_server(con);
	}
}

static void layoutmon(Monitor *m)
{
	if (m)
		showhide(m->stack);
	else for (m = mons; m; m = m->next)
		showhide(m->stack);
	if (m) {
		if (m->layout->func)
			m->layout->func(m);
		restack(m);
	} else for (m = mons; m; m = m->next)
		if (m->layout->func)
			m->layout->func(m);
}

static Client *nexttiled(Client *c)
{
	for (; c && (c->floating || c->workspace != c->mon->workspace); c = c->next);
	return c;
}

static int pointerxy(int *x, int *y)
{
	xcb_query_pointer_reply_t *p;

	if (!(p = xcb_query_pointer_reply(con, xcb_query_pointer(con, root), NULL)))
		return 0;
	*x = p->root_x;
	*y = p->root_y;
	free(p);
	return 1;
}

static Monitor *ptrtomon(int x, int y)
{
	Monitor *m;

	DBG("finding monitor at pointer location: %d,%d", x, y);
	for (m = mons; m; m = m->next)
		if (x >= m->winarea_x && x < m->winarea_x + m->winarea_w && y >= m->winarea_y && y < m->winarea_y + m->winarea_h) {
			DBG("returning monitor: %d", m->num);
			return m;
		}
	DBG("unable to find monitor, returning selected monitor: %d", selmon->num);
	return selmon;
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
	if ((nexttiled(c->mon->clients) == c && !nexttiled(c->next)) && !(c->floating || c->fullscreen)) {
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

static void restack(Monitor *m)
{
	Client *c;
	
	if (!m->sel)
		return;
	DBG("restacking clients on monitor monitor: %d", m->num);
	if (m->sel->floating || !m->layout->func) {
		DBG("setting focused floating client stack mode: STACK_MODE_ABOVE");
		xcb_configure_window(con, m->sel->win, XCB_CONFIG_WINDOW_STACK_MODE, (uint32_t []){ XCB_STACK_MODE_ABOVE });
	}
	if (m->layout->func) {
		DBG("layout exists, setting tiled clients stack mode: STACK_MODE_BELOW");
		for (c = m->stack; c; c = c->snext)
			if (!c->floating && c->workspace == c->mon->workspace)
				xcb_configure_window(con, c->win, XCB_CONFIG_WINDOW_STACK_MODE, (uint32_t []){ XCB_STACK_MODE_BELOW });
	}
	xcb_aux_sync(con);
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
	if (selmon->sel && arg->ui != selmon->sel->workspace) {
		setclientdesktop(selmon->sel, arg->ui);
		focusclient(NULL);
		layoutmon(selmon);
	}
}

static int sendevent(Client *c, xcb_atom_t proto)
{
	int exists = 0;
	xcb_generic_error_t *e;
	xcb_void_cookie_t cookie;
	xcb_client_message_event_t cme;

	if ((exists = hasproto(c, proto))) {
		cme.response_type = XCB_CLIENT_MESSAGE;
		cme.window = c->win;
		cme.type = wmatoms[WMProtocols];
		cme.format = 32;
		cme.data.data32[0] = proto;
		cme.data.data32[1] = XCB_TIME_CURRENT_TIME;
		cookie = xcb_send_event_checked(con, 0, c->win, XCB_EVENT_MASK_NO_EVENT, (const char *)&cme);
		if ((e = xcb_request_check(con, cookie))) {
			free(e);
			errx(1, "failed sending event to window: %d", c->win);
		}
	}
	return exists;
}

static void setclientdesktop(Client *c, int num)
{
	c->workspace = num;
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, c->win, netatoms[NetWMDesktop], XCB_ATOM_CARDINAL, 32, 1, &num);
}

static void setclientstate(Client *c, long state)
{
	long data[] = { state, XCB_ATOM_NONE };

	xcb_change_property(con, XCB_PROP_MODE_REPLACE, c->win, wmatoms[WMState], wmatoms[WMState], 32, 2, (uchar *)data);
}

static void setfield(int *dst, int val, int *old)
{
	if (old)
		*old = *dst;
	*dst = val;
}

static void setfocus(const Arg *arg)
{
	Client *c = NULL, *i;

	if (!selmon->sel || selmon->sel->fullscreen)
		return;
	DBG("finding %s client from window: %d", arg->i > 0 ? "next" : "previous", selmon->sel->win);
	if (arg->i > 0) {
		for (c = selmon->sel->next; c && c->workspace != c->mon->workspace; c = c->next);
		if (!c) /* end of list reached */
			for (c = selmon->clients; c && c->workspace != c->mon->workspace; c = c->next);
	} else {
		for (i = selmon->clients; i != selmon->sel; i = i->next)
			if (i->workspace == i->mon->workspace)
				c = i;
		if (!c) /* end of list reached */
			for (; i; i = i->next)
				if (i->workspace == i->mon->workspace)
					c = i;
	}
	if (c) {
		DBG("found client window: %d", c->win);
		focusclient(c);
		restack(c->mon);
	} else {
		DBG("unable to find next client");
	}
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
		resize(c, c->mon->x, c->mon->y, c->mon->w, c->mon->h);
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
		layoutmon(c->mon);
	}
}

static void setlayout(const Arg *arg)
{
	DBG("setting current monitor layout");
	if (arg && arg->v)	
		selmon->layout = (Layout *)arg->v;
	if (selmon->sel)
		layoutmon(selmon);
}

static void setnmaster(const Arg *arg)
{
	selmon->nmaster = MAX(selmon->nmaster + arg->i, 0);
	layoutmon(selmon);
}

static int setsizehints(Client *c, int *x, int *y, int *w, int *h, int interact)
{
	int baseismin;
	Monitor *m = c->mon;

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
	if (c->floating || !m->layout->func) {
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

	if (!arg || !selmon->layout->func)
		return;
	f = arg->f < 1.0 ? arg->f + selmon->splitratio : arg->f - 1.0;
	if (f < 0.1 || f > 0.9)
		return;
	DBG("setting split ratio: %f -> %f", selmon->splitratio, f);
	selmon->splitratio = f;
	layoutmon(selmon);
}

static void showhide(Client *c)
{
	if (!c)
		return;
	if (c->workspace == c->mon->workspace) {
		DBG("showing clients breadthfirst");
		xcb_configure_window(con, c->win, XCB_CONFIG_WINDOW_X|XCB_CONFIG_WINDOW_Y, (uint32_t []){ c->x, c->y });
		if ((!c->mon->layout->func || c->floating) && !c->fullscreen)
			resizehint(c, c->x, c->y, c->w, c->h, 0);
		showhide(c->snext);
	} else {
		DBG("hiding clients depthfirst");
		showhide(c->snext);
		xcb_configure_window(con, c->win, XCB_CONFIG_WINDOW_X|XCB_CONFIG_WINDOW_Y, (uint32_t []){ W(c) * -2, c->y });
	}
}

static void sigchld(int unused)
{
	(void)(unused);
	if (signal(SIGCHLD, sigchld) == SIG_ERR)
		errx(1, "can't install SIGCHLD handler");
	while(0 < waitpid(-1, NULL, WNOHANG));
}

static void sizehints(Client *c)
{
	xcb_size_hints_t s;
	xcb_get_property_cookie_t pc;

	pc = xcb_icccm_get_wm_normal_hints(con, c->win);
	DBG("setting client size hints");
	c->min_w = c->min_h = 0;
	c->max_w = c->max_h = 0;
	c->base_w = c->base_h = 0;
	c->max_aspect = c->min_aspect = 0.0;
	c->increment_w = c->increment_h = 0;

	if (!xcb_icccm_get_wm_normal_hints_reply(con, pc, &s, NULL))
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

static void swapclient(const Arg *arg)
{
	Client *c = selmon->sel;

	(void)(arg);
	if (!selmon->layout->func || (c && c->floating) || (c == nexttiled(selmon->clients) && (!c || !(c = nexttiled(c->next)))))
		return;
	DBG("swapping current client window: %d", c->win);
	detach(c, 1);
	focusclient(c);
	layoutmon(c->mon);
}

static void tile(Monitor *m)
{
	Client *c;
	uint i, n, h, mw, my, ty;

	for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++);
	if (n == 0)
		return;
	DBG("tiling monitor: %d", m->num);
	if (n > m->nmaster)
		mw = m->nmaster ? m->winarea_w * m->splitratio : 0;
	else
		mw = m->winarea_w;
	for (i = my = ty = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++)
		if (i < m->nmaster) {
			h = (m->winarea_h - my) / (MIN(n, m->nmaster) - i);
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

	if (!(c = selmon->sel))
		return;
	DBG("toggling selected window floating state: %d -> %d", c->floating, !c->floating);
	(void)(arg);
	if ((c->floating = !c->floating || c->fixed))
		resizehint(c, c->x, c->y, c->w, c->h, 0);
	layoutmon(selmon);
}

static void unfocus(Client *c, int focusroot)
{
	if (!c)
		return;
	DBG("unfocusing client: %d", c->win);
	xcb_change_window_attributes(con, c->win, XCB_CW_BORDER_PIXEL, (uint32_t []){ unfocuscol });
	if (focusroot) {
		DBG("focusing root window");
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
		xcb_delete_property(con, root, netatoms[NetActiveWindow]);
	}
}

static void updateclientlist(void)
{
	Client *c;
	Monitor *m;

	xcb_delete_property(con, root, netatoms[NetClientList]);
	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			xcb_change_property(con, XCB_PROP_MODE_APPEND, root, netatoms[NetClientList], XCB_ATOM_WINDOW, 32, 1, (uchar *)&(c->win));
}

static void view(const Arg *arg)
{
	if (arg->ui != selmon->workspace) {
		DBG("viewing workspace: %d", arg->ui);
		selmon->workspace = arg->ui;
		xcb_change_property(con, XCB_PROP_MODE_REPLACE, root, netatoms[NetCurrentDesktop], XCB_ATOM_CARDINAL, 32, 1, (uchar *)&arg->ui);
		layoutmon(selmon);
		focusclient(NULL);
		if (selmon->sel) /* shit hack to fix focus on workspace change */
			xcb_warp_pointer(con, XCB_NONE, selmon->sel->win, 0, 0, 0, 0, selmon->sel->w - 10, selmon->sel->h - 10);
	}
}

static xcb_get_window_attributes_reply_t *windowattr(xcb_window_t win)
{
	xcb_get_window_attributes_reply_t *wa;

	DBG("getting window attributes from window: %d", win);
	if (!(wa = xcb_get_window_attributes_reply(con, xcb_get_window_attributes(con, win), NULL)))
		errx(1, "unable to get window attributes from window");
	return wa;
}

static xcb_atom_t windowprop(xcb_window_t win, xcb_atom_t prop)
{
	xcb_get_property_reply_t *r;
	xcb_atom_t ret = XCB_ATOM_NONE;

	DBG("getting window property atom from window: %d", win);
	if (!(r = xcb_get_property_reply(con, xcb_get_property(con, 0, win, prop, XCB_ATOM_ANY, 0, sizeof(prop)), NULL))) {
		warnx("unable to get window property reply");
	} else if (xcb_get_property_value_length(r)) {
		ret = *(xcb_atom_t *)xcb_get_property_value(r);
		free(r);
	}
	return ret;
}

static xcb_window_t windowtrans(xcb_window_t win, Client *c)
{
	Client *t;
	xcb_window_t trans;
	xcb_get_property_cookie_t pc;

	pc = xcb_icccm_get_wm_transient_for(con, win);
	trans = XCB_WINDOW_NONE;
	DBG("getting transient hints for window: %d", win);
	if (xcb_icccm_get_wm_transient_for_reply(con, pc, &trans, NULL) && c && (t = wintoclient(trans))) {
		DBG("window is transient, setting workspace and monitor to match");
		c->mon = t->mon;
		c->floating = 1;
		setclientdesktop(c, t->workspace);
	}
	return trans;
}

static void windowtype(Client *c)
{
	DBG("checking window type for window: %d", c->win)
	if (windowprop(c->win, netatoms[NetWMState]) == netatoms[NetWMFullscreen])
		setfullscreen(c, 1);
	if (windowprop(c->win, netatoms[NetWMWindowType]) == netatoms[NetWMWindowTypeDialog])
		c->floating = 1;
}

static Client *wintoclient(xcb_window_t win)
{
	Client *c;
	Monitor *m;

	if (win == root)
		return NULL;
	DBG("finding client from window: %d", win);
	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			if (c->win == win) {
				DBG("returning matching client");
				return c;
			}
	DBG("unable to find existing client");
	return NULL;
}

static Monitor *wintomon(xcb_window_t win)
{
	int x, y;
	Client *c;

	DBG("finding monitor from window: %d", win);
	if (win == root) {
		DBG("root window, returning monitor at pointer location");
		if (pointerxy(&x, &y))
			return ptrtomon(x, y);
	} else if ((c = wintoclient(win))) {
		DBG("returning matching monitor");
		return c->mon;
	}
	DBG("unable to find monitor");
	return selmon;
}

static void wmhints(Client *c)
{
	xcb_icccm_wm_hints_t wmh;
	
	DBG("setting window manager hints for window: %d", c->win);
	if (xcb_icccm_get_wm_hints_reply(con, xcb_icccm_get_wm_hints(con, c->win), &wmh, NULL)) {
		DBG("got hints reply")
		if (c == selmon->sel && wmh.flags & XCB_ICCCM_WM_HINT_X_URGENCY) {
			wmh.flags &= ~XCB_ICCCM_WM_HINT_X_URGENCY;
			xcb_icccm_set_wm_hints(con, c->win, &wmh);
		} else
			c->urgent = (wmh.flags & XCB_ICCCM_WM_HINT_X_URGENCY) ? 1 : 0;
		if (wmh.flags & XCB_ICCCM_WM_HINT_INPUT)
			c->nofocus = !wmh.input;
		else
			c->nofocus = 0;
	}
}

int main(int argc, char *argv[])
{
	xcb_void_cookie_t c;

	argv0 = argv[0];
	if (argc > 1) {
		fprintf(stderr, strcmp(argv[1], "-v") ? "usage: %s [-v]\n" : "%s v0.01\n", argv0);
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
