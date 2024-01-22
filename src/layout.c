/* dk window manager
 *
 * see license file for copyright and license details
 * vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
 */

#ifdef DEBUG
#include <err.h>
#endif

#include <xcb/randr.h>
#include <xcb/xcb_util.h>
#include <xcb/xcb_keysyms.h>

#include "dk.h"
#include "layout.h"

int dwindle(Workspace *ws)
{
	Client *c;
	Monitor *m = ws->mon;
	uint32_t i, n;
	int x, y, w, h, ww, g, f = 0, ret = 1;

	if (!(n = tilecount(ws))) {
		return 1;
	}

	if (globalcfg[GLB_SMART_GAP].val && n == 1) {
		g = 0, ws->smartgap = 1;
	} else {
		g = ws->gappx, ws->smartgap = 0;
	}

	x = m->wx + ws->padl;
	y = m->wy + ws->padt;
	w = m->ww - ws->padl - ws->padr;
	h = m->wh - ws->padt - ws->padb;
	ww = w;

	for (i = 0, c = nexttiled(ws->clients); c; c = nexttiled(c->next), i++) {
		uint32_t ox = x, oy = y;
		int *p = (i % 2) ? &h : &w;
		int b = globalcfg[GLB_SMART_BORDER].val && n == 1 ? 0 : c->bw;
		if (i < n - 1) {
			*p /= 2;
		}
		switch (i % 4) {
			case 0: y += h; break;
			case 1: x += w; break;
			case 2: y += h; break;
			case 3: x += w; break;
		}
		if (!i) {
			w = n > 1 ? (ww * ws->msplit) - (float)g / 2 : ww - g;
			h -= g;
			y = m->wy + ws->padt;
		} else if (i == 1) {
			w = ww - ((ww * ws->msplit) + (float)g / 2);
		}
		if (f || *p - g - (2 * b) < globalcfg[GLB_MIN_WH].val) {
			if (f) {
				popfloat(c);
				ret = -1;
				continue;
			}
			f = 1;
			*p *= 2;
			ret = -1;
			if (i % 2) {
				y = oy;
			} else {
				x = ox;
			}
		}
		resizehint(c, x + g, y + g, w - g - (2 * b), h - g - (2 * b), b, 0, 0);
	}
	return ret;
}

int grid(Workspace *ws)
{
	Client *c;
	Monitor *m = ws->mon;
	int wx, wy, ww, wh;
	int i, n, g, cols, rows, col, row;

	if (!(n = tilecount(ws))) {
		return 1;
	}
	for (cols = 0; cols <= n / 2; cols++) {
		if (cols * cols >= n) {
			break;
		}
	}
	if (n == 5) {
		cols = 2;
	}
	cols = MAX(1, cols);
	rows = n / MAX(1, cols);
	wx = m->wx + ws->padl;
	wy = m->wy + ws->padt;
	ww = m->ww - ws->padl - ws->padr;
	wh = m->wh - ws->padt - ws->padb;

	if (globalcfg[GLB_SMART_GAP].val && n == 1) {
		g = 0, ws->smartgap = 1;
	} else {
		g = ws->gappx, ws->smartgap = 0;
	}

	for (i = col = row = 0, c = nexttiled(ws->clients); c; i++, c = nexttiled(c->next)) {
		if (i / MAX(1, rows) + 1 > cols - n % cols) {
			rows = n / cols + 1;
		}
		int b = globalcfg[GLB_SMART_BORDER].val && n == 1 ? 0 : c->bw;
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
	return 1;
}

int ltile(Workspace *ws)
{
	Client *c;
	Monitor *m = ws->mon;
	int i, g, n, x, *y, remain, ret = 0, p = -1, pbw = 0;
	int mw, my, sw, sy, ss, ssw, ssy, ns = 1;
	int minh = globalcfg[GLB_MIN_WH].val;

	if (!(n = tilecount(ws))) {
		return 1;
	}
	mw = ss = sw = ssw = 0;
	int geo[n + 1][4];
	int wx = m->wx + ws->padl;
	int wy = m->wy + ws->padt;
	int ww = m->ww - ws->padl - ws->padr;
	int wh = m->wh - ws->padt - ws->padb;

	if (globalcfg[GLB_SMART_GAP].val && n == 1) {
		g = 0, ws->smartgap = 1;
	} else {
		g = ws->gappx, ws->smartgap = 0;
	}

	if (n <= ws->nmaster) {
		mw = ww, ss = 1;
	} else if (ws->nmaster) {
		ns = 2, mw = ww * ws->msplit;
	}
	if (n - ws->nmaster <= ws->nstack) {
		sw = ww - mw;
	} else if (ws->nstack) {
		sw = (ww - mw) * ws->ssplit;
	}
	if (n - ws->nmaster > ws->nstack) {
		ss = 1, ssw = ww - mw - sw;
	}
	if (!ws->nmaster) {
		ss = 0;
	}

	/* We use an array to store client geometries so we can change the size of
	 * the previous client if needed. There's a lot of messy maths to adjust
	 * the size of each window using it's height offset and stack split ratio.
	 */
	for (i = 0, my = sy = ssy = g, c = nexttiled(ws->clients); c; c = nexttiled(c->next), ++i) {
		if (i < ws->nmaster) {
			remain = MIN(n, ws->nmaster) - i;
			x = g;
			y = &my;
			geo[i][2] = mw - g * (5 - ns) / 2;
		} else if (i - ws->nmaster < ws->nstack) {
			remain = MIN(n - ws->nmaster, ws->nstack) - (i - ws->nmaster);
			x = mw + (g / ns);
			y = &sy;
			geo[i][2] =
				(sw - g * (5 - ns - ss) / 2) + (!ws->nmaster && n > ws->nmaster + ws->nstack ? g / 2 : 0);
		} else {
			remain = n - i;
			x = mw + sw + (g / ns) - (!ws->nmaster ? g / 2 : 0);
			y = &ssy;
			geo[i][2] = (ssw - g * (5 - ns) / 2) + (!ws->nmaster ? g / 2 : 0);
		}
		geo[i][0] = wx + x;
		geo[i][1] = wy + *y;
		int bw = !globalcfg[GLB_SMART_BORDER].val || n > 1 ? c->bw : 0;
		if (p == -1 && remain == 1) {
			geo[i][3] = wh - *y - g;
			goto update;
		} else {
			geo[i][3] = ((wh - *y) / MAX(1, remain)) - g + c->hoff;
		}
		int available = wh - (*y + geo[i][3] + g);
		if (!c->hoff && geo[i][3] - (2 * bw) < minh) {
			popfloat(c);
			continue;
		} else if (remain > 1 && (remain - 1) * (minh + g + (2 * bw)) > available) {
			geo[i][3] += available - ((remain - 1) * (minh + g + (2 * bw)));
			ret = -1;
		} else if (remain == 1 && *y + geo[i][3] != wh - g) {
			if (p != -1) {
				if (geo[p][3] + available < minh + (2 * bw)) {
					geo[p][3] = minh + (2 * pbw);
					geo[i][1] = geo[p][1] + geo[p][3] + g + (2 * pbw);
					geo[i][3] = (wh - (2 * g)) - (geo[p][1] + geo[p][3]) - (2 * pbw);
					ret = -1;
				} else if (geo[i][3] <= minh) {
					geo[p][3] -= minh - geo[i][3] + (2 * bw);
					geo[i][1] = geo[p][1] + geo[p][3] + g;
					geo[i][3] = minh + (2 * bw);
					ret = -1;
				} else {
					geo[p][3] += available;
					geo[i][1] += available;
				}
			} else {
				geo[i][3] = available;
			}
		} else if (geo[i][3] - (2 * bw) < minh) {
			geo[i][3] = remain == 1 ? wh - (2 * g) : minh + (2 * bw);
			ret = -1;
		}
update:
		*y += geo[i][3] + g;
		geo[i][2] -= (2 * bw);
		geo[i][3] -= (2 * bw);
		p = (remain == 1 && n - i != 0) ? -1 : i;
		pbw = bw;
	}

	/* Do the actual resizing, if a client goes below the minimum allowed size
	 * we return -1 to signify the layout exceeded it.
	 */
	for (i = 0, c = nexttiled(ws->clients); c; c = nexttiled(c->next), i++) {
		if (geo[i][3] <= globalcfg[GLB_MIN_WH].val) {
			ret = -1;
		}
		resizehint(c, geo[i][0], geo[i][1], geo[i][2], geo[i][3],
				   !globalcfg[GLB_SMART_BORDER].val || n > 1 ? c->bw : 0, 0, 0);
	}
	return ret;
}

int mono(Workspace *ws)
{
	int g;
	Client *c;

	if (!ws->sel) {
		return 1;
	}
	if (globalcfg[GLB_SMART_GAP].val) {
		g = 0, ws->smartgap = 1;
	} else {
		g = ws->gappx, ws->smartgap = 0;
	}

	int b = globalcfg[GLB_SMART_BORDER].val ? 0 : ws->sel->bw;

	for (c = nexttiled(ws->clients); c; c = nexttiled(c->next)) {
		resizehint(c, ws->mon->wx + ws->padl + g, ws->mon->wy + ws->padt + g,
				   ws->mon->ww - ws->padl - ws->padr - (2 * g) - (2 * b),
				   ws->mon->wh - ws->padt - ws->padb - (2 * g) - (2 * b), b, 0, 0);
		if (c != c->ws->sel) { /* hide inactive windows */
			MOVE(c->win, W(c) * -2, c->y);
		}
	}
	return 1;
}

int rtile(Workspace *ws)
{
	Client *c;
	Monitor *m = ws->mon;
	int i, g, n, x, *y, remain, ret = 0, p = -1, pbw = 0;
	int mw, my, sw, sy, ss, ssw, ssy, ns = 1;
	int minh = globalcfg[GLB_MIN_WH].val;

	if (!(n = tilecount(ws))) {
		return 1;
	}
	mw = ss = sw = ssw = 0;
	int geo[n + 1][4];
	int wx = m->wx + ws->padl;
	int wy = m->wy + ws->padt;
	int ww = m->ww - ws->padl - ws->padr;
	int wh = m->wh - ws->padt - ws->padb;

	if (globalcfg[GLB_SMART_GAP].val && n == 1) {
		g = 0, ws->smartgap = 1;
	} else {
		g = ws->gappx, ws->smartgap = 0;
	}

	if (n <= ws->nmaster) {
		mw = ww, ss = 1;
	} else if (ws->nmaster) {
		ns = 2, mw = ww * ws->msplit;
	}
	if (n - ws->nmaster <= ws->nstack) {
		sw = ww - mw;
	} else if (ws->nstack) {
		sw = (ww - mw) * ws->ssplit;
	}
	if (n - ws->nmaster > ws->nstack) {
		ss = 1, ssw = ww - mw - sw;
	}
	if (!ws->nmaster) {
		ss = 0;
	}

	for (i = 0, my = sy = ssy = g, c = nexttiled(ws->clients); c; c = nexttiled(c->next), ++i) {
		if (i < ws->nmaster) {
			remain = MIN(n, ws->nmaster) - i;
			x = sw + ssw + (g / ns);
			y = &my;
			geo[i][2] = mw - g * (5 - ns) / 2;
		} else if (i - ws->nmaster < ws->nstack) {
			remain = MIN(n - ws->nmaster, ws->nstack) - (i - ws->nmaster);
			x = n <= ws->nmaster + ws->nstack
					? g
					: (ssw + g / ns) - (!ws->nmaster && n > ws->nmaster + ws->nstack ? g / 2 : 0);
			y = &sy;
			geo[i][2] =
				(sw - g * (5 - ns - ss) / 2) + (!ws->nmaster && n > ws->nmaster + ws->nstack ? g / 2 : 0);
		} else {
			remain = n - i;
			x = g;
			y = &ssy;
			geo[i][2] = ssw - g * (5 - ns) / 2;
			if (!ws->nmaster) {
				geo[i][2] += g / 2;
			}
		}
		geo[i][0] = wx + x;
		geo[i][1] = wy + *y;
		int bw = !globalcfg[GLB_SMART_BORDER].val || n > 1 ? c->bw : 0;
		if (p == -1 && remain == 1) {
			geo[i][3] = wh - *y - g;
			goto update;
		} else {
			geo[i][3] = ((wh - *y) / MAX(1, remain)) - g + c->hoff;
		}
		int available = wh - (*y + geo[i][3] + g);
		if (!c->hoff && geo[i][3] - (2 * bw) < minh) {
			popfloat(c);
			ret = -1;
			continue;
		} else if (remain > 1 && (remain - 1) * (minh + g + (2 * bw)) > available) {
			geo[i][3] += available - ((remain - 1) * (minh + g + (2 * bw)));
			ret = -1;
		} else if (remain == 1 && *y + geo[i][3] != wh - g) {
			if (p != -1) {
				if (geo[p][3] + available < minh + (2 * bw)) {
					geo[p][3] = minh + (2 * pbw);
					geo[i][1] = geo[p][1] + geo[p][3] + g + (2 * pbw);
					geo[i][3] = (wh - (2 * g)) - (geo[p][1] + geo[p][3]) - (2 * pbw);
					ret = -1;
				} else if (geo[i][3] <= minh) {
					geo[p][3] -= minh - geo[i][3] + (2 * bw);
					geo[i][1] = geo[p][1] + geo[p][3] + g;
					geo[i][3] = minh + (2 * bw);
					ret = -1;
				} else {
					geo[p][3] += available;
					geo[i][1] += available;
				}
			} else {
				geo[i][3] = available;
			}
		} else if (geo[i][3] - (2 * bw) < minh) {
			geo[i][3] = remain == 1 ? wh - (2 * g) : minh + (2 * bw);
			ret = -1;
		}
update:
		*y += geo[i][3] + g;
		geo[i][2] -= (2 * bw);
		geo[i][3] -= (2 * bw);
		p = (remain == 1 && n - i != 0) ? -1 : i;
		pbw = bw;
	}

	for (i = 0, c = nexttiled(ws->clients); c; c = nexttiled(c->next), i++) {
		if (geo[i][3] <= globalcfg[GLB_MIN_WH].val) {
			ret = -1;
		}
		resizehint(c, geo[i][0], geo[i][1], geo[i][2], geo[i][3],
				   !globalcfg[GLB_SMART_BORDER].val || n > 1 ? c->bw : 0, 0, 0);
	}
	return ret;
}

int spiral(Workspace *ws)
{
	Client *c;
	Monitor *m = ws->mon;
	uint32_t i, n;
	int x, y, w, h, ww, g, f = 0, ret = 1;

	if (!(n = tilecount(ws))) {
		return 1;
	}

	if (globalcfg[GLB_SMART_GAP].val && n == 1) {
		g = 0, ws->smartgap = 1;
	} else {
		g = ws->gappx, ws->smartgap = 0;
	}

	x = m->wx + ws->padl;
	y = m->wy + ws->padt;
	w = m->ww - ws->padl - ws->padr;
	h = m->wh - ws->padt - ws->padb;
	ww = w;

	for (i = 0, c = nexttiled(ws->clients); c; c = nexttiled(c->next), i++) {
		uint32_t ox = x, oy = y;
		int *p = (i % 2) ? &h : &w;
		int b = globalcfg[GLB_SMART_BORDER].val && n == 1 ? 0 : c->bw;
		if (i < n - 1) {
			*p /= 2;
			if (i % 4 == 2) {
				x += w;
			} else if (i % 4 == 3) {
				y += h;
			}
		}
		switch (i % 4) {
			case 0: y -= h; break;
			case 1: x += w; break;
			case 2: y += h; break;
			case 3: x -= w; break;
		}
		if (!i) {
			w = n > 1 ? (ww * ws->msplit) - (float)g / 2 : ww - g;
			h -= g;
			y = m->wy + ws->padt;
		} else if (i == 1) {
			w = ww - ((ww * ws->msplit) + (float)g / 2);
		}

		if (f || *p - g - (2 * b) < globalcfg[GLB_MIN_WH].val) {
			if (f) {
				popfloat(c);
				ret = -1;
				continue;
			}
			f = 1;
			*p *= 2;
			ret = -1;
			if (i % 2) {
				y = oy;
			} else {
				x = ox;
			}
		}
		resizehint(c, x + g, y + g, w - (2 * b) - g, h - (2 * b) - g, b, 0, 0);
	}
	return ret;
}
