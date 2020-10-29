/* dk - /dəˈkā/ window manager
 *
 * see license file for copyright and license details
 * vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
 */

#include "event.h"

void buttonpress(xcb_generic_event_t *ev)
{
	Client *c;
	xcb_generic_error_t *er;
	xcb_grab_pointer_cookie_t pc;
	xcb_grab_pointer_reply_t *p = NULL;
	xcb_button_press_event_t *e = (xcb_button_press_event_t *)ev;

	if (!(c = wintoclient(e->event))) return;
	DBG("buttonpress: 0x%08x - button: %d", e->event, e->detail)
	focus(c);
	restack(c->ws);
	xcb_allow_events(con, XCB_ALLOW_REPLAY_POINTER, e->time);
	if (CLNMOD(e->state) == CLNMOD(mousemod) && (e->detail == mousemove || e->detail == mouseresize)) {
		int move = e->detail == mousemove;
		if (FULLSCREEN(c) || ((c->state & STATE_FIXED) && !move))
			return;
		DBG("buttonpress: grabbing pointer for move/resize - 0x%08x", e->event)
		pc = xcb_grab_pointer(con, 0, root, XCB_EVENT_MASK_BUTTON_RELEASE
				| XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_POINTER_MOTION,
				XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, root,
				cursor[move ? CURS_MOVE : CURS_RESIZE], XCB_CURRENT_TIME);
		if ((p = xcb_grab_pointer_reply(con, pc, &er)) && p->status == XCB_GRAB_STATUS_SUCCESS)
			mouse(c, move, e->root_x, e->root_y);
		else
			iferr(0, "unable to grab pointer", er);
		free(p);
	}
}

void clientmessage(xcb_generic_event_t *ev)
{
	Client *c;
	xcb_client_message_event_t *e = (xcb_client_message_event_t *)ev;
	unsigned int *d = e->data.data32;

	DBG("clientmessage: 0x%08x", e->window)
	if (e->window == root && e->type == netatom[NET_DESK_CUR]) {
		unfocus(selws->sel, 1);
		cmdview(itows(d[0]));
	} else if (e->type == netatom[NET_CLOSE]) {
		unmanage(e->window, 1);
	} else if ((c = wintoclient(e->window))) {
		if (e->type == netatom[NET_WM_DESK]) {
			if (!itows(d[0])) {
				warnx("invalid workspace index: %d", d[0]);
				return;
			}
			setworkspace(c, d[0], c != c->ws->sel);
			needsrefresh = 1;
		} else if (e->type == netatom[NET_WM_STATE]
				&& (d[1] == netatom[NET_STATE_FULL] || d[2] == netatom[NET_STATE_FULL]))
		{
			setfullscreen(c, (d[0] == 1 || (d[0] == 2 && !(c->state & STATE_FULLSCREEN))));
		} else if (e->type == netatom[NET_ACTIVE] && c != selws->sel) {
			if (globalcfg[GLB_FOCUS_URGENT]) {
				if (c->ws != selws) {
					unfocus(selws->sel, 1);
					cmdview(c->ws);
				}
				focus(c);
			} else {
				seturgent(c, 1);
			}
		}
	}
}

void confignotify(xcb_generic_event_t *ev)
{
	xcb_configure_notify_event_t *e = (xcb_configure_notify_event_t *)ev;

	if (e->window != root)
		return;
	scr_w = e->width;
	scr_h = e->height;
}

void configrequest(xcb_generic_event_t *ev)
{
	Client *c;
	Monitor *m;
	xcb_configure_request_event_t *e = (xcb_configure_request_event_t *)ev;

	if ((c = wintoclient(e->window))) {
		DBG("configrequest: managed %s window 0x%08x",
			FLOATING(c) ? "floating" : "tiled", e->window)
			if (e->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH) {
				c->bw = e->border_width;
			} else if (FLOATING(c)) {
				m = c->ws->mon;
				SAVEOLD(c);
				if (e->value_mask & XCB_CONFIG_WINDOW_X && e->x != W(c) * -2)
					c->x = m->x + e->x - c->bw;
				if (e->value_mask & XCB_CONFIG_WINDOW_Y && e->x != W(c) * -2)
					c->y = m->y + e->y - c->bw;
				if (e->value_mask & XCB_CONFIG_WINDOW_WIDTH)
					c->w = e->width;
				if (e->value_mask & XCB_CONFIG_WINDOW_HEIGHT)
					c->h = e->height;
				if (c->x + c->w > m->wx + m->ww)
					c->x = m->wx + ((m->ww - W(c)) / 2);
				if (c->y + c->h > m->wy + m->wh)
					c->y = m->wy + ((m->wh - H(c)) / 2);
				if (e->value_mask & (XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y)
						&& !(e->value_mask & (XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT)))
					sendconfigure(c);
				if (c->ws == m->ws && e->x != W(c) * -2)
					resize(c, c->x, c->y, c->w, c->h, c->bw);
				else
					c->state |= STATE_NEEDSRESIZE;
			} else {
				sendconfigure(c);
			}
	} else {
		DBG("configrequest: unmanaged - 0x%08x", e->window)
		xcb_params_configure_window_t wc;
		wc.x = e->x, wc.y = e->y, wc.border_width = e->border_width;
		wc.width = e->width, wc.height = e->height;
		wc.sibling = e->sibling;
		wc.stack_mode = e->stack_mode;
		xcb_configure_window(con, e->window, e->value_mask, &wc);
	}
	xcb_aux_sync(con);
}

void destroynotify(xcb_generic_event_t *ev)
{
	unmanage(((xcb_destroy_notify_event_t *)ev)->window, 1);
}

void dispatch(xcb_generic_event_t *ev)
{
	short type;

	if ((type = ev->response_type & 0x7f)) {
		if (handlers[type])
			handlers[type](ev);
		else if (ev->response_type == randrbase + XCB_RANDR_SCREEN_CHANGE_NOTIFY)
			randr(ev);
	} else {
		error(ev);
	}
}

void enternotify(xcb_generic_event_t *ev)
{
	Client *c;
	Monitor *m;
	Workspace *ws;
	xcb_enter_notify_event_t *e = (xcb_enter_notify_event_t *)ev;

	if (e->event != root && (e->mode != XCB_NOTIFY_MODE_NORMAL
				|| e->detail == XCB_NOTIFY_DETAIL_INFERIOR))
		return;
	DBG("enternotify: 0x%08x", e->event)
	ws = selws;
	if ((c = wintoclient(e->event)))
		ws = c->ws;
	else if ((m = coordtomon(e->root_x, e->root_y)))
		ws = m->ws;
	if (ws && ws != selws)
		changews(ws, 0, 0);
	if (c && globalcfg[GLB_FOCUS_MOUSE])
		focus(c);
}

void error(xcb_generic_event_t *ev)
{
	xcb_generic_error_t *e = (xcb_generic_error_t*)ev;

	fprintf(stderr, "dk: previous request returned error %i, \"%s\""
			" major code %u, minor code %u resource id %u sequence %u\n",
			(int)e->error_code, xcb_event_get_error_label(e->error_code),
			(uint32_t) e->major_code, (uint32_t) e->minor_code,
			(uint32_t) e->resource_id, (uint32_t) e->sequence);
}

void focusin(xcb_generic_event_t *ev)
{
	xcb_focus_in_event_t *e = (xcb_focus_in_event_t *)ev;

	if (e->mode == XCB_NOTIFY_MODE_GRAB
			|| e->mode == XCB_NOTIFY_MODE_UNGRAB
			|| e->detail == XCB_NOTIFY_DETAIL_POINTER
			|| e->detail == XCB_NOTIFY_DETAIL_POINTER_ROOT
			|| e->detail == XCB_NOTIFY_DETAIL_NONE)
		return;
	if (selws->sel && e->event != selws->sel->win) {
		DBG("focusin: 0x%08x", e->event)
		setinputfocus(selws->sel);
	}
}

void ignore(uint8_t type)
{
	xcb_generic_event_t *ev = NULL;

	xcb_flush(con);
	while (running && (ev = xcb_poll_for_event(con))) {
		if ((ev->response_type & 0x7f) != type)
			dispatch(ev);
		free(ev);
	}
}

void maprequest(xcb_generic_event_t *ev)
{
	xcb_get_geometry_reply_t *g;
	xcb_get_window_attributes_reply_t *wa;
	xcb_map_request_event_t *e = (xcb_map_request_event_t *)ev;

	if (!(wa = winattr(e->window)) || !(g = wingeom(e->window)))
		return;
	manage(e->window, g, wa);
	free(wa);
	free(g);
}

void motionnotify(xcb_generic_event_t *ev)
{
	Monitor *m;
	xcb_motion_notify_event_t *e = (xcb_motion_notify_event_t *)ev;

	if (e->event == root && (m = coordtomon(e->root_x, e->root_y)) && m->ws != selws) {
		DBG("motionnotify: updating active monitor - 0x%08x", e->event)
		changews(m->ws, 0, 0);
		focus(NULL);
	}
}

void mouse(Client *c, int move, int mx, int my)
{
	Monitor *m;
	xcb_timestamp_t last = 0;
	xcb_motion_notify_event_t *e;
	xcb_generic_event_t *ev = NULL;
	int ox, oy, ow, oh, nw, nh, nx, ny, released = 0;

	ox = nx = c->x;
	oy = ny = c->y;
	ow = nw = c->w;
	oh = nh = c->h;
	while (running && !released && (ev = xcb_wait_for_event(con))) {
		switch (ev->response_type & 0x7f) {
		case XCB_MOTION_NOTIFY:
			e = (xcb_motion_notify_event_t *)ev;
			if ((e->time - last) < (1000 / 60))
				break;
			last = e->time;

			if (!move && !(c->state & STATE_FLOATING) && selws->layout->func == tile) {
				int i = 0;
				Client *p = nexttiled(selws->clients);
				for (; p && p != c; p = nexttiled(p->next), i++)
					;
				if (i >= selws->nstack + selws->nmaster)
					selws->ssplit =
						(double)(ox - selws->mon->x + (e->root_x - mx)
								- (selws->mon->ww * selws->msplit))
						/ (double)(selws->mon->ww - (selws->mon->ww * selws->msplit));
				else if (i >= selws->nmaster)
					selws->msplit = (double)(ox - selws->mon->x + (e->root_x - mx))
						/ (double)selws->mon->ww;
				else
					selws->msplit = (double)((ox - selws->mon->x + ow) + (e->root_x - mx))
						/ (double)selws->mon->ww;
				int ohoff = c->hoff;
				if (i + 1 == selws->nmaster || i + 1 == selws->nmaster + selws->nstack
						|| !nexttiled(c->next))
					c->hoff = (e->root_y - my) * -1;
				else
					c->hoff = e->root_y - my;
				if (selws->layout->func(selws) < 0)
					c->hoff = ohoff;
			} else {
				if (move) {
					nx = ox + (e->root_x - mx);
					ny = oy + (e->root_y - my);
				} else {
					nw = ow + (e->root_x - mx);
					nh = oh + (e->root_y - my);
				}
				if ((nw != c->w || nh != c->h || nx != c->x || ny != c->y)) {
					if (!FLOATING(c) || (c->state & STATE_FULLSCREEN
								&& c->state & STATE_FAKEFULL && !(c->old_state & STATE_FLOATING)))
					{
						c->state |= STATE_FLOATING;
						c->old_state |= STATE_FLOATING;
						if (c->max_w) c->w = MIN(c->w, c->max_w);
						if (c->max_h) c->h = MIN(c->h, c->max_h);
						c->x = CLAMP(c->x, selws->mon->wx, selws->mon->wx + selws->mon->ww - W(c));
						c->y = CLAMP(c->y, selws->mon->wy, selws->mon->wy + selws->mon->wh - H(c));
						resizehint(c, c->x, c->y, c->w, c->h, c->bw, 1, 1);
						if (selws->layout->func)
							selws->layout->func(selws);
						restack(selws);
					}
					if (move && (m = coordtomon(e->root_x, e->root_y)) && m->ws != c->ws) {
						setworkspace(c, m->ws->num, 0);
						changews(m->ws, 0, 0);
						focus(c);
					}
					resizehint(c, nx, ny, nw, nh, c->bw, 1, 1);
					xcb_flush(con);
				}
			}
			break;
		case XCB_BUTTON_RELEASE:
			released = 1;
			DBG("buttonrelease: ungrabbing pointer - 0x%08x", selws->sel->win)
				iferr(1, "failed to ungrab pointer",
						xcb_request_check(con, xcb_ungrab_pointer_checked(con, XCB_CURRENT_TIME)));
			if (!move) ignore(XCB_ENTER_NOTIFY);
			break;
		default: /* handle other event types normally */
			dispatch(ev);
			break;
		}
		free(ev);
	}
}

void propertynotify(xcb_generic_event_t *ev)
{
	Panel *p;
	Client *c;
	xcb_property_notify_event_t *e = (xcb_property_notify_event_t *)ev;

#ifdef DEBUG
	if (e->window != root) {
		for (unsigned int i = 0; i < LEN(netatom); i++)
			if (netatom[i] == e->atom) {
				DBG("propertynotify: atom: %s - 0x%08x", netatoms[i], e->window)
				break;
			}
		for (unsigned int i = 0; i < LEN(wmatom); i++)
			if (wmatom[i] == e->atom) {
				DBG("propertynotify: atom: %s - 0x%08x", wmatoms[i], e->window)
				break;
			}
	}
#endif
	if (e->state == XCB_PROPERTY_DELETE)
		return;
	if ((c = wintoclient(e->window))) {
		switch (e->atom) {
		case XCB_ATOM_WM_HINTS:
			clienthints(c); return;
		case XCB_ATOM_WM_NORMAL_HINTS:
			sizehints(c, 0); return;
		case XCB_ATOM_WM_TRANSIENT_FOR:
			if ((c->trans = wintoclient(wintrans(c->win))) && !FLOATING(c)) {
				c->state |= STATE_FLOATING;
				needsrefresh = 1;
			}
			return;
		default:
			if (e->atom == XCB_ATOM_WM_NAME || e->atom == netatom[NET_WM_NAME]) {
				if (clientname(c)) pushstatus();
			} else if (e->atom == netatom[NET_WM_TYPE])
				clienttype(c);
			return;
		}
	} else if ((e->atom == netatom[NET_WM_STRUTP] || e->atom == netatom[NET_WM_STRUT])
			&& (p = wintopanel(e->window)))
	{
		updstruts(p, 1);
		needsrefresh = 1;
	}
}

void randr(xcb_generic_event_t *ev)
{
	if (((xcb_randr_screen_change_notify_event_t *)ev)->root == root && updrandr())
		updworkspaces(globalcfg[GLB_NUMWS]);
}

void unmapnotify(xcb_generic_event_t *ev)
{
	xcb_unmap_notify_event_t *e = (xcb_unmap_notify_event_t *)ev;

	if (e->response_type & ~0x7f)
		setwmwinstate(e->window, XCB_ICCCM_WM_STATE_WITHDRAWN);
	else {
		DBG("unmapnotify: 0x%08x", e->window)
		unmanage(e->window, 0);
	}
}