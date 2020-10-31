/* dk - /dəˈkā/ window manager
 *
 * see license file for copyright and license details
 * vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
 */

#include <regex.h>
#include <stdio.h>

#ifdef DEBUG
#include <err.h>
#endif

#include <xcb/randr.h>
#include <xcb/xcb_util.h>
#include <xcb/xcb_keysyms.h>

#include "dk.h"
#include "cmd.h"
#include "layout.h"
#include "parse.h"
#include "config.h"


int dwindle(Workspace *ws)
{
	return fib(ws, 1);
}

int fib(Workspace *ws, int out)
{
	Client *c;
	Monitor *m = ws->mon;
	unsigned int i, n, x, y;
	int w, h, g, f = 0, ret = 1;

	for (n = 0, c = nexttiled(ws->clients); c; c = nexttiled(c->next), n++)
		;
	if (!n) return ret;

	g = globalcfg[GLB_SMART_GAP] && n == 1 ? 0 : ws->gappx;
	x = m->wx + ws->padl;
	y = m->wy + ws->padt;
	w = m->ww - ws->padl - ws->padr;
	h = m->wh - ws->padt - ws->padb;

	for (i = 0, c = nexttiled(ws->clients); c; c = nexttiled(c->next), i++) {
		unsigned int ox = x;
		int *p = (i % 2) ? &h : &w;
		int b = globalcfg[GLB_SMART_BORDER] && n == 1 ? 0 : c->bw;
		if (i < n - 1) {
			*p /= 2;
			if (!out) {
				if (i % 4 == 2)
					x += w;
				else if (i % 4 == 3)
					y += h;
			}
		}
		switch (i % 4) {
		case 0: y += out ? h : h * -1; break;
		case 1: x += w; break;
		case 2: y += h; break;
		case 3: x += out ? w : w * -1; break;
		}
		if (!i) {
			if (n > 1)
				w = ((m->ww - ws->padl - ws->padr) * ws->msplit) - g;
			y = m->wy - ws->padt;
		} else if (i == 1) {
			w = m->ww - ws->padl - ws->padr - w - g;
		}
		if (f || *p - (2 * b) - (n > 1 ? g : (2 * g)) < globalcfg[GLB_MIN_WH]) {
			*p *= 2;
			x = (i % 2) ? x : ox;
			if (f) {
				popfloat(c);
				continue;
			}
			f = 1;
			ret = -1;
		}
		resizehint(c, x + g, y + g, w - (2 * b) - (n > 1 ? g : (2 * g)),
				h - (2 * b) - (n > 1 ? g : (2 * g)), b, 0, 0);
	}
	xcb_aux_sync(con);
	return ret;
}

int grid(Workspace *ws)
{
	Client *c;
	Monitor *m = ws->mon;
	int wx, wy, ww, wh;
	int i, n, g, cols, rows, col, row;

	for (n = 0, c = nexttiled(ws->clients); c; c = nexttiled(c->next), n++)
		;
	if (!n) return 1;
	for (cols = 0; cols <= n / 2; cols++)
		if (cols * cols >= n)
			break;
	if (n == 5)
		cols = 2;
	rows = n / cols;
	wx = m->wx + ws->padl;
	wy = m->wy + ws->padt;
	ww = m->ww - ws->padl - ws->padr;
	wh = m->wh - ws->padt - ws->padb;
	g = globalcfg[GLB_SMART_GAP] && n == 1 ? 0 : ws->gappx;

	for (i = col = row = 0, c = nexttiled(ws->clients); c; i++, c = nexttiled(c->next)) {
		if (i / rows + 1 > cols - n % cols)
			rows = n / cols + 1;
		int b = globalcfg[GLB_SMART_BORDER] && n == 1 ? 0 : c->bw;
		int ch = rows ? (wh - g) / rows : wh;
		int cw = cols ? (ww - g) / cols : ww;
		int cx = (wx + g) + col * cw;
		int cy = (wy + g) + row * ch;
		resizehint(c, cx, cy, cw - (2 * b) - g, ch - (2 * b) - g, b, 0, 0);
		if (++row >= rows) {
			row = 0;
			col++;
		}
	}
	xcb_aux_sync(con);
	return 1;
}

int mono(Workspace *ws)
{
	int g;
	Client *c;

	g = globalcfg[GLB_SMART_GAP] ? 0 : ws->gappx;
	for (c = nexttiled(ws->clients); c; c = nexttiled(c->next)) {
		int b = globalcfg[GLB_SMART_BORDER] ? 0 : c->bw;
		resizehint(c, ws->mon->wx + ws->padl + g, ws->mon->wy + ws->padt + g,
				ws->mon->ww - ws->padl - ws->padr - (2 * g) - (2 * b),
				ws->mon->wh - ws->padt - ws->padb - (2 * g) - (2 * b),
				globalcfg[GLB_SMART_BORDER] ? 0 : c->bw, 0, 0);
	}
	xcb_aux_sync(con);
	return 1;
}

int spiral(Workspace *ws)
{
	return fib(ws, 0);
}

int tile(Workspace *ws)
{
	Monitor *m = ws->mon;
	Client *c, *prev = NULL;
	int i, n, remaining, my, sy, ssy, g, ret = 0;
	int x, *y, wx, wy, ww, wh, mw, ss, sw, ssw, ns = 1;

	for (n = 0, c = nexttiled(ws->clients); c; c = nexttiled(c->next), n++)
		;
	if (!n) return 1;

	mw = ss = sw = ssw = 0;
	wx = m->wx + ws->padl;
	wy = m->wy + ws->padt;
	ww = m->ww - ws->padl - ws->padr;
	wh = m->wh - ws->padt - ws->padb;
	g = !globalcfg[GLB_SMART_GAP] || n > 1 ? ws->gappx : 0;

	if (n <= ws->nmaster)
		mw = ww, ss = 1;
	else if (ws->nmaster)
		ns = 2, mw = ww * ws->msplit;
	if (n - ws->nmaster <= ws->nstack)
		sw = ww - mw;
	else if (ws->nstack)
		sw = (ww - mw) * ws->ssplit;
	if (n - ws->nmaster > ws->nstack)
		ss = 1, ssw = ww - mw - sw;

	for (i = 0, my = sy = ssy = g, c = nexttiled(ws->clients); c; c = nexttiled(c->next), ++i) {
		c->old_x = c->x, c->old_y = c->y, c->old_w = c->w, c->old_h = c->h;
		if (i < ws->nmaster) {
			remaining = MIN(n, ws->nmaster) - i;
			x = g;
			y = &my;
			c->w = mw - g * (5 - ns) / 2;
		} else if (i - ws->nmaster < ws->nstack) {
			remaining = MIN(n - ws->nmaster, ws->nstack) - (i - ws->nmaster);
			x = mw + (g / ns);
			y = &sy;
			c->w = sw - g * (5 - ns - ss) / 2;
		} else {
			remaining = n - i;
			x = mw + sw + (g / ns);
			y = &ssy;
			c->w = ssw - g * (5 - ns) / 2;
		}
		c->x = wx + x;
		c->y = wy + *y;
		c->h = ((wh - *y) / MAX(1, remaining)) - g + c->hoff;
		int bw = !globalcfg[GLB_SMART_BORDER] || n > 1 ? c->bw : 0;
		int minh = MAX(globalcfg[GLB_MIN_WH], c->min_h);
		int available = wh - (*y + c->h + g);
		if (!c->hoff && c->h < minh) {
			popfloat(c);
		} else if (remaining > 1 && (remaining - 1) * (minh + g) > available) {
			c->h += available - ((remaining - 1) * (minh + g));
		} else if (remaining == 1 && *y + c->h != wh - g) {
			if (prev) {
				prev->old_h = prev->h;
				if (prev->h + available < minh) {
					prev->h = MAX(globalcfg[GLB_MIN_WH], prev->min_h);
					c->y = prev->y + prev->h + g;
					c->h = (wh - (2 * g)) - (prev->y + prev->h);
				} else if (c->h <= minh) {
					prev->h -= minh - c->h;
					c->y = prev->y + prev->h + g;
					c->h = minh;
				} else {
					prev->h += available;
					c->y += available;
				}
			} else {
				c->h = available;
			}
		} else if (c->h <= minh) {
			c->h = remaining == 1 ? wh - (2 * g) : minh;
		}
		*y += c->h + g;
		c->w -= (2 * bw);
		c->h -= (2 * bw);
		prev = (remaining == 1 && n - i != 0) ? NULL : c;
	}

	for (c = nexttiled(ws->clients); c; c = nexttiled(c->next)) {
		int bw = !globalcfg[GLB_SMART_BORDER] || n > 1 ? c->bw : 0;
		if (c->h <= MAX(globalcfg[GLB_MIN_WH], c->min_h))
			ret = -1;
		if (applysizehints(c, &c->x, &c->y, &c->w, &c->h, c->bw, 0, 0)
				|| (c->x != c->old_x || c->y != c->old_y || c->w != c->old_w || c->h != c->old_h))
		{
			MOVERESIZE(c->win, c->x, c->y, c->w, c->h, bw);
			drawborder(c, c == selws->sel);
			sendconfigure(c);
		}
	}

	xcb_aux_sync(con);
	return ret;
}
