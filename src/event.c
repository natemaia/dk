/* dk window manager
 *
 * see license file for copyright and license details
 * vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
 */

#include <stdio.h>
#include <stdlib.h>
#include <regex.h>
#include <err.h>

#include <xcb/randr.h>
#include <xcb/xcb_util.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_keysyms.h>

#include "dk.h"
#include "cmd.h"
#include "parse.h"
#include "layout.h"
#include "event.h"


static void (*handlers[XCB_NO_OPERATION + 1])(xcb_generic_event_t *) = {
	[XCB_BUTTON_PRESS]      = &buttonpress,
	[XCB_CLIENT_MESSAGE]    = &clientmessage,
	[XCB_CONFIGURE_NOTIFY]  = &confignotify,
	[XCB_CONFIGURE_REQUEST] = &configrequest,
	[XCB_DESTROY_NOTIFY]    = &destroynotify,
	[XCB_ENTER_NOTIFY]      = &enternotify,
	[XCB_FOCUS_IN]          = &focusin,
	[XCB_MAPPING_NOTIFY]    = &mappingnotify,
	[XCB_MAP_REQUEST]       = &maprequest,
	[XCB_MOTION_NOTIFY]     = &motionnotify,
	[XCB_PROPERTY_NOTIFY]   = &propertynotify,
	[XCB_UNMAP_NOTIFY]      = &unmapnotify,
	[XCB_NO_OPERATION]      = NULL
};

void buttonpress(xcb_generic_event_t *ev)
{
	Client *c;
	xcb_generic_error_t *er;
	xcb_grab_pointer_cookie_t pc;
	xcb_button_press_event_t *e = (xcb_button_press_event_t *)ev;

	if (!(c = wintoclient(e->event))) return;
	if (c != selws->sel) focus(c);
	if (FLOATING(c)) setstackmode(c->win, XCB_STACK_MODE_ABOVE);
	xcb_allow_events(con, XCB_ALLOW_REPLAY_POINTER, e->time);
	if ((e->state & ~(lockmask | XCB_MOD_MASK_LOCK)) == (mousemod & ~(lockmask | XCB_MOD_MASK_LOCK))
			&& (e->detail == mousemove || e->detail == mouseresize))
	{
		if (FULLSCREEN(c) || ((c->state & STATE_FIXED) && e->detail != mousemove))
			return;
		DBG("buttonpress: %s - 0x%08x", e->detail == mousemove ? "move" : "resize", e->event)
		xcb_grab_pointer_reply_t *p;
		pc = xcb_grab_pointer(con, 0, root, XCB_EVENT_MASK_BUTTON_RELEASE
				| XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_POINTER_MOTION,
				XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, root,
				cursor[e->detail == mousemove ? CURS_MOVE : CURS_RESIZE], XCB_CURRENT_TIME);
		if ((p = xcb_grab_pointer_reply(con, pc, &er)) && p->status == XCB_GRAB_STATUS_SUCCESS)
			mousemotion(c, e->detail, e->root_x, e->root_y);
		else
			iferr(0, "unable to grab pointer", er);
		free(p);
	}
}

void buttonrelease(int move)
{
	DBG("buttonrelease: ungrabbing pointer - 0x%08x", selws->sel->win)
	iferr(1, "failed to ungrab pointer",
			xcb_request_check(con, xcb_ungrab_pointer_checked(con, XCB_CURRENT_TIME)));
	if (!move) {
		xcb_aux_sync(con);
		ignore(XCB_ENTER_NOTIFY);
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
		} else if (e->type == netatom[NET_WM_STATE]) {
			if (d[1] == netatom[NET_STATE_FULL] || d[2] == netatom[NET_STATE_FULL]) {
				setfullscreen(c, (d[0] == 1 || (d[0] == 2 && !(c->state & STATE_FULLSCREEN))));
			} else if ((d[1] == netatom[NET_STATE_DEMANDATT]
						|| d[2] == netatom[NET_STATE_DEMANDATT]) && c != selws->sel)
			{
				goto act;
			}
		} else if (e->type == netatom[NET_ACTIVE] && c != selws->sel) {
act:
			if (globalcfg[GLB_FOCUS_URGENT].val) {
				setnetstate(c->win, c->state);
				if (c->ws != selws) {
					unfocus(selws->sel, 1);
					cmdview(c->ws);
				}
				focus(c);
				restack(c->ws);
			} else {
				seturgent(c, 1);
			}
			needsrefresh = 1;
		}
	}
}

void confignotify(xcb_generic_event_t *ev)
{
	xcb_configure_notify_event_t *e = (xcb_configure_notify_event_t *)ev;

	if (e->window != root) return;
	scr_w = e->width;
	scr_h = e->height;
}

void configrequest(xcb_generic_event_t *ev)
{
	Client *c;
	Monitor *m;
	xcb_configure_request_event_t *e = (xcb_configure_request_event_t *)ev;

	if ((c = wintoclient(e->window))) {
		if (c->state & STATE_IGNORECFG || e->x == W(c) * -2
				|| e->x <= (c->ws->mon->x - c->w) + globalcfg[GLB_MIN_WH].val)
			return;
		DBG("configrequest: managed %s client 0x%08x", FLOATING(c) ?"floating":"tiled", e->window)
		if (e->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH) {
			DBG("configrequest: bw: %d -> %d", c->bw, e->border_width)
			c->bw = e->border_width;
		} else if (FLOATING(c)) {
			m = c->ws->mon;
			if (e->value_mask & XCB_CONFIG_WINDOW_X) {
				DBG("configrequest: x: %d - > %d", c->x, m->x + e->x)
				c->old_x = c->x;
				c->x = m->x + e->x;
			}
			if (e->value_mask & XCB_CONFIG_WINDOW_Y) {
				DBG("configrequest: y: %d - > %d", c->y, m->y + e->y)
				c->old_y = c->y;
				c->y = m->y + e->y;
			}
			if (e->value_mask & XCB_CONFIG_WINDOW_WIDTH && !(c->state & STATE_FIXED)) {
				DBG("configrequest: w: %d - > %d", c->w, e->width)
				c->old_w = c->w;
				c->w = CLAMP(e->width, globalcfg[GLB_MIN_WH].val, m->w);
			}
			if (e->value_mask & XCB_CONFIG_WINDOW_HEIGHT && !(c->state & STATE_FIXED)) {
				DBG("configrequest: h: %d - > %d", c->h, e->height)
				c->old_h = c->h;
				c->h = CLAMP(e->height, globalcfg[GLB_MIN_WH].val, m->h);
			}
			if ((c->x + c->w < m->x + globalcfg[GLB_MIN_XY].val || c->x > m->x + m->w - globalcfg[GLB_MIN_XY].val) && c->state & STATE_FLOATING) {
				DBG("configrequest: x is out of monitor bounds, centering: %d -> %d", c->x, m->x + (m->w / 2 - W(c) / 2))
				c->x = m->x + (m->w / 2 - W(c) / 2);
			}
			if ((c->y + c->h < m->y + globalcfg[GLB_MIN_XY].val || c->y > m->y + m->h - globalcfg[GLB_MIN_XY].val) && c->state & STATE_FLOATING) {
				DBG("configrequest: y is out of monitor bounds, centering: %d -> %d", c->y, m->y + (m->h / 2 - H(c) / 2))
				c->y = m->y + (m->h / 2 - H(c) / 2);
			}
			if (e->value_mask & (XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y)
					&& !(e->value_mask & (XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT)))
			{
				DBG("configrequest: changing x/y but not width/height, sending configure notify: %d,%d", c->x, c->y)
				sendconfigure(c);
			}
			if (c->ws == m->ws) {
				DBG("configrequest: visible window, performing resize: %d,%d %dx%d", c->x, c->y, c->w, c->h)
				MOVERESIZE(c->win, c->x, c->y, c->w, c->h, c->bw);
			}
		} else {
			sendconfigure(c);
		}
		/* xcb_aux_sync(con); */
		/* return; */
	} else {
		DBG("configrequest: 0x%08x - %d,%d @ %dx%d", e->window, e->x, e->y, e->width, e->height)
		xcb_params_configure_window_t wc = {
			.x = e->x,
			.y = e->y,
			.width = e->width,
			.height = e->height,
			.sibling = e->sibling,
			.stack_mode = e->stack_mode,
			.border_width = e->border_width
		};
		xcb_aux_configure_window(con, e->window, e->value_mask, &wc);
	}
	xcb_flush(con);
}

void destroynotify(xcb_generic_event_t *ev)
{
	unmanage(((xcb_destroy_notify_event_t *)ev)->window, 1);
}

void dispatch(xcb_generic_event_t *ev)
{
	short type;

	if ((type = ev->response_type & 0x7f)) {
		if (handlers[type]) {
			handlers[type](ev);
		} else if (ev->response_type == randrbase + XCB_RANDR_SCREEN_CHANGE_NOTIFY) {
			if (((xcb_randr_screen_change_notify_event_t *)ev)->root == root && updrandr())
				updworkspaces(globalcfg[GLB_WS_NUM].val);
		}
	} else {
		xcb_generic_error_t *e = (xcb_generic_error_t*)ev;

		if (e->error_code == XCB_WINDOW
				|| (e->error_code == XCB_MATCH
					&& (e->major_code == XCB_SET_INPUT_FOCUS || e->major_code == XCB_CONFIGURE_WINDOW))
				|| (e->error_code == XCB_ACCESS
					&& (e->major_code == XCB_GRAB_BUTTON || e->major_code == XCB_GRAB_KEY)))
			return;
		fprintf(stderr, "dk: previous request returned error %i, \"%s\""
				" major code %u, minor code %u resource id %u sequence %u\n",
				(int)e->error_code, xcb_event_get_error_label(e->error_code),
				(uint32_t) e->major_code, (uint32_t) e->minor_code,
				(uint32_t) e->resource_id, (uint32_t) e->sequence);
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
	if (c && globalcfg[GLB_FOCUS_MOUSE].val)
		focus(c);
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

void mappingnotify(xcb_generic_event_t *ev)
{
	Client *c;
	Workspace *ws;
	xcb_mapping_notify_event_t *e = (xcb_mapping_notify_event_t *)ev;

	if (e->request == XCB_MAPPING_KEYBOARD || e->request == XCB_MAPPING_MODIFIER) {
		xcb_refresh_keyboard_mapping(keysyms, e);
		FOR_CLIENTS(c, ws)
			grabbuttons(c);
	}
}

void maprequest(xcb_generic_event_t *ev)
{
	manage(((xcb_map_request_event_t *)ev)->window, 0);
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

void mousemotion(Client *c, xcb_button_t button, int mx, int my)
{
	int released = 0;
	Monitor *m = selws->mon;
	xcb_timestamp_t last = 0;
	xcb_motion_notify_event_t *e;
	xcb_generic_event_t *ev = NULL;
	int ox = c->x, oy = c->y;

	if (button == mousemove) {
		int nx, ny;

		while (running && !released && (ev = xcb_wait_for_event(con))) {
			switch (ev->response_type & 0x7f) {
			case XCB_MOTION_NOTIFY:
				e = (xcb_motion_notify_event_t *)ev;
				if (e->time - last < 1000 / 60) break;
				last = e->time;
				nx = ox + (e->root_x - mx);
				ny = oy + (e->root_y - my);
				if (nx == c->x && ny == c->y) break;
				if (!FLOATING(c) || (c->state & STATE_FULLSCREEN
							&& c->state & STATE_FAKEFULL && !(c->old_state & STATE_FLOATING)))
				{
					DBG("mousemotion: popping float: %d,%d", c->x, c->y)
					int x = c->x, y = c->y, w = c->w, h = c->h;
					c->state |= STATE_FLOATING;
					c->old_state |= STATE_FLOATING;
					if (c->max_w) w = MIN(c->w, c->max_w);
					if (c->max_h) h = MIN(c->h, c->max_h);
					if (w == c->ws->mon->ww) w = c->ws->mon->w * 0.75;
					if (h == c->ws->mon->wh) h = c->ws->mon->h * 0.75;
					x = CLAMP(x, c->ws->mon->x, c->ws->mon->x + c->ws->mon->w - W(c));
					y = CLAMP(y, c->ws->mon->y, c->ws->mon->y + c->ws->mon->h - H(c));
					if (x + y == c->ws->mon->x + c->ws->mon->y) {
						x += c->ws->mon->w - ((c->ws->mon->w * 0.75) / 2);
						y += c->ws->mon->h - ((c->ws->mon->h * 0.75) / 2);
					}
					DBG("mousemotion: popping float -- new: %d,%d", c->x, c->y)
					resizehint(c, x, y, w, h, c->bw, 1, 1);
					if (selws->layout->func) selws->layout->func(selws);
					restack(selws);
				}
				if ((m = coordtomon(e->root_x, e->root_y)) && m->ws != c->ws) {
					setworkspace(c, m->ws->num, 0);
					changews(m->ws, 0, 0);
					focus(c);
				}
				resizehint(c, nx, ny, c->w, c->h, c->bw, 1, 1);
				xcb_flush(con);
				break;
			case XCB_BUTTON_RELEASE:
				released = 1;
				buttonrelease(1);
				break;
			default: /* handle other event types normally */
				dispatch(ev);
				break;
			}
			free(ev);
		}
	} else {
		Client *p, *prev = NULL;
		int (*lyt)(Workspace *) = selws->layout->func;
		int i, nw, nh, first = 1, left = lyt == ltile, ow = c->w, oh = c->h;

		for (i = 0, p = nexttiled(selws->clients); p && p != c; p = nexttiled(p->next), i++)
			if (nexttiled(p->next) == c)
				prev = (i + 1 == selws->nmaster || i + 1 == selws->nstack + selws->nmaster) ? NULL : p;
		while (running && !released && (ev = xcb_wait_for_event(con))) {
			switch (ev->response_type & 0x7f) {
			case XCB_MOTION_NOTIFY:
				e = (xcb_motion_notify_event_t *)ev;
				if (e->time - last < 1000 / 60) break;
				last = e->time;

				if (!(c->state & STATE_FLOATING) && ISTILE(selws)) {
					/* TODO: fix this shit, surely there's a better way that I'm not seeing
					 * this whole block is just calculating the split ratio and height
					 * offset of the current tiled client based on mouse movement (a resize) */
					if (selws->nstack && i >= selws->nstack + selws->nmaster) {
						if (left)
							selws->ssplit = (float)(ox - m->x + (e->root_x - mx)
									- (m->w * selws->msplit)) / (float)(m->w - (m->w * selws->msplit));
						else
							selws->ssplit = (float)(ox - m->x + ow - (e->root_x - mx))
								/ (float)(m->w - (m->w * selws->msplit));
						selws->ssplit = CLAMP(selws->ssplit, 0.05, 0.95);
					} else if (selws->nmaster && i >= selws->nmaster) {
						if (left)
							selws->msplit = (float)(ox - m->x + (e->root_x - mx)) / (float)m->w;
						else
							selws->msplit = (float)(ox - m->x + ow - (e->root_x - mx)) / (float)m->w;
						selws->msplit = CLAMP(selws->msplit, 0.05, 0.95);
					} else {
						if (left)
							selws->msplit = (float)(ox - m->x + ow + (e->root_x - mx)) / (float)m->w;
						else
							selws->msplit = (float)(ox - m->x - (e->root_x - mx)) / (float)m->w;
						selws->msplit = CLAMP(selws->msplit, 0.05, 0.95);
					}

					if (prev || ((i == selws->nmaster || i == selws->nmaster + selws->nstack)
								&& nexttiled(c->next)))
					{
						int ohoff = c->hoff;
						if (first) {
							first = 0;
							if (i + 1 == selws->nmaster || i + 1 == selws->nmaster + selws->nstack
									|| !nexttiled(c->next))
							{
								c->hoff = ((e->root_y - my) * -1) + ohoff;
								my += ohoff;
							} else {
								c->hoff = (e->root_y - my) + ohoff;
								my -= ohoff;
							}
						} else {
							if (i + 1 == selws->nmaster || i + 1 == selws->nmaster + selws->nstack
									|| !nexttiled(c->next))
								c->hoff = ((e->root_y - my) * -1);
							else
								c->hoff = (e->root_y - my);
						}
						if (selws->layout->func(selws) < 0) c->hoff = ohoff;
					} else {
						selws->layout->func(selws);
					}

				} else {
					nw = ow + (e->root_x - mx);
					nh = oh + (e->root_y - my);
					if (nw == c->w && nh == c->h) break;
					if (!FLOATING(c) || (c->state & STATE_FULLSCREEN
								&& c->state & STATE_FAKEFULL && !(c->old_state & STATE_FLOATING)))
					{
						c->state |= STATE_FLOATING;
						c->old_state |= STATE_FLOATING;
						if (selws->layout->func) selws->layout->func(selws);
						restack(selws);
					}
					resizehint(c, c->x, c->y, nw, nh, c->bw, 1, 1);
					xcb_flush(con);
				}
				break;
			case XCB_BUTTON_RELEASE:
				released = 1;
				buttonrelease(0);
				break;
			default: /* handle other event types normally */
				dispatch(ev);
				break;
			}
			free(ev);
		}
	}
}

void propertynotify(xcb_generic_event_t *ev)
{
	Panel *p;
	Client *c;
	xcb_property_notify_event_t *e = (xcb_property_notify_event_t *)ev;

	if (e->state == XCB_PROPERTY_DELETE || e->window == root) return;
	if ((c = wintoclient(e->window))) {
		switch (e->atom) {
		case XCB_ATOM_WM_HINTS: clienthints(c); break;
		case XCB_ATOM_WM_NORMAL_HINTS: sizehints(c, 0); break;
		case XCB_ATOM_WM_TRANSIENT_FOR:
			if ((c->trans = wintoclient(wintrans(c->win))) && !FLOATING(c)) {
				c->state |= STATE_FLOATING;
				needsrefresh = 1;
			}
			break;
		default:
			if (e->atom == XCB_ATOM_WM_NAME || e->atom == netatom[NET_WM_NAME]) {
				if (clientname(c)) winchange = 1;
			} else if (e->atom == netatom[NET_WM_TYPE]) {
				clienttype(c);
			}
			break;
		}
	} else if ((e->atom == netatom[NET_WM_STRUTP] || e->atom == netatom[NET_WM_STRUT])
			&& (p = wintopanel(e->window)))
	{
		updstruts(p, 1);
		needsrefresh = 1;
	}
}

void unmapnotify(xcb_generic_event_t *ev)
{
	xcb_generic_error_t *er;
	xcb_unmap_notify_event_t *e = (xcb_unmap_notify_event_t *)ev;

	if (e->event == root) return;
	free(xcb_query_tree_reply(con, xcb_query_tree(con, e->window), &er));
	if (er) { free(er); return; }
	if (e->response_type & ~0x7f) {
		setwinstate(e->window, XCB_ICCCM_WM_STATE_WITHDRAWN);
	} else {
		DBG("unmapnotify: 0x%08x", e->window)
		unmanage(e->window, 0);
	}
}
