/* yet another X window manager
*
* vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
*/

#include "yaxwm.h"

int main(int argc, char *argv[])
{
	argv0 = argv[0];
	xcb_void_cookie_t c;
	struct sigaction sa;
	int sigs[] = { SIGTERM, SIGINT, SIGHUP, SIGCHLD };
	uint mask = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT;

	if (argc > 1) {
		fprintf(stderr, !strcmp(argv[1], "-v") ? "%s v0.01\n" : "usage: %s [-v]\n", argv0);
		exit(1);
	}
	if (!setlocale(LC_CTYPE, ""))
		errx(1, "no locale support");
	if (xcb_connection_has_error((con = xcb_connect(NULL, NULL))))
		errx(1, "error connecting to X");

	/* cleanly quit when exit(3) is called */
	atexit(freewm);

	/* setup root screen */
	if (!(scr = xcb_setup_roots_iterator(xcb_get_setup(con)).data))
		errx(1, "error getting default screen from X connection");
	root = scr->root;
	scr_w = scr->width_in_pixels;
	scr_h = scr->height_in_pixels;
	DBG("initialized root window: 0x%x - size: %dx%d", root, scr_w, scr_h)

	/* check that we can grab SubstructureRedirect events on the root window */
	c = xcb_change_window_attributes_checked(con, root, XCB_CW_EVENT_MASK, &mask);
	if (xcb_request_check(con, c))
		errx(1, "is another window manager already running?");

	/* setup signal handlers (atexit(3) doesn't handle process exiting via signals) */
	sa.sa_handler = sighandle;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	for (uint i = 0; i < LEN(sigs); i++)
		if (sigaction(sigs[i], &sa, NULL) < 0)
			errx(1, "unable to setup handler for signal: %d", sigs[i]);

	/* setup the wm and existing windows before entering the event loop */
	initwm();
	initexisting();
	eventloop();

	return 0;
}

void attach(Client *c, int tohead)
{ /* attach client to it's workspaces client list */
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
{ /* attach client to it's workspaces focus stack list */
	c->snext = c->ws->stack;
	c->ws->stack = c;
}

void assignworkspaces(void)
{ /* map workspaces to monitors, create more if needed */
	Monitor *m;
	Workspace *ws;
	uint i, j, n = 0;

	FOR_EACH(m, monitors)
		n++;

	updatenumws(n);
	j = numws / MAX(1, n);
	ws = workspaces;
	DBG("%d workspaces - %d per monitor", numws, j)

	FOR_EACH(m, monitors)
		for (i = 0; ws && i < j; i++, ws = ws->next) {
			ws->mon = m;
			DBG("workspace: %d - monitor: %s", ws->num, m->name)
			if (!i)
				m->ws = ws;
		}

	if (j * n != numws) {
		DBG("leftovers after dividing between monitors, assigning one per monitor until exhausted")
		for (m = monitors; ws; m = monitors)
			while (ws && m) {
				DBG("workspace: %d - monitor: %s", ws->num, m->name)
				ws->mon = m;
				ws = ws->next;
				m = m->next;
			}
	}
}

void changefocus(const Arg *arg)
{ /* focus the next or previous client on the active workspace */
	Client *c;

	if (!selws->sel || selws->sel->fullscreen)
		return;
	if (arg->i > 0)
		c = selws->sel->next ? selws->sel->next : selws->clients;
	else
		FOR_PREV(c, selws->sel, selws->clients);
	if (c) {
		DBG("focusing %s client", arg->i > 0 ? "next" : "previous")
		focus(c);
		restack(c->ws);
	}
}

void changews(Workspace *ws, int usermotion)
{ /* change the currently active workspace and warp the mouse if needed */
	int diffmon = selws->mon != ws->mon;

	DBG("viewing workspace: %d", ws->num)
	selws = ws;
	selws->mon->ws = ws;
	PROP_REPLACE(root, netatoms[CurrentDesktop], XCB_ATOM_CARDINAL, 32, 1, &ws->num);
	if (diffmon && !usermotion) {
		DBG("workspace is assigned to a different monitor "
				"and user wasn't moving the pointer, warping it to: %d,%d",
				ws->mon->x + (ws->mon->w / 2), ws->mon->y + (ws->mon->h / 2))
		xcb_warp_pointer(con, root, root, 0, 0, 0, 0,
				ws->mon->x + (ws->mon->w / 2), ws->mon->y + (ws->mon->h / 2));
	}
}

void checkerror(char *prompt, xcb_generic_error_t *e)
{ /* if e is non-null print a warning with error code and name to stderr and free(3) e */
	if (!e)
		return;
	warnx("%s -- X11 error: %d: %s", prompt, e->error_code,
			xcb_event_get_error_label(e->error_code));
	free(e);
}

void configure(Client *c)
{ /* send client a configure notify event */
	xcb_configure_notify_event_t ce;

	DBG("sending configure notify event to client window: 0x%x", c->win)
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

void clientrules(Client *c, xcb_window_t trans)
{ /* apply user specified rules to client, try using _NET atoms otherwise */
	uint i;
	Client *t;
	Monitor *m;
	int ws, n, len, num = -1;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t pc;
	xcb_icccm_get_wm_class_reply_t prop;

	if ((trans != XCB_WINDOW_NONE || (trans = windowtrans(c->win)) != XCB_WINDOW_NONE)
			&& (t = wintoclient(trans)))
	{
		DBG("window is transient of managed client, setting workspace and monitor to match")
		c->ws = t->ws;
		c->floating = 1;
		ws = c->ws->num;
		goto done;
	}

	DBG("setting client defaults and rule matching for window: 0x%x", c->win)
	pc = xcb_icccm_get_wm_class(con, c->win);
	c->floating = 0;
	if ((ws = windowprop(c->win, netatoms[Desktop])) < 0)
		ws = selws->num;
	if (xcb_icccm_get_wm_class_reply(con, pc, &prop, &e)) {
		DBG("window class: %s - instance: %s", prop.class_name, prop.instance_name)
			for (i = 0; i < LEN(rules); i++)
				if (ruleregcmp(&rules[i].regcomp, prop.class_name, prop.instance_name)) {
					DBG("client matched rule regex: %s", rules[i].regex)
					c->floating = rules[i].floating;
					if (rules[i].workspace >= 0)
						ws = rules[i].workspace;
					else if (rules[i].monitor) {
						len = strlen(rules[i].monitor);
						if (len <= 2 && isdigit(rules[i].monitor[0])
								&& (len == 1 || isdigit(rules[i].monitor[1])))
							num = atoi(rules[i].monitor);
						for (n = 0, m = monitors; m; m = m->next, n++)
							if ((num >= 0 && num == n) || !strcmp(rules[i].monitor, m->name)) {
								ws = m->ws->num;
								break;
							}
					}
					break;
				}
		xcb_icccm_get_wm_class_reply_wipe(&prop);
	} else {
		checkerror("failed to get window class", e);
	}

done:
	setclientws(c, ws);
	DBG("set client values - workspace: %d, monitor: %s, floating: %d",
			c->ws->num, c->ws->mon->name, c->floating)
}

void detach(Client *c, int reattach)
{ /* detach client from it's workspaces client list, can reattach to save calling attach() */
	Client **tc = &c->ws->clients;

	while (*tc && *tc != c)
		tc = &(*tc)->next;
	*tc = c->next;
	if (reattach)
		attach(c, 1);
}

void detachstack(Client *c)
{ /* detach client from it's workspaces focus stack list */
	Client **tc = &c->ws->stack;

	while (*tc && *tc != c)
		tc = &(*tc)->snext;
	*tc = c->snext;
	if (c == c->ws->sel)
		c->ws->sel = c->ws->stack;
}

void *ecalloc(size_t elems, size_t size)
{ /* calloc(3) elems elements of size size, exit with message on error */
	void *p;

	if (!(p = calloc(elems, size)))
		err(1, "unable to allocate space");
	return p;
}

void eventhandle(xcb_generic_event_t *ev)
{
	uint i;
	int x, y;
	Panel *p;
	Client *c;
	Monitor *m;
	Workspace *ws;
	xcb_generic_error_t *err;

	if (ev->response_type == randrbase + XCB_RANDR_SCREEN_CHANGE_NOTIFY) {
		DBG("RANDR screen change notify, updating monitors")
		if (updaterandr() > 0)
			fixupworkspaces();
		return;
	}

	switch (XCB_EVENT_RESPONSE_TYPE(ev)) {
		case XCB_FOCUS_IN:
		{
			xcb_focus_in_event_t *e = (xcb_focus_in_event_t *)ev;

			if (selws->sel && e->event != selws->sel->win)
				setfocus(selws->sel);
			return;
		}
		case XCB_CONFIGURE_NOTIFY:
		{
			xcb_configure_notify_event_t *e = (xcb_configure_notify_event_t *)ev;

			if (e->window == root && (scr_h != e->height || scr_w != e->width)) {
				DBG("root window configure notify event, changed geometry")
				scr_w = e->width;
				scr_h = e->height;
				if (randrbase < 0) {
					monitors->w = monitors->winarea_w = scr_w;
					monitors->h = monitors->winarea_h = scr_h;
					fixupworkspaces();
				}
			}
			return;
		}
		case XCB_MAPPING_NOTIFY:
		{
			xcb_mapping_notify_event_t *e = (xcb_mapping_notify_event_t *)ev;

			xcb_refresh_keyboard_mapping(keysyms, e);
			if (e->request == XCB_MAPPING_KEYBOARD)
				grabkeys();
			return;
		}
		case XCB_CONFIGURE_REQUEST:
		{
			xcb_configure_request_event_t *e = (xcb_configure_request_event_t *)ev;

			if ((c = wintoclient(e->window))) {
				DBG("configure request event for managed window: 0x%x", e->window)
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
					if ((e->value_mask & XYMASK) && !(e->value_mask & WHMASK))
						configure(c);
					if (c->ws == c->ws->mon->ws)
						resize(c, c->x, c->y, c->w, c->h);
				} else {
					configure(c);
				}
			} else {
				DBG("configure request event for unmanaged window: 0x%x", e->window)
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
			return;
		}
		case XCB_DESTROY_NOTIFY:
		{
			xcb_destroy_notify_event_t *e = (xcb_destroy_notify_event_t *)ev;

			if ((c = wintoclient(e->window))) {
				DBG("destroy notify event for managed client window: 0x%x -- freeing", e->window)
				freeclient(c, 1);
			} else if ((p = wintopanel(e->window))) {
				DBG("destroy notify event for managed panel window: 0x%x -- freeing", e->window)
				freepanel(p, 1);
			}
			return;
		}
		case XCB_ENTER_NOTIFY:
		{
			xcb_enter_notify_event_t *e = (xcb_enter_notify_event_t *)ev;

			if (e->event != root && (e->mode != XCB_NOTIFY_MODE_NORMAL
						|| e->detail == XCB_NOTIFY_DETAIL_INFERIOR))
				return;
			DBG("enter notify event - window: 0x%x", e->event)
			c = wintoclient(e->event);
			if ((ws = c ? c->ws : wintows(e->event)) != selws) {
				unfocus(selws->sel, 1);
				selws = ws;
				selws->mon->ws = ws;
			} else if (!focusmouse || !c || c == selws->sel)
				return;
			focus(c);
			return;
		}
		case XCB_BUTTON_PRESS:
		{
			xcb_button_press_event_t *b = (xcb_button_press_event_t *)ev;

			if (!b->event || !(c = selws->sel))
				return;
			b->state &= 0x00ff; /* drop the top 8 bits (status bits) */
			if ((mousebtn = b->detail) == BUTTON1 && CLNMOD(b->state) != MODKEY) {
				DBG("button press event had no modifiers, focusing clicked window")
				focus((c = wintoclient(b->event)));
				restack(c->ws);
			} else if (mousebtn == BUTTON1 || mousebtn == BUTTON3) {
				DBG("button press event - button: %d", b->detail)
				restack(selws);
				if (mousebtn == BUTTON1)
					xcb_warp_pointer(con, XCB_NONE, c->win, 0, 0, 0, 0, c->w / 2, c->h / 2);
				else
					xcb_warp_pointer(con, XCB_NONE, c->win, 0, 0, 0, 0, c->w, c->h);
				if (!grabpointer(cursor[b->detail == BUTTON1 ? Move : Resize]))
					mousebtn = 0;
			} else if (mousebtn == BUTTON2) {
				togglefloat(NULL);
			}
			return;
		}
		case XCB_BUTTON_RELEASE:
		{
			xcb_void_cookie_t cookie;
			xcb_button_release_event_t *b = (xcb_button_release_event_t *)ev;

			if (!b->event || !(c = selws->sel))
				return;
			DBG("button release event, ungrabbing pointer")
			cookie = xcb_ungrab_pointer_checked(con, XCB_CURRENT_TIME);
			if ((err = xcb_request_check(con, cookie))) {
				free(err);
				errx(1, "failed to ungrab pointer");
			}
			if (mousebtn == BUTTON3)
				ignorefocusevents();
			mousebtn = 0;
			focus(c);
			return;
		}
		case XCB_MOTION_NOTIFY:
		{
			xcb_motion_notify_event_t *e = (xcb_motion_notify_event_t *)ev;

			if (!mousebtn || !(c = selws->sel) || c->fullscreen) {
				if (!e->child && pointerxy(&x, &y) && (m = ptrtomon(x, y)) != selws->mon) {
					unfocus(selws->sel, 1);
					changews(m->ws, 1);
					focus(NULL);
				}
				return;
			}
			if (!c->floating && selws->layout)
				togglefloat(NULL);
			if (pointerxy(&x, &y) && (!selws->layout || c->floating)) {
				DBG("motion notify event window: 0x%x - coord: %d,%d", e->child, e->root_x, e->root_y)
				if ((m = ptrtomon(x, y)) != selws->mon) {
					setclientws(c, m->ws->num);
					changews(m->ws, 1);
					focus(c);
				}
				if (mousebtn == BUTTON1)
					resizehint(c, x - c->w / 2, y - c->h / 2, c->w, c->h, 1);
				else
					resizehint(c, c->x, c->y, x - c->x, y - c->y, 1);
			}
			return;
		}
		case XCB_KEY_PRESS: /* fallthrough */
		case XCB_KEY_RELEASE:
		{
			xcb_keysym_t sym;
			xcb_key_press_event_t *e = (xcb_key_press_event_t *)ev;
			
			e->state &= 0x00ff; /* drop the top 8 bits (status bits) */
			sym = xcb_key_symbols_get_keysym(keysyms, e->detail, 0);
			for (i = 0; i < LEN(binds); i++)
				if (sym == binds[i].keysym && binds[i].func
						&& CLNMOD(binds[i].mod) == CLNMOD(e->state)
						&& ev->response_type == binds[i].type)
				{
					DBGBIND(ev, CLNMOD(e->state), sym)
					binds[i].func(&(binds[i].arg));
					return;
				}
			return;
		}
		case XCB_MAP_REQUEST:
		{
			xcb_get_window_attributes_reply_t *wa;
			xcb_map_request_event_t *e = (xcb_map_request_event_t *)ev;

			if (!e->window || !(wa = windowattr(e->window)))
				return;
			if ((c = wintoclient(e->window)) || (p = wintopanel(e->window))) {
				DBG("map request for managed window: 0x%x - ignoring", c ? c->win : p->win)
				free(wa);
				return;
			}
			DBG("map request event for unmanaged window: 0x%x", e->window)
			if (windowprop(e->window, netatoms[WindowType]) == netatoms[WindowTypeDock])
				initpanel(e->window);
			else if (!wa->override_redirect)
				initclient(e->window, XCB_WINDOW_NONE);
			free(wa);
			return;
		}
		case XCB_UNMAP_NOTIFY:
		{
			xcb_unmap_notify_event_t *e = (xcb_unmap_notify_event_t *)ev;

			if (!(c = wintoclient(e->window)) && !(p = wintopanel(e->window)))
				return;
			if (XCB_EVENT_SENT(e))
				setwinstate(c ? c->win : p->win, XCB_ICCCM_WM_STATE_WITHDRAWN);
			else if (c)
				freeclient(c, 0);
			else
				freepanel(p, 0);
			return;
		}
		case XCB_CLIENT_MESSAGE:
		{
			xcb_client_message_event_t *e = (xcb_client_message_event_t *)ev;

			if (e->window == root && e->type == netatoms[CurrentDesktop]) {
				view(&(const Arg){.ui = e->data.data32[0]});
			} else if ((c = wintoclient(e->window))) {
				if (e->type == netatoms[ActiveWindow]) {
					changews(c->ws, 0);
					focus(c);
					layoutws(NULL, 1);
				} else if (e->type == netatoms[State]
						&& (e->data.data32[1] == netatoms[Fullscreen]
							|| e->data.data32[2] == netatoms[Fullscreen]))
				{
					setfullscreen(c, e->data.data32[0]);
				}
			}
			return;
		}
		case XCB_PROPERTY_NOTIFY:
		{
			xcb_window_t trans;
			xcb_property_notify_event_t *e = (xcb_property_notify_event_t *)ev;

			if (e->state != XCB_PROPERTY_DELETE && (c = wintoclient(e->window))) {
				if (e->atom == XCB_ATOM_WM_TRANSIENT_FOR) {
					if (!c->floating && (trans = windowtrans(c->win)) != XCB_NONE
							&& (c->floating = (wintoclient(trans) != NULL)))
						layoutws(c->ws, 1);
				} else if (e->atom == XCB_ATOM_WM_NORMAL_HINTS) {
					sizehints(c);
				} else if (e->atom == XCB_ATOM_WM_HINTS) {
					windowhints(c);
				}
				if (e->atom == netatoms[WindowType])
					windowtype(c);
			}
			return;
		}
		default:
		{
			xcb_generic_error_t *e = (xcb_generic_error_t *)ev;

			if (ev->response_type /* ignored event, not an error */
					/* BadWindow */
					||  e->error_code == 3
					/* BadMatch & SetInputFocus/ConfigureWindow */
					|| (e->error_code == 8  && (e->major_code == 42 || e->major_code == 12))
					/* BadAccess & GrabButton/GrabKey */
					|| (e->error_code == 10 && (e->major_code == 28 || e->major_code == 33)))
				return;

			/* TODO: some kind of error handling for those we don't want to ignore */
			warnx("event error: %d: \"%s\" - %d: \"%s\"",
					e->error_code, xcb_event_get_error_label(e->error_code),
					e->major_code, xcb_event_get_request_label(e->major_code));
			return;
		}
	}
}

void eventloop(void)
{ /* wait for events while the user hasn't requested quit */
	xcb_generic_event_t *ev;

	xcb_aux_sync(con);
	while (running && (ev = xcb_wait_for_event(con)) != NULL) {
		eventhandle(ev);
		free(ev);
	}
}

void fixupworkspaces(void)
{ /* after monitor(s) change we need to reassign workspaces and resize fullscreen clients */
	Client *c;
	Monitor *m;
	Workspace *ws;
	uint v[] = { scr_w, scr_h };

	assignworkspaces();
	FOR_WSCLIENTS(c, ws)
		if (c->fullscreen) {
			m = ws->mon;
			resize(c, m->winarea_x, m->winarea_y, m->winarea_w, m->winarea_h);
		}
	PROP_REPLACE(root, netatoms[DesktopGeometry], XCB_ATOM_CARDINAL, 32, 2, v);
	if (panels)
		updatestruts(panels, 1);
	focus(NULL);
	layoutws(NULL, 0);
}

void focus(Client *c)
{ /* focus client (making it the head of the focus stack)
   * when client is NULL focus the current workspace stack head */
	if (!selws)
		return;
	if (!c || c->ws != c->ws->mon->ws)
		c = selws->stack;
	if (selws->sel && selws->sel != c)
		unfocus(selws->sel, 0);
	if (c) {
		DBG("focusing client window: 0x%x", c->win)
		if (c->ws != selws)
			selws = c->ws;
		if (c->urgent)
			seturgency(c, 0);
		detachstack(c);
		attachstack(c);
		grabbuttons(c, 1);
		xcb_change_window_attributes(con, c->win, XCB_CW_BORDER_PIXEL, &borders[Focus]);
		setfocus(c);
	} else {
		DBG("no available clients on this workspace, focusing root window")
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
		xcb_delete_property(con, root, netatoms[ActiveWindow]);
	}
	selws->sel = c;
}

void follow(const Arg *arg)
{ /* send selected client to a workspace and view that workspace */
	if (selws->sel && arg->ui != selws->num) {
		send(arg);
		view(arg);
	}
}

void freeclient(Client *c, int destroyed)
{ /* detach client and free it, if !destroyed we update the state to withdrawn */
	if (!c)
		return;
	Workspace *ws, *cws = c->ws;

	DBG("freeing client window: 0x%x - destroyed: %i", c->win, destroyed)
	detach(c, 0);
	detachstack(c);
	if (!destroyed) {
		xcb_grab_server(con);
		xcb_configure_window(con, c->win, BWMASK, &c->old_bw);
		xcb_ungrab_button(con, XCB_BUTTON_INDEX_ANY, c->win, XCB_MOD_MASK_ANY);
		setwinstate(c->win, XCB_ICCCM_WM_STATE_WITHDRAWN);
		xcb_aux_sync(con);
		xcb_ungrab_server(con);
	}
	free(c);
	focus(NULL);
	xcb_delete_property(con, root, netatoms[ClientList]);
	FOR_WSCLIENTS(c, ws)
		PROP_APPEND(root, netatoms[ClientList], XCB_ATOM_WINDOW, 32, 1, &c->win);
	layoutws(cws, 0);
}

void freemon(Monitor *m)
{ /* detach and free a monitor and it's name */
	Monitor *mon;

	if (m == monitors)
		monitors = monitors->next;
	else {
		FOR_PREV(mon, m, monitors);
		if (mon)
			mon->next = m->next;
	}
	DBG("freeing monitor: %s", m->name)
	free(m->name);
	free(m);
}

void freewm(void)
{ /* exit yaxwm, free everything and cleanup X */
	uint i;
	Workspace *ws;

	FOR_EACH(ws, workspaces)
		while (ws->stack)
			freeclient(ws->stack, 0);
	xcb_ungrab_key(con, XCB_GRAB_ANY, root, XCB_MOD_MASK_ANY);
	xcb_key_symbols_free(keysyms);
	while (panels)
		freepanel(panels, 0);
	while (monitors)
		freemon(monitors);
	while (workspaces)
		freews(workspaces);
	for (i = 0; i < LEN(cursors); i++)
		xcb_free_cursor(con, cursor[i]);
	for (i = 0; i < LEN(rules); i++)
		regfree(&rules[i].regcomp);
	xcb_destroy_window(con, wmcheck);
	xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT,
			XCB_INPUT_FOCUS_POINTER_ROOT, XCB_CURRENT_TIME);
	xcb_aux_sync(con);
	xcb_delete_property(con, root, netatoms[ActiveWindow]);
	xcb_disconnect(con);
}

void freews(Workspace *ws)
{ /* detach and free workspace */
	Workspace *sel;

	if (ws == workspaces)
		workspaces = workspaces->next;
	else {
		FOR_PREV(sel, ws, workspaces);
		if (sel)
			sel->next = ws->next;
	}
	DBG("freeing workspace: %s", ws->name)
	free(ws);
}

void grabbuttons(Client *c, int focused)
{
	updatenumlock();
	{ /* new scope to use updated numlock mask */
		uint btns[] = { BUTTON1, BUTTON2, BUTTON3 };
		uint mods[] = { 0, XCB_MOD_MASK_LOCK, numlockmask, numlockmask | XCB_MOD_MASK_LOCK };

		DBG("grabbing buttons on client window: 0x%x - focused: %d", c->win, focused)
		xcb_ungrab_button(con, XCB_BUTTON_INDEX_ANY, c->win, XCB_MOD_MASK_ANY);
		if (!focused)
			xcb_grab_button(con, 0, c->win, BUTTONMASK, ASYNC, SYNC, 0, XCB_NONE,
					XCB_BUTTON_INDEX_ANY, XCB_MOD_MASK_ANY);
		for (uint i = 0; i < LEN(mods); i++)
			for (uint j = 0; j < LEN(btns); j++)
				xcb_grab_button(con, 0, c->win, BUTTONMASK, ASYNC, SYNC, 0, XCB_NONE,
						btns[j], MODKEY | mods[i]);
	}
}

void grabkeys(void)
{
	updatenumlock();
	{ /* new scope to use updated numlock mask */
		xcb_keycode_t *c;
		uint mods[] = { 0, XCB_MOD_MASK_LOCK, numlockmask, numlockmask | XCB_MOD_MASK_LOCK };

		DBG("grabbing bound keys on the root window")
		xcb_ungrab_key(con, XCB_GRAB_ANY, root, XCB_MOD_MASK_ANY);

		for (uint i = 0; i < LEN(mods); i++)
			for (uint j = 0; j < LEN(binds); j++)
				if ((c = xcb_key_symbols_get_keycode(keysyms, binds[j].keysym))) {
					xcb_grab_key(con, 1, root, binds[j].mod | mods[i], *c, ASYNC, ASYNC);
					free(c);
				}
	}

}

int grabpointer(xcb_cursor_t cursor)
{ /* grab the mouse pointer on the root window with cursor passed */
	int r = 0;
	xcb_generic_error_t *e;
	xcb_grab_pointer_cookie_t pc;
	xcb_grab_pointer_reply_t *ptr;

	pc = xcb_grab_pointer(con, 0, root, GRABMASK, ASYNC, ASYNC, root, cursor, XCB_CURRENT_TIME);
	if ((ptr = xcb_grab_pointer_reply(con, pc, &e))) {
		r = ptr->status == XCB_GRAB_STATUS_SUCCESS;
		free(ptr);
	} else {
		checkerror("unable to grab pointer", e);
	}
	return r;
}

void ignorefocusevents(void)
{ /* A ton of events can be ignored here, most are focus in/out from us moving windows around
   * when changing workspaces if we don't, the focus will drift to whatever window is
   * under the cursor after switching.. Incredibly annoying! */
	xcb_generic_event_t *ev;

	xcb_aux_sync(con);
	while ((ev = xcb_poll_for_queued_event(con))) {
		switch (XCB_EVENT_RESPONSE_TYPE(ev)) {
			case XCB_FOCUS_IN: /* fallthrough */
			case XCB_FOCUS_OUT: /* fallthrough */
			case XCB_ENTER_NOTIFY: /* fallthrough */
			case XCB_LEAVE_NOTIFY:
				break;
			default:
				eventhandle(ev);
				break;
		}
		free(ev);
	}
}

void initatoms(xcb_atom_t *atoms, const char **names, int num)
{ /* intern atoms in bulk */
	int i;
	xcb_generic_error_t *e;
	xcb_intern_atom_reply_t *r;
	xcb_intern_atom_cookie_t c[num];

	for (i = 0; i < num; ++i)
		c[i] = xcb_intern_atom(con, 0, strlen(names[i]), names[i]);
	for (i = 0; i < num; ++i) {
		if ((r = xcb_intern_atom_reply(con, c[i], &e))) {
			DBG("initializing atom: %s - value: %d", names[i], r->atom)
			atoms[i] = r->atom;
			free(r);
		} else {
			checkerror("unable to initialize atom", e);
		}
	}
}

void initclient(xcb_window_t win, xcb_window_t trans)
{ /* allocate and setup new client from window */
	Client *c;
	Monitor *m;
	int x, y, w, h, bw;
	uint frame[] = { borders[Width], borders[Width], borders[Width], borders[Width] };

	DBG("initializing new client from window: 0x%x", win)
	c = ecalloc(1, sizeof(Client));
	c->win = win;
	clientrules(c, trans);
	m = c->ws ? c->ws->mon : selws->mon;

	if (windowgeom(c->win, &x, &y, &w, &h, &bw)) {
		DBG("using geometry given by the window")
		c->x = c->old_x = x;
		c->y = c->old_y = y;
		c->w = c->old_w = w;
		c->h = c->old_h = h;
		c->old_bw = bw;
	} else {
		DBG("using root window geometry")
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
	xcb_configure_window(con, c->win, BWMASK, &c->bw);
	configure(c);
	windowtype(c);
	sizehints(c);
	windowhints(c);
	xcb_change_window_attributes(con, c->win, XCB_CW_EVENT_MASK | XCB_CW_BORDER_PIXEL,
			(uint []){ borders[Focus], /* borders must always be before event mask due to the bit ordering */
			XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_FOCUS_CHANGE
			| XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY });
	grabbuttons(c, 0);
	if (c->floating || (c->floating = c->oldstate = c->fixed))
		setstackmode(c->win, XCB_STACK_MODE_ABOVE);
	PROP_APPEND(root, netatoms[ClientList], XCB_ATOM_WINDOW, 32, 1, &c->win);
	setwinstate(c->win, XCB_ICCCM_WM_STATE_NORMAL);
	PROP_REPLACE(c->win, netatoms[FrameExtents], XCB_ATOM_CARDINAL, 32, 4, frame);
	if (selws && c->ws == selws && selws->sel)
		unfocus(selws->sel, 0);
	if (c->ws)
		c->ws->sel = c;
	else if (selws)
		selws->sel = c;
	layoutws(c->ws, 0);
	xcb_map_window(con, c->win);
	focus(NULL);

	DBG("new client mapped on workspace %s: %d,%d @ %dx%d -- floating: %d",
			c->ws->name, c->x, c->y, c->w, c->h, c->floating)
}

void initexisting(void)
{ /* walk root window tree and init existing windows */
	uint i, num;
	xcb_window_t *win;
	xcb_generic_error_t *e;
	xcb_query_tree_cookie_t c;
	xcb_query_tree_reply_t *tree;
	xcb_atom_t iconified = XCB_ICCCM_WM_STATE_ICONIC;

	c = xcb_query_tree(con, root);
	DBG("getting root window tree and initializing existing child windows")
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
			} else if (windowprop(win[i], netatoms[WindowType]) == netatoms[WindowTypeDock]
					&& wa[i]->map_state != XCB_MAP_STATE_UNMAPPED)
			{
				initpanel(win[i]);
				win[i] = 0;
			} else if (!wa[i]->override_redirect
					&& (trans[i] = windowtrans(win[i])) == XCB_WINDOW_NONE
					&& (wa[i]->map_state == XCB_MAP_STATE_VIEWABLE
						|| (state[i] = windowprop(win[i], wmatoms[WMState])) == iconified))
			{
				initclient(win[i], XCB_WINDOW_NONE);
				win[i] = 0;
			}
		}
		for (i = 0; i < num; i++) { /* transients */
			if (win[i] && trans[i] && !wa[i]->override_redirect
					&& (wa[i]->map_state == XCB_MAP_STATE_VIEWABLE || state[i] == iconified))
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
{ /* allocate a monitor from randr output */
	Monitor *m;
	uint len = strlen(name) + 1;

	DBG("initializing new monitor: %s - %d,%d - %dx%d", name, x, y, w, h)
	m = ecalloc(1, sizeof(Monitor));
	m->x = m->winarea_x = x;
	m->y = m->winarea_y = y;
	m->w = m->winarea_w = w;
	m->h = m->winarea_h = h;
	m->id = id;
	m->name = ecalloc(1, len);
	if (len > 1)
		strlcpy(m->name, name, len);
	return m;
}

void initwm(void)
{ /* setup internals, binds, atoms, and root window event mask */
	int r;
	Workspace *ws;
	uint i, j, cws;
	size_t len = 1;
	char errbuf[256];
	xcb_void_cookie_t c;
	xcb_generic_error_t *e;
	xcb_cursor_context_t *ctx;
	uint v[] = { 0, 0 };

	/* monitor(s) & workspaces */
	if ((randrbase = initrandr()) < 0 || !monitors)
		monitors = initmon("default", 0, 0, 0, scr_w, scr_h);
	initworkspaces();
	assignworkspaces();

	/* cursors */
	if (xcb_cursor_context_new(con, scr, &ctx) < 0)
		errx(1, "unable to create cursor context");
	for (i = 0; i < LEN(cursors); i++)
		cursor[i] = xcb_cursor_load_cursor(ctx, cursors[i]);
	xcb_cursor_context_free(ctx);

	/* client rules regexes */
	for (i = 0; i < LEN(rules); i++)
		if ((r = regcomp(&rules[i].regcomp, rules[i].regex, REG_NOSUB | REG_EXTENDED | REG_ICASE))) {
			regerror(r, &rules[i].regcomp, errbuf, sizeof(errbuf));
			errx(1, "invalid regex rules[%d]: %s: %s\n", i, rules[i].regex, errbuf);
		}

	/* atoms */
	initatoms(wmatoms, wmatomnames, LEN(wmatomnames));
	initatoms(netatoms, netatomnames, LEN(netatomnames));

	/* create simple window for _NET_SUPPORTING_WM_CHECK and initialize it's atoms */
	wmcheck = xcb_generate_id(con);
	xcb_create_window(con, XCB_COPY_FROM_PARENT, wmcheck, root, -1, -1, 1, 1, 0,
			XCB_WINDOW_CLASS_INPUT_ONLY, scr->root_visual, 0, NULL);
	PROP_REPLACE(wmcheck, netatoms[Check], XCB_ATOM_WINDOW, 32, 1, &wmcheck);
	PROP_REPLACE(wmcheck, netatoms[Name], wmatoms[Utf8Str], 8, 5, "yaxwm");
	PROP_REPLACE(root, netatoms[Check], XCB_ATOM_WINDOW, 32, 1, &wmcheck);

	/* set most of the root window atoms that are unlikely to change often */
	updatenumws(numws);
	PROP_REPLACE(root, netatoms[DesktopViewport], XCB_ATOM_CARDINAL, 32, 2, v);
	v[0] = scr_w, v[1] = scr_h;
	PROP_REPLACE(root, netatoms[DesktopGeometry], XCB_ATOM_CARDINAL, 32, 2, v);
	PROP_REPLACE(root, netatoms[Supported], XCB_ATOM_ATOM, 32, NetLast, netatoms);
	xcb_delete_property(con, root, netatoms[ClientList]);

	/* CurrentDesktop */
	cws = (r = windowprop(root, netatoms[CurrentDesktop])) >= 0 ? r : 0;
	selws = (ws = itows(cws)) ? ws : workspaces;
	selws->mon->ws = ws;
	PROP_REPLACE(root, netatoms[CurrentDesktop], XCB_ATOM_CARDINAL, 32, 1, &selws->num);
	xcb_warp_pointer(con, root, root, 0, 0, 0, 0,
				selws->mon->x + (selws->mon->w / 2), selws->mon->y + (selws->mon->h / 2));

	/* DesktopNames */
	FOR_EACH(ws, workspaces)
		len += strlen(ws->name) + 1;
	char names[len];
	len = 0;
	FOR_EACH(ws, workspaces)
		for (j = 0; (names[len++] = ws->name[j]); j++)
			;
	PROP_REPLACE(root, netatoms[DesktopNames], wmatoms[Utf8Str], 8, --len, names);

	/* root window event mask & cursor */
	c = xcb_change_window_attributes_checked(con, root, XCB_CW_EVENT_MASK | XCB_CW_CURSOR,
			(uint []){ XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT
			| XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_BUTTON_PRESS
			| XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION
			| XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW
			| XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE,
			cursor[Normal] });
	if ((e = xcb_request_check(con, c))) {
		free(e);
		errx(1, "unable to change root window event mask and cursor");
	}

	/* binds */
	if (!(keysyms = xcb_key_symbols_alloc(con)))
		errx(1, "error unable to get keysyms from X connection");
	grabkeys();
	focus(NULL);
}

void initworkspaces(void)
{ /* setup default workspaces from user specified workspace rules */
	WsRule *def;
	Workspace *ws;

	for (numws = 0; numws < LEN(wsrules); numws++) {
		def = &wsrules[numws];
		FOR_TAIL(ws, workspaces);
		if (ws)
			ws->next = initws(numws, def->name, def->nmaster, def->splitratio, def->layout);
		else
			workspaces = initws(numws, def->name, def->nmaster, def->splitratio, def->layout);
	}
}

Workspace *initws(uint num, char *name, uint nmaster, float splitratio, void (*layout)(Workspace *))
{ /* allocate a new workspace with default values */
	Workspace *ws;

	DBG("initializing new workspace: '%s': %d", name, num)
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
{ /* close currently active client and free it */
	if (!selws->sel)
		return;
	DBG("user requested kill current client")
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

void layoutws(Workspace *ws, int allowfocusevents)
{ /* show currently visible clients and restack workspaces */
	if (ws) {
		showhide(ws->stack, allowfocusevents);
		if (ws->layout)
			ws->layout(ws);
		restack(ws);
	} else FOR_EACH(ws, workspaces) {
		showhide(ws->stack, allowfocusevents);
		if (ws == ws->mon->ws && ws->layout)
			ws->layout(ws);
		restack(ws);
	}
	xcb_aux_sync(con);
}

Client *nexttiled(Client *c)
{ /* return c */
	while (c && c->floating)
		c = c->next;
	return c;
}

int pointerxy(int *x, int *y)
{
	xcb_generic_error_t *e;
	xcb_query_pointer_reply_t *p;

	if ((p = xcb_query_pointer_reply(con, xcb_query_pointer(con, root), &e))) {
		*x = p->root_x, *y = p->root_y;
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

	c->x = c->old_x = x, c->y = c->old_y = y, c->w = c->old_w = w, c->h = c->old_h = h;
	if (c->ws && nexttiled(c->ws->clients) == c
			&& !nexttiled(c->next) && !c->floating && !c->fullscreen)
		c->bw = 0, v[2] = W(c), v[3] = H(c), v[4] = 0;
	else
		c->bw = borders[Width];
	xcb_configure_window(con, c->win, XYMASK | WHMASK | BWMASK, v);
	configure(c);
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
	DBG("restacking clients on workspace: %d", ws->num)
	if (c->floating || !ws->layout)
		setstackmode(c->win, XCB_STACK_MODE_ABOVE);
	if (ws->layout) {
		FOR_EACH(c, ws->stack)
			if (!c->floating && c->ws == c->ws->mon->ws)
				setstackmode(c->win, XCB_STACK_MODE_BELOW);
	}
}

int ruleregcmp(regex_t *r, char *class, char *inst)
{
	return (!regexec(r, class, 0, NULL, 0) || !regexec(r, inst, 0, NULL, 0));
}

void runcmd(const Arg *arg)
{
	DBG("user run command: %s", ((char **)arg->v)[0])
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
		setclientws(c, arg->ui);
		focus(NULL);
		layoutws(NULL, 0);
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

void setstackmode(xcb_window_t win, uint mode)
{
	xcb_configure_window(con, win, XCB_CONFIG_WINDOW_STACK_MODE, &mode);
}

void setwinstate(xcb_window_t win, long state)
{
	long s[] = { state, XCB_ATOM_NONE };
	PROP_REPLACE(win, wmatoms[WMState], wmatoms[WMState], 32, 2, s);
}

void setclientws(Client *c, uint num)
{
	DBG("setting client atom -- _NET_WM_DESKTOP: %d", num)
	if (c->ws) {
		detach(c, 0);
		detachstack(c);
	}
	c->ws = itows(num);
	PROP_REPLACE(c->win, netatoms[Desktop], XCB_ATOM_CARDINAL, 32, 1, &num);
	attach(c, 0);
	attachstack(c);
}

void setfocus(Client *c)
{
	if (!c->nofocus) {
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, c->win, XCB_CURRENT_TIME);
		PROP_REPLACE(root, netatoms[ActiveWindow], XCB_ATOM_WINDOW, 32, 1, &c->win);
	}
	sendevent(c, TakeFocus);
}

void setfullscreen(Client *c, int fullscreen)
{
	Monitor *m;

	if (!c->ws || !(m = c->ws->mon))
		m = selws->mon;
	if (fullscreen && !c->fullscreen) {
		PROP_REPLACE(c->win, netatoms[State], XCB_ATOM_ATOM, 32, 1, &netatoms[Fullscreen]);
		c->oldstate = c->floating;
		c->fullscreen = 1;
		c->old_bw = c->bw;
		c->bw = 0;
		c->floating = 1;
		resize(c, m->x, m->y, m->w, m->h);
		setstackmode(c->win, XCB_STACK_MODE_ABOVE);
	} else if (!fullscreen && c->fullscreen) {
		PROP_REPLACE(c->win, netatoms[State], XCB_ATOM_ATOM, 32, 0, (uchar *)0);
		c->floating = c->oldstate;
		c->fullscreen = 0;
		c->bw = c->old_bw;
		c->x = c->old_x;
		c->y = c->old_y;
		c->w = c->old_w;
		c->h = c->old_h;
		resize(c, c->x, c->y, c->w, c->h);
		layoutws(c->ws, 0);
	}
}

void setlayout(const Arg *arg)
{
	DBG("setting current monitor layout")
	if (arg && arg->v)
		selws->layout = (void (*)(Workspace *))arg->v;
	if (selws->sel)
		layoutws(selws, 0);
}

void setnmaster(const Arg *arg)
{
	selws->nmaster = MAX(selws->nmaster + arg->i, 0);
	layoutws(selws, 0);
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
		if (!(baseismin = c->base_w == c->min_w && c->base_h == c->min_h)) {
			/* temporarily remove base dimensions */
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
	DBG("setting split splitratio: %f -> %f", selws->splitratio, f)
	selws->splitratio = f;
	layoutws(selws, 0);
}

void seturgency(Client *c, int urg)
{
	xcb_generic_error_t *e;
	xcb_icccm_wm_hints_t wmh;
	xcb_get_property_cookie_t pc;

	pc = xcb_icccm_get_wm_hints(con, c->win);
	c->urgent = urg;
	DBG("setting urgency hint for window: 0x%x -- value: %d", c->win, urg)
	if (xcb_icccm_get_wm_hints_reply(con, pc, &wmh, &e)) {
		wmh.flags = urg ? (wmh.flags | XCB_ICCCM_WM_HINT_X_URGENCY)
			: (wmh.flags & ~XCB_ICCCM_WM_HINT_X_URGENCY);
		xcb_icccm_set_wm_hints(con, c->win, &wmh);
	} else {
		checkerror("unable to get wm window hints", e);
	}
}

void showhide(Client *c, int allowfocusevents)
{
	if (!c)
		return;
	if (c->ws == c->ws->mon->ws) {
		DBG("showing client window: 0x%x - workspace: %d", c->win, c->ws->num)
		MOVE(c->win, c->x, c->y);
		if ((!c->ws->layout || c->floating) && !c->fullscreen)
			resizehint(c, c->x, c->y, c->w, c->h, 0);
		showhide(c->snext, allowfocusevents);
	} else {
		showhide(c->snext, allowfocusevents);
		DBG("hiding client window: 0x%x - workspace: %d", c->win, c->ws->num)
		MOVE(c->win, W(c) * -2, c->y);
	}
	if (!allowfocusevents)
		ignorefocusevents();
}

void sighandle(int sig)
{
	switch (sig) {
	case SIGINT: /* fallthrough */
	case SIGTERM: /* fallthrough */
	case SIGHUP: /* fallthrough */
		freewm();
		exit(1);
		break;
	case SIGCHLD:
		while (waitpid(-1, NULL, WNOHANG) > 0)
			;
		break;
	}
}

void sizehints(Client *c)
{
	xcb_size_hints_t s;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t pc;

	pc = xcb_icccm_get_wm_normal_hints(con, c->win);
	DBG("setting client size hints")
	c->min_w = c->min_h = 0;
	c->max_w = c->max_h = 0;
	c->base_w = c->base_h = 0;
	c->max_aspect = c->min_aspect = 0.0;
	c->increment_w = c->increment_h = 0;
	if (xcb_icccm_get_wm_normal_hints_reply(con, pc, &s, &e)) {
		if (s.flags & XCB_ICCCM_SIZE_HINT_P_ASPECT) {
			c->min_aspect = (float)s.min_aspect_den / s.min_aspect_num;
			c->max_aspect = (float)s.max_aspect_num / s.max_aspect_den;
			DBG("set min/max aspect: min = %f, max = %f", c->min_aspect, c->max_aspect)
		}
		if (s.flags & XCB_ICCCM_SIZE_HINT_P_MAX_SIZE) {
			c->max_w = s.max_width;
			c->max_h = s.max_height;
			DBG("set max size: %dx%d", c->max_w, c->max_h)
		}
		if (s.flags & XCB_ICCCM_SIZE_HINT_P_RESIZE_INC) {
			c->increment_w = s.width_inc;
			c->increment_h = s.height_inc;
			DBG("set increment size: %dx%d", c->increment_w, c->increment_h)
		}
		if (s.flags & XCB_ICCCM_SIZE_HINT_BASE_SIZE) {
			c->base_w = s.base_width;
			c->base_h = s.base_height;
			DBG("set base size: %dx%d", c->base_w, c->base_h)
		} else if (s.flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) {
			c->base_w = s.min_width;
			c->base_h = s.min_height;
			DBG("set base size to min size: %dx%d", c->base_w, c->base_h)
		}
		if (s.flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) {
			c->min_w = s.min_width;
			c->min_h = s.min_height;
			DBG("set min size: %dx%d", c->min_w, c->min_h)
		} else if (s.flags & XCB_ICCCM_SIZE_HINT_BASE_SIZE) {
			c->min_w = s.base_width;
			c->min_h = s.base_height;
			DBG("set min size to base size: %dx%d", c->min_w, c->min_h)
		}
	} else {
		checkerror("unable to get wm normal hints", e);
	}
	c->fixed = (c->max_w && c->max_h && c->max_w == c->min_w && c->max_h == c->min_h);
	DBG("client is %s size", c->fixed ? "fixed" : "variable")
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
	DBG("swapping current client window: 0x%x", c->win)
	detach(c, 1);
	focus(c);
	layoutws(c->ws, 0);
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
	DBG("tiling workspace: %d", ws->num)
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
			resize(c, m->winarea_x + mw, m->winarea_y + ty,
					m->winarea_w - mw - (2*c->bw), h - (2*c->bw));
			ty += H(c);
		}
}

void togglefloat(const Arg *arg)
{
	Client *c;

	if (!(c = selws->sel) || c->fullscreen)
		return;
	(void)(arg);
	DBG("toggling selected window floating state: %d -> %d", c->floating, !c->floating)
	if ((c->floating = !c->floating || c->fixed))
		resizehint(c, (c->x = c->old_x), (c->y = c->old_y),
				(c->w = c->old_w), (c->h = c->old_h), 0);
	else
		c->old_x = c->x, c->old_y = c->y, c->old_w = c->w, c->old_h = c->h;
	layoutws(selws, 0);
}

void unfocus(Client *c, int focusroot)
{
	if (!c)
		return;
	DBG("unfocusing client window: 0x%x", c->win)
	grabbuttons(c, 0);
	xcb_change_window_attributes(con, c->win, XCB_CW_BORDER_PIXEL, &borders[Unfocus]);
	if (focusroot) {
		DBG("focusing root window")
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
		xcb_delete_property(con, root, netatoms[ActiveWindow]);
	}
}

void updatenumlock(void)
{
	xcb_keycode_t *c, *t;
	xcb_generic_error_t *e;
	xcb_get_modifier_mapping_reply_t *m;

	DBG("updating numlock bit mask")
	numlockmask = 0;
	if ((m = xcb_get_modifier_mapping_reply(con, xcb_get_modifier_mapping(con), &e))) {
		if (!(t = xcb_key_symbols_get_keycode(keysyms, XK_Num_Lock)))
			return;
		if (!(c = xcb_get_modifier_mapping_keycodes(m))) {
			free(t);
			return;
		}
		for (uint i = 0; i < 8; i++)
			for (uint j = 0; j < m->keycodes_per_modifier; j++)
				if (c[i * m->keycodes_per_modifier + j] == *t)
					numlockmask = (1 << i);
		free(t);
		free(m);
	} else {
		checkerror("unable to get modifier mapping for numlock", e);
	}
}

void updatenumws(uint needed)
{
	char name[4]; /* we're never gonna have more than 999 workspaces */
	Workspace *ws;

	if (needed > numws) {
		DBG("more monitors than workspaces, allocating enough for each monitor")
		while (needed > numws) {
			FOR_TAIL(ws, workspaces);
			if (ws)
				ws->next = initws(numws, itoa(numws, name),
						ws->nmaster, ws->splitratio, ws->layout);
			else
				workspaces = initws(numws, itoa(numws, name), 1, 0.5, tile);
			numws++;
		}
	}
	PROP_REPLACE(root, netatoms[NumDesktops], XCB_ATOM_CARDINAL, 32, 1, &numws);
}

void view(const Arg *arg)
{
	Workspace *ws;

	if (arg->ui == selws->num || !(ws = itows(arg->ui)))
		return;
	changews(ws, 0);
	focus(NULL);
	layoutws(NULL, 0);
}

xcb_get_window_attributes_reply_t *windowattr(xcb_window_t win)
{
	xcb_generic_error_t *e;
	xcb_get_window_attributes_cookie_t c;
	xcb_get_window_attributes_reply_t *wa;

	c = xcb_get_window_attributes(con, win);
	DBG("getting window attributes from window: 0x%x", win)
	if (!(wa = xcb_get_window_attributes_reply(con, c, &e)))
		checkerror("unable to get window attributes", e);
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
	DBG("checking and setting wm hints for window: 0x%x", c->win)
	if (xcb_icccm_get_wm_hints_reply(con, pc, &wmh, &e)) {
		if (c == selws->sel && wmh.flags & XCB_ICCCM_WM_HINT_X_URGENCY) {
			wmh.flags &= ~XCB_ICCCM_WM_HINT_X_URGENCY;
			xcb_icccm_set_wm_hints(con, c->win, &wmh);
		} else {
			c->urgent = (wmh.flags & XCB_ICCCM_WM_HINT_X_URGENCY) ? 1 : 0;
		}
		c->nofocus = (wmh.flags & XCB_ICCCM_WM_HINT_INPUT) ? !wmh.input : 0;
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
	DBG("getting window property atom %d from window: 0x%x", prop, win)
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
	DBG("getting transient for hint - window: 0x%x", win)
	if (!xcb_icccm_get_wm_transient_for_reply(con, pc, &trans, &e) && e) {
		warnx("unable to get wm transient for hint - X11 error: %d: %s",
				e->error_code, xcb_event_get_error_label(e->error_code));
		free(e);
	}
	return trans;
}

void windowtype(Client *c)
{
	DBG("checking window type for window: 0x%x", c->win)
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
	int x, y;
	Client *c;
	Workspace *ws;

	if (win == root && pointerxy(&x, &y))
		return ptrtomon(x, y)->ws;
	FOR_WSCLIENTS(c, ws)
		if (c->win == win)
			return ws;
	return selws;
}
