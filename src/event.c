/* dk window manager
 *
 * see license file for copyright and license details
 * vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
 */

#include <stdio.h>
#include <stdlib.h>
#include <regex.h>
#include <err.h>

#include <string.h>
#include <xcb/randr.h>
#include <xcb/xcb_util.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_keysyms.h>

#include "dk.h"
#include "cmd.h"
#include "event.h"

int released = 1, grabbing = 0;

static void (*handlers[XCB_NO_OPERATION + 1])(xcb_generic_event_t *) = {
	[XCB_BUTTON_PRESS] = &buttonpress,
	[XCB_CLIENT_MESSAGE] = &clientmessage,
	[XCB_CONFIGURE_NOTIFY] = &confignotify,
	[XCB_CONFIGURE_REQUEST] = &configrequest,
	[XCB_DESTROY_NOTIFY] = &destroynotify,
	[XCB_ENTER_NOTIFY] = &enternotify,
	[XCB_FOCUS_IN] = &focusin,
	[XCB_MAPPING_NOTIFY] = &mappingnotify,
	[XCB_MAP_REQUEST] = &maprequest,
	[XCB_MOTION_NOTIFY] = &motionnotify,
	[XCB_PROPERTY_NOTIFY] = &propertynotify,
	[XCB_UNMAP_NOTIFY] = &unmapnotify,
	[XCB_NO_OPERATION] = NULL};

void buttonpress(xcb_generic_event_t *ev)
{
	Client *c;
	xcb_generic_error_t *er;
	xcb_grab_pointer_cookie_t pc;
	xcb_button_press_event_t *e = (xcb_button_press_event_t *)ev;

	if (!(c = wintoclient(e->event))) {
		return;
	}
	if (c != selws->sel) {
		focus(c);
	}
	if (FLOATING(c) && (e->detail == mousemove || e->detail == mouseresize)) {
		setstackmode(c->win, XCB_STACK_MODE_ABOVE);
	}
	xcb_allow_events(con, XCB_ALLOW_REPLAY_POINTER, e->time);
	if ((e->state & ~(lockmask | XCB_MOD_MASK_LOCK)) == (mousemod & ~(lockmask | XCB_MOD_MASK_LOCK)) &&
		(e->detail == mousemove || e->detail == mouseresize)) {
		if (FULLSCREEN(c) || (STATE(c, FIXED) && e->detail != mousemove)) {
			return;
		}
		DBG("buttonpress: %s - %#08x", e->detail == mousemove ? "move" : "resize", e->event)
		xcb_grab_pointer_reply_t *p;
		pc = xcb_grab_pointer(con, 0, root,
							  XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_MOTION |
								  XCB_EVENT_MASK_POINTER_MOTION,
							  XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, root,
							  cursor[e->detail == mousemove ? CURS_MOVE : CURS_RESIZE], XCB_CURRENT_TIME);
		if ((p = xcb_grab_pointer_reply(con, pc, &er)) && p->status == XCB_GRAB_STATUS_SUCCESS) {
			mousemotion(c, e->detail, e->root_x, e->root_y);
		} else {
			iferr(0, "unable to grab pointer", er);
		}
		free(p);
	}
}

void buttonrelease(int move)
{
	DBG("buttonrelease: ungrabbing pointer - %#08x", selws->sel->win)
	iferr(1, "failed to ungrab pointer",
		  xcb_request_check(con, xcb_ungrab_pointer_checked(con, XCB_CURRENT_TIME)));
	if (!move) {
		ignore(XCB_ENTER_NOTIFY);
		xcb_aux_sync(con);
	}
}

void clientmessage(xcb_generic_event_t *ev)
{
	Client *c;
	Workspace *ws;
	xcb_client_message_event_t *e = (xcb_client_message_event_t *)ev;
	uint32_t *d = e->data.data32;

	if (e->window == root && e->type == netatom[NET_DESK_CUR]) {
		DBG("clientmessage: %#08x -- e->type = %d (_NET_CURRENT_DESKTOP)", e->window, e->type)
		unfocus(selws->sel, 1);
		cmdview(itows(d[0]));
	} else if (e->type == netatom[NET_CLOSE]) {
		DBG("clientmessage: %#08x -- e->type = %d (_NET_CLOSE_WINDOW)", e->window, e->type)
		unmanage(e->window, 1);
	} else if ((c = wintoclient(e->window))) {
		if (e->type == netatom[NET_WM_DESK]) {
			if (!(ws = itows(d[0]))) {
				warnx("invalid workspace index: %d", d[0]);
				return;
			}
			setworkspace(c, ws, c != c->ws->sel);
			wschange = winchange = needsrefresh = 1;
		} else if (e->type == netatom[NET_WM_STATE]) {
			if (d[1] == netatom[NET_STATE_FULL] || d[2] == netatom[NET_STATE_FULL]) {
				int full = (d[0] == 1 || (d[0] == 2 && !STATE(c, FULLSCREEN)));
				if (VISIBLE(c)) {
					setfullscreen(c, full);
					ignore(XCB_ENTER_NOTIFY);
					xcb_aux_sync(con);
				}
			} else if (d[1] == netatom[NET_STATE_ABOVE] || d[2] == netatom[NET_STATE_ABOVE]) {
				int above = d[0] == 1 || (d[0] == 2 && !STATE(c, ABOVE));
				if (above && !STATE(c, ABOVE)) {
					c->state |= STATE_ABOVE | STATE_FLOATING;
					needsrefresh = 1;
				} else if (!above && STATE(c, ABOVE)) {
					c->state &= ~STATE_ABOVE;
					needsrefresh = 1;
				}
			} else if ((d[1] == netatom[NET_STATE_DEMANDATT] || d[2] == netatom[NET_STATE_DEMANDATT]) &&
					   c != selws->sel) {
				goto activate;
			}
		} else if (e->type == netatom[NET_ACTIVE] && c != selws->sel) {
activate:
			if (globalcfg[GLB_FOCUS_URGENT].val && !STATE(c, IGNOREMSG) && !STATE(c, SCRATCH)) {
				if (grabbing && !released) {
					released = 1, grabbing = 0;
					buttonrelease(0);
				}
				if (c->ws != selws) {
					unfocus(selws->sel, 1);
					cmdview(c->ws);
				}
				focus(c);
				if (e->type == netatom[NET_WM_STATE]) {
					setnetstate(c->win, c->state);
				}
			} else {
				seturgent(c, 1);
				clientborder(c, 0);
			}
			needsrefresh = refresh();
		}
	}
	xcb_flush(con);
}

void confignotify(xcb_generic_event_t *ev)
{
	xcb_configure_notify_event_t *e = (xcb_configure_notify_event_t *)ev;

	if (e->window == root) {
		scr_w = e->width;
		scr_h = e->height;
	}
}

void configrequest(xcb_generic_event_t *ev)
{
	Client *c;
	Monitor *m;
	xcb_configure_request_event_t *e = (xcb_configure_request_event_t *)ev;

	if ((c = wintoclient(e->window))) {
		if (e->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH) {
			c->bw = e->border_width;
		} else if (FLOATING(c)) {
			if (!VISIBLE(c) || STATE(c, IGNORECFG) ||
				((e->value_mask & XCB_CONFIG_WINDOW_X) &&
				 (e->x == W(c) * -2 || c->x + 1 <= e->x || c->x + 1 >= e->x))) {
				DBG("configrequest: %#08x: floating - ignoring hidden window or small shift", c->win)
				return;
			}
			m = MON(c);
			if (e->value_mask & XCB_CONFIG_WINDOW_X) {
				c->old_x = c->x;
				c->x = m->x + e->x;
			}
			if (e->value_mask & XCB_CONFIG_WINDOW_Y) {
				c->old_y = c->y;
				c->y = m->y + e->y;
			}
			if (e->value_mask & XCB_CONFIG_WINDOW_WIDTH) {
				c->old_w = c->w;
				c->w = CLAMP(e->width, globalcfg[GLB_MIN_WH].val, m->w);
			}
			if (e->value_mask & XCB_CONFIG_WINDOW_HEIGHT) {
				c->old_h = c->h;
				c->h = CLAMP(e->height, globalcfg[GLB_MIN_WH].val, m->h);
			}
			if (c->x + c->w < m->x + globalcfg[GLB_MIN_XY].val ||
				c->x > m->x + m->w - globalcfg[GLB_MIN_XY].val) {
				c->x = m->x + (m->w / 2 - W(c) / 2);
			}
			if (c->y + c->h < m->y + globalcfg[GLB_MIN_XY].val ||
				c->y > m->y + m->h - globalcfg[GLB_MIN_XY].val) {
				c->y = m->y + (m->h / 2 - H(c) / 2);
			}
			applysizehints(c, &c->x, &c->y, &c->w, &c->h, c->bw, 0, 0);
			resize(c, c->x, c->y, c->w, c->h, c->bw);
		} else {
			sendconfigure(c);
		}
	} else {
		xcb_params_configure_window_t wc = {.x = e->x,
											.y = e->y,
											.width = e->width,
											.height = e->height,
											.sibling = e->sibling,
											.stack_mode = e->stack_mode,
											.border_width = e->border_width};
		xcb_aux_configure_window(con, e->window, e->value_mask, &wc);
	}
	xcb_flush(con);
}

void destroynotify(xcb_generic_event_t *ev)
{
	DBG("destroynotify: %#08x", ((xcb_destroy_notify_event_t *)ev)->window)
	unmanage(((xcb_destroy_notify_event_t *)ev)->window, 1);
}

void dispatch(xcb_generic_event_t *ev)
{
	short type;

	if ((type = XCB_EVENT_RESPONSE_TYPE(ev))) {
		if (handlers[type]) {
			handlers[type](ev);
		} else if (ev->response_type == randrbase + XCB_RANDR_SCREEN_CHANGE_NOTIFY &&
				   ((xcb_randr_screen_change_notify_event_t *)ev)->root == root) {
			if (updrandr(0)) {
				updworkspaces(globalcfg[GLB_NUM_WS].val);
			}
			updstruts();
		}
	} else {
		xcb_generic_error_t *e = (xcb_generic_error_t *)ev;

		/* ignore some specific error types */
		if (e->error_code == XCB_WINDOW ||
			(e->error_code == XCB_MATCH &&
			 (e->major_code == XCB_SET_INPUT_FOCUS || e->major_code == XCB_CONFIGURE_WINDOW)) ||
			(e->error_code == XCB_ACCESS &&
			 (e->major_code == XCB_GRAB_BUTTON || e->major_code == XCB_GRAB_KEY)) ||
			(e->error_code == XCB_DRAWABLE &&
			 (e->major_code == XCB_CREATE_PIXMAP || e->major_code == XCB_CREATE_GC ||
			  e->major_code == XCB_POLY_FILL_RECTANGLE)) ||
			(e->error_code == XCB_G_CONTEXT &&
			 (e->major_code == XCB_CHANGE_GC || e->major_code == XCB_FREE_GC)) ||
			(e->error_code == XCB_PIXMAP && e->major_code == XCB_FREE_PIXMAP)) {
			return;
		}
		fprintf(stderr,
				"dk: previous request returned error %i, \"%s\""
				" major code %u, minor code %u resource id %u sequence %u\n",
				e->error_code, xcb_event_get_error_label(e->error_code), (uint32_t)e->major_code,
				(uint32_t)e->minor_code, (uint32_t)e->resource_id, (uint32_t)e->sequence);
	}
}

void enternotify(xcb_generic_event_t *ev)
{
	Client *c;
	Monitor *m;
	Workspace *ws;
	xcb_enter_notify_event_t *e = (xcb_enter_notify_event_t *)ev;

	if (e->event != root &&
		(e->mode != XCB_NOTIFY_MODE_NORMAL || e->detail == XCB_NOTIFY_DETAIL_INFERIOR)) {
		return;
	}
	DBG("enternotify: %#08x", e->event)
	ws = selws;
	if ((c = wintoclient(e->event))) {
		ws = c->ws;
	} else if ((m = coordtomon(e->root_x, e->root_y))) {
		ws = m->ws;
	}
	if (ws && ws != selws) {
		changews(ws, 0, 0);
	}
	if (c && c != ws->sel && globalcfg[GLB_FOCUS_MOUSE].val) {
		focus(c);
	}
}

void focusin(xcb_generic_event_t *ev)
{
	xcb_focus_in_event_t *e = (xcb_focus_in_event_t *)ev;

	if (e->mode == XCB_NOTIFY_MODE_GRAB || e->mode == XCB_NOTIFY_MODE_UNGRAB ||
		e->detail == XCB_NOTIFY_DETAIL_POINTER || e->detail == XCB_NOTIFY_DETAIL_POINTER_ROOT ||
		e->detail == XCB_NOTIFY_DETAIL_NONE) {
		return;
	}
	if (selws->sel && e->event != selws->sel->win) {
		DBG("focusin: %#08x", e->event)
		setinputfocus(selws->sel);
	}
}

void ignore(uint8_t type)
{
	xcb_generic_event_t *ev = NULL;

	xcb_flush(con);
	while (running && (ev = xcb_poll_for_event(con))) {
		if (XCB_EVENT_RESPONSE_TYPE(ev) != type) {
			dispatch(ev);
		}
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
#define BODY grabbuttons(c);
		FOR_CLIENTS (c, ws)
#undef BODY
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
		DBG("motionnotify: updating active monitor - %#08x", e->event)
		changews(m->ws, 0, 0);
		focus(NULL);
	}
}

static void mousemotion_move(Client *c, int mx, int my)
{
	Monitor *m = selws->mon;
	xcb_timestamp_t last = 0;
	xcb_motion_notify_event_t *e;
	xcb_generic_event_t *ev = NULL;
	int ox = c->x, oy = c->y, nx, ny, w, h;

	/* single pass to ensure the border is drawn and the client is floating */
	if (!FLOATING(c) || (STATE(c, FULLSCREEN) && STATE(c, FAKEFULL))) {
		while (running && !released && (ev = xcb_wait_for_event(con))) {
			switch (XCB_EVENT_RESPONSE_TYPE(ev)) {
				case XCB_MOTION_NOTIFY:
					e = (xcb_motion_notify_event_t *)ev;
					if (e->time - last < 1000 / 60) {
						break;
					}
					last = e->time;
					nx = ox + (e->root_x - mx);
					ny = oy + (e->root_y - my);
					if (nx == c->x && ny == c->y) {
						break;
					}
					c->state |= STATE_FLOATING;
					c->old_state |= STATE_FLOATING;
					if (selws->layout->func) {
						selws->layout->func(selws);
					}
					setstackmode(c->win, XCB_STACK_MODE_ABOVE);
					w = c->w, h = c->h;
					resizehint(c, nx, ny, w, h, c->bw, 1, 1);
					free(ev);
					goto primary_loop;
					break;
				case XCB_BUTTON_RELEASE:
					grabbing = 0, released = 1;
					buttonrelease(1);
					break;
				default: dispatch(ev);
			}
			free(ev);
		}
	}
primary_loop:
	while (running && !released && (ev = xcb_wait_for_event(con))) {
		switch (XCB_EVENT_RESPONSE_TYPE(ev)) {
			case XCB_MOTION_NOTIFY:
				e = (xcb_motion_notify_event_t *)ev;
				if (e->time - last < 1000 / 60) {
					break;
				}
				last = e->time;
				nx = ox + (e->root_x - mx);
				ny = oy + (e->root_y - my);
				if (nx == c->x && ny == c->y) {
					break;
				}
				if ((m = coordtomon(e->root_x, e->root_y)) && m->ws != c->ws) {
					setworkspace(c, m->ws, 0);
					changews(m->ws, 0, 0);
					focus(c);
				}
				w = c->w, h = c->h;
				if (applysizehints(c, &nx, &ny, &w, &h, c->bw, 1, 1)) {
					c->x = nx, c->y = ny, c->w = w, c->h = h;
					MOVERESIZE(c->win, c->x, c->y, c->w, c->h, c->bw);
					sendconfigure(c);
					xcb_flush(con);
				}
				break;
			case XCB_BUTTON_RELEASE:
				grabbing = 0, released = 1;
				buttonrelease(1);
				break;
			default: dispatch(ev);
		}
		free(ev);
	}
}

/* already floating windows and tiled layouts that don't support resize */
static void mousemotion_resize(Client *c, int mx, int my)
{
	xcb_timestamp_t last = 0;
	xcb_motion_notify_event_t *e;
	xcb_generic_event_t *ev = NULL;
	int x, y, nw, nh, ow = c->w, oh = c->h;
	;

	while (running && !released && (ev = xcb_wait_for_event(con))) {
		switch (XCB_EVENT_RESPONSE_TYPE(ev)) {
			case XCB_MOTION_NOTIFY:
				e = (xcb_motion_notify_event_t *)ev;
				if (e->time - last < 1000 / 60) {
					break;
				}
				last = e->time;
				nw = ow + (e->root_x - mx);
				nh = oh + (e->root_y - my);
				if (nw == c->w && nh == c->h) {
					break;
				}
				if (!FLOATING(c) || (STATE(c, FULLSCREEN) && STATE(c, FAKEFULL))) {
					c->state |= STATE_FLOATING;
					c->old_state |= STATE_FLOATING;
					if (selws->layout->func) {
						selws->layout->func(selws);
					}
					setstackmode(c->win, XCB_STACK_MODE_ABOVE);
				}
				resizehint(c, (x = c->x), (y = c->y), nw, nh, c->bw, 1, 1);
				break;
			case XCB_BUTTON_RELEASE:
				grabbing = 0, released = 1;
				buttonrelease(0);
				break;
			default: dispatch(ev);
		}
		free(ev);
	}
}

/* duplicate code in both _resizet and _resizetinv */
#define HEIGHT_OFFSET                                                                                        \
	if (prev || nearend) {                                                                                   \
		int ohoff = c->hoff;                                                                                 \
		if (first) {                                                                                         \
			first = 0;                                                                                       \
			if (isend) {                                                                                     \
				c->hoff = ((e->root_y - my) * -1) + ohoff;                                                   \
				my += ohoff;                                                                                 \
			} else {                                                                                         \
				c->hoff = (e->root_y - my) + ohoff;                                                          \
				my -= ohoff;                                                                                 \
			}                                                                                                \
		} else {                                                                                             \
			c->hoff = isend ? (e->root_y - my) * -1 : e->root_y - my;                                        \
		}                                                                                                    \
		if (selws->layout->func(selws) < 0) {                                                                \
			c->hoff = ohoff;                                                                                 \
			selws->layout->func(selws);                                                                      \
		}                                                                                                    \
	} else if (selws->layout->func(selws) < 0) {                                                             \
		selws->layout->func(selws);                                                                          \
	}                                                                                                        \
	xcb_flush(con)

/* layouts that support resize with standard tiling direction */
static void mousemotion_resizet(Client *c, Client *prev, int idx, int mx, int my, int isend, int nearend)
{
	Monitor *m = selws->mon;
	xcb_timestamp_t last = 0;
	xcb_motion_notify_event_t *e;
	xcb_generic_event_t *ev = NULL;
	int first = 1, ow = c->w, ox = c->x;

	while (running && !released && (ev = xcb_wait_for_event(con))) {
		switch (XCB_EVENT_RESPONSE_TYPE(ev)) {
			case XCB_MOTION_NOTIFY:
				e = (xcb_motion_notify_event_t *)ev;
				if (e->time - last < 1000 / 60) {
					break;
				}
				last = e->time;
				if (selws->nstack && idx >= selws->nstack + selws->nmaster) {
					selws->ssplit = CLAMP((ox - m->x + (e->root_x - mx) - (m->w * selws->msplit)) /
											  (m->w - (m->w * selws->msplit)),
										  0.05, 0.95);
				} else if (selws->nmaster && idx >= selws->nmaster) {
					selws->msplit = CLAMP((float)(ox - m->x + (e->root_x - mx)) / m->w, 0.05, 0.95);
				} else {
					selws->msplit = CLAMP((float)(ox - m->x + ow + (e->root_x - mx)) / m->w, 0.05, 0.95);
				}
				HEIGHT_OFFSET;
				break;
			case XCB_BUTTON_RELEASE:
				grabbing = 0, released = 1;
				buttonrelease(0);
				break;
			default: dispatch(ev);
		}
		free(ev);
	}
}

/* layouts that support resize with inverted tiling direction */
static void mousemotion_resizetinv(Client *c, Client *prev, int idx, int mx, int my, int isend, int nearend)
{
	Monitor *m = selws->mon;
	xcb_timestamp_t last = 0;
	xcb_motion_notify_event_t *e;
	xcb_generic_event_t *ev = NULL;
	int first = 1, ow = c->w, ox = c->x;

	while (running && !released && (ev = xcb_wait_for_event(con))) {
		switch (XCB_EVENT_RESPONSE_TYPE(ev)) {
			case XCB_MOTION_NOTIFY:
				e = (xcb_motion_notify_event_t *)ev;
				if (e->time - last < 1000 / 60) {
					break;
				}
				last = e->time;
				if (selws->nstack && idx >= selws->nstack + selws->nmaster) {
					selws->ssplit = CLAMP(
						(ox - m->x + ow - (e->root_x - mx)) / (m->w - (m->w * selws->msplit)), 0.05, 0.95);
				} else if (selws->nmaster && idx >= selws->nmaster) {
					selws->msplit = CLAMP((float)(ox - m->x + ow - (e->root_x - mx)) / m->w, 0.05, 0.95);
				} else {
					selws->msplit = CLAMP((float)(ox - m->x - (e->root_x - mx)) / m->w, 0.05, 0.95);
				}
				HEIGHT_OFFSET;
				break;
			case XCB_BUTTON_RELEASE:
				grabbing = 0, released = 1;
				buttonrelease(0);
				break;
			default: dispatch(ev);
		}
		free(ev);
	}
}
#undef HEIGHT_OFFSET

void mousemotion(Client *c, xcb_button_t button, int mx, int my)
{
	grabbing = 1, released = 0;

	if (button == mousemove) {
		mousemotion_move(c, mx, my);
	} else {
		if (!FLOATING(c) && c->ws->layout->implements_resize) {
			int i, isend, nearend;
			Client *p, *prev = NULL;
			for (i = 0, p = nexttiled(selws->clients); p && p != c; p = nexttiled(p->next), i++) {
				if (nexttiled(p->next) == c) {
					prev = (i + 1 == selws->nmaster || i + 1 == selws->nstack + selws->nmaster) ? NULL : p;
				}
			}
			/* calculate these here to save the resize loops extra unnecessary work */
			isend = i + 1 == selws->nmaster || i + 1 == selws->nmaster + selws->nstack || !nexttiled(c->next);
			nearend = (i == selws->nmaster || i == selws->nmaster + selws->nstack) && nexttiled(c->next);

			if (!selws->layout->invert_split_direction) {
				mousemotion_resizet(c, prev, i, mx, my, isend, nearend);
			} else {
				mousemotion_resizetinv(c, prev, i, mx, my, isend, nearend);
			}
		} else {
			mousemotion_resize(c, mx, my);
		}
	}
}

void propertynotify(xcb_generic_event_t *ev)
{
	Panel *p;
	Client *c;
	xcb_property_notify_event_t *e = (xcb_property_notify_event_t *)ev;

	if (e->state == XCB_PROPERTY_DELETE || e->window == root) {
		return;
	}
	if ((c = wintoclient(e->window))) {
		switch (e->atom) {
			case XCB_ATOM_WM_HINTS: clienthints(c); break;
			case XCB_ATOM_WM_NORMAL_HINTS: c->hints = 0; break;
			case XCB_ATOM_WM_TRANSIENT_FOR:
				if ((c->trans = wintoclient(wintrans(c->win))) && !FLOATING(c)) {
					c->state |= STATE_FLOATING;
					needsrefresh = 1;
				}
				break;
			default:
				if (e->atom == XCB_ATOM_WM_NAME || e->atom == netatom[NET_WM_NAME]) {
					if (clientname(c)) {
						winchange = 1;
					}
				} else if (e->atom == netatom[NET_WM_TYPE]) {
					clienttype(c);
				}
		}
	} else if ((e->atom == netatom[NET_WM_STRUTP] || e->atom == netatom[NET_WM_STRUT]) &&
			   (p = wintopanel(e->window))) {
		fillstruts(p);
		updstruts();
		needsrefresh = 1;
	}
}

void unmapnotify(xcb_generic_event_t *ev)
{
	xcb_generic_error_t *er;
	xcb_unmap_notify_event_t *e = (xcb_unmap_notify_event_t *)ev;

	if (e->event != root) {
		free(xcb_query_tree_reply(con, xcb_query_tree(con, e->window), &er));
		if (er) {
			free(er);
			return;
		}
		DBG("unmapnotify: un-managing window: %#08x", e->window)
		unmanage(e->window, 0);
	}
}
