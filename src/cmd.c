/* dk - /dəˈkā/ window manager
 *
 * see license file for copyright and license details
 * vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
 */

/*
 * various functions to handle command parsing and actions
 *
 * all cmd* functions should:
 *
 * return -1 on error
 * return number of arguments parsed on success (including 0)
 *
 * caller is responsible for the argument counter increment
 * which should be passed back up the call stack to parsecmd()
 */

#define _XOPEN_SOURCE 700

#include <sys/types.h>
#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <regex.h>
#include <err.h>
#include <errno.h>

#include <xcb/randr.h>
#include <xcb/xcb_util.h>
#include <xcb/xcb_keysyms.h>

#include "dk.h"
#include "cmd.h"
#include "util.h"
#include "strl.h"
#include "parse.h"
#include "event.h"
#include "layout.h"


enum DirOpts {
	DIR_NEXT   = 0,
	DIR_PREV   = 1,
	DIR_LAST   = 2,
	DIR_NEXTNE = 3,
	DIR_PREVNE = 4,
};

static char *opts[] = {
	[DIR_NEXT]   = "next",
	[DIR_PREV]   = "prev",
	[DIR_LAST]   = "last",
	[DIR_NEXTNE] = "nextne",
	[DIR_PREVNE] = "prevne",
	NULL
};


static int adjustisetting(int i, int rel, int *val, int other, int border)
{
	int n;
	int max = setws->mon->wh - setws->padb - setws->padt;

	if (!rel && !(i -= *val))
		return -1;
	n = CLAMP(*val + i, 0, border ? (max / 6) - other : max / globalcfg[GLB_MIN_WH]);
	if (n != *val)
		*val = n;
	return 0;
}

static int adjustwsormon(char **argv)
{
	int opt, nparsed = 0, e = 0;
	int (*fn)(Workspace *) = cmdview;
	Workspace *ws = NULL, *cur = selws;
	Monitor *m = NULL, *cm = cur->mon;

	cmdclient = selws->sel;
	if (*argv) {
		for (unsigned int i = 0; wscmds[i].str; i++)
			if (!strcmp(wscmds[i].str, *argv)) {
				fn = wscmds[i].func;
				argv++;
				nparsed++;
				break;
			}
		if (fn != cmdview && (cmdclient = parseclient(*argv, &e))) {
			cur = cmdclient->ws;
			cm = cur->mon;
			argv++;
			nparsed++;
		} else if (e == -1) {
			respond(cmdresp, "!invalid window id: %s", *argv);
			return e;
		} else {
			cmdclient = selws->sel;
		}
	}
	if (!*argv) {
		respond(cmdresp, "!%s %s", cmdusemon ? "mon" : "ws", enoargs);
		return -1;
	}
	if ((opt = parseopt(*argv, opts)) >= 0) {
		if (opt == DIR_LAST) {
			ws = cmdusemon
				? (lastmon && lastmon->connected ? lastmon->ws : cur)
				: lastws ? lastws : cur;
		} else if (opt == DIR_NEXT && cmdusemon) {
			if (!(m = nextmon(cm->next)))
				m = nextmon(monitors);
			ws = m->ws;
		} else if (opt == DIR_NEXT) {
			ws = cur->next ? cur->next : workspaces;
		} else if (cmdusemon && opt == DIR_PREV) {
			for (m = nextmon(monitors); m && nextmon(m->next)
					&& nextmon(m->next) != cm; m = nextmon(m->next))
				;
			ws = m ? m->ws : selws;
		} else if (opt == DIR_PREV) {
			FIND_PREV(ws, cur, workspaces);
		} else {
			int r = 0;
			Workspace *save = cur;
			while (!ws && r < globalcfg[GLB_WS_NUM]) {
				if (opt == DIR_NEXTNE) {
					if (cmdusemon) {
						if (!(m = nextmon(cm)))
							m = nextmon(monitors);
						ws = m->ws;
					} else
						ws = cur->next ? cur->next : workspaces;
				} else if (cmdusemon) {
					for (m = nextmon(monitors); m && nextmon(m->next)
							&& nextmon(m->next) != cm; m = nextmon(m->next))
						;
					ws = m ? m->ws : selws;
				} else {
					FIND_PREV(ws, cur, workspaces);
				}
				cur = ws;
				cm = ws->mon;
				if (!ws->clients && ws != save)
					ws = NULL;
				r++;
			}
		}
	} else {
		ws = parsewsormon(*argv, cmdusemon);
	}
	if (ws) {
		nparsed++;
		if (ws != selws || selws->mon != ws->mon)
			fn(ws);
	} else {
		respond(cmdresp, "!invalid value for %s: %s", cmdusemon ? "mon" : "ws", *argv);
		return -1;
	}
	return nparsed;
}

int cmdborder(char **argv)
{
	Client *c;
	Workspace *ws;
	int i, nparsed = 0, rel, col = 0, first;
	int bw = border[BORD_WIDTH], old = border[BORD_WIDTH], ow = border[BORD_O_WIDTH];

	while (*argv) {
		int outer = 0;
		if (!strcmp(*argv, "w") || !strcmp(*argv, "width") || (outer = !strcmp("ow", *argv)
					|| !strcmp("outer", *argv) || !strcmp("outer_width", *argv)))
		{
			col = 0;
			nparsed++;
			if ((i = parseint(*(++argv), &rel, 1)) == INT_MIN) goto badvalue;
			adjustisetting(i, rel, outer ? &ow : &bw, selws->gappx + (outer ? bw : 0), 1);
		} else if (col || (first = !strcmp(*argv, "colour") || !strcmp(*argv, "color"))) {
			if (!col) {
				col = 1;
				argv++;
				nparsed++;
			}
			if (!strcmp("f", *argv) || !strcmp("focus", *argv)) {
				if (parsecolour(*(++argv), &border[BORD_FOCUS]) < 0) goto badvalue;
			} else if (!strcmp("u", *argv) || !strcmp("urgent", *argv)) {
				if (parsecolour(*(++argv), &border[BORD_URGENT]) < 0) goto badvalue;
			} else if (!strcmp("r", *argv) || !strcmp("unfocus", *argv)) {
				if (parsecolour(*(++argv), &border[BORD_UNFOCUS]) < 0) goto badvalue;
			} else if (!strcmp("of", *argv) || !strcmp("outer_focus", *argv)) {
				if (parsecolour(*(++argv), &border[BORD_O_FOCUS]) < 0) goto badvalue;
			} else if (!strcmp("ou", *argv) || !strcmp("outer_urgent", *argv)) {
				if (parsecolour(*(++argv), &border[BORD_O_URGENT]) < 0) goto badvalue;
			} else if (!strcmp("or", *argv) || !strcmp("outer_unfocus", *argv)) {
				if (parsecolour(*(++argv), &border[BORD_O_UNFOCUS]) < 0) goto badvalue;
			} else if (first) {
				goto badvalue;
			} else {
				col = first = 0;
				continue;
			}
			first = 0;
			nparsed++;
		} else {
			break;
badvalue:
			respond(cmdresp, "!set border: invalid %s value: %s", *(argv - 1), *argv);
			return -1;
		}
		argv++;
		nparsed++;
	}
	if (bw - ow < 1 && (unsigned int)ow != border[BORD_O_WIDTH])
		respond(cmdresp, "!border outer exceeds limit: %d - maximum: %d", ow, bw - 1);
	else if (bw - ow > 0)
		border[BORD_O_WIDTH] = ow;
	border[BORD_WIDTH] = bw;
	FOR_CLIENTS(c, ws) {
		if (!(c->state & STATE_NOBORDER)) {
			if (c->bw == old)
				c->bw = bw;
			drawborder(c, c == selws->sel);
		}
	}
	return nparsed;
}

int cmdcycle(char **argv)
{
	Client *c = cmdclient, *first;

	if (FLOATING(c) || FULLSCREEN(c)) {
		respond(cmdresp, "!unable to cycle floating or fullscreen windows");
		return -1;
	}
	if (c == (first = nexttiled(selws->clients)) && !nexttiled(c->next))
		return 0;
	if (!(c = nexttiled(selws->sel->next)))
		c = first;
	focus(first);
	movestack(-1);
	focus(c);
	(void)(argv);
	return 0;
}

int cmdfakefull(char **argv)
{
	Client *c = cmdclient;

	if ((c->state ^= STATE_FAKEFULL) & STATE_FULLSCREEN) {
		if (c->w != c->ws->mon->w || c->h != c->ws->mon->h)
			c->bw = c->old_bw;
		if (!(c->state & STATE_FAKEFULL))
			resize(c, c->ws->mon->x, c->ws->mon->y, c->ws->mon->w, c->ws->mon->h, c->bw);
		needsrefresh = 1;
	}
	(void)(argv);
	return 0;
}

int cmdfloat(char **argv)
{
	int nparsed = 0;
	Client *c = cmdclient;

	if (!c->ws->layout->func) return nparsed;
	if (argv && *argv && !strcmp(*argv, "all")) {
		nparsed++;
		FOR_EACH(c, cmdclient->ws->clients) {
			cmdclient = c;
			if (FLOATING(c) || c->state & STATE_WASFLOATING) {
				if (FLOATING(c))
					c->state |= STATE_WASFLOATING;
				else
					c->state &= ~STATE_WASFLOATING;
				cmdfloat(NULL);
			}
		}
		return nparsed;
	}
	if (FULLSCREEN(c) || c->state & (STATE_STICKY | STATE_FIXED)) {
		respond(cmdresp, "!unable to float fullscreen, sticky, or fixed windows");
	} else {
		if ((c->state ^= STATE_FLOATING) & STATE_FLOATING) {
			if (c->old_x + c->old_y == c->ws->mon->wx + c->ws->mon->wy)
				quadrant(c, &c->old_x, &c->old_y, &c->old_w, &c->old_h);
			resizehint(c, c->old_x, c->old_y, c->old_w, c->old_h, c->bw, 0, 0);
		} else {
			c->old_x = c->x, c->old_y = c->y, c->old_w = c->w, c->old_h = c->h;
		}
		needsrefresh = 1;
	}
	return nparsed;
}

int cmdfocus(char **argv)
{
	int i = 0, nparsed = 0, opt;
	Client *c = cmdclient;

	if (FULLSCREEN(c) || !c->ws->clients->next) return nparsed;
	if (c != selws->sel) {
		focus(c);
		return nparsed;
	}
	if ((opt = parseopt(*argv, opts)) < 0 && (i = parseint(*argv, NULL, 0)) == INT_MIN) {
		respond(cmdresp, "!%s win focus: %s", ebadarg, *argv);
		return -1;
	}
	nparsed++;
	if (opt == DIR_LAST) {
		focus(c->snext);
	} else {
		int direction = opt == -1 ? i : opt == DIR_NEXT ? 1 : -1;
		while (direction) {
			if (direction > 0) {
				c = selws->sel->next ? selws->sel->next : selws->clients;
				direction--;
			} else {
				FIND_PREV(c, selws->sel, selws->clients);
				direction++;
			}
			if (c) {
				focus(c);
				restack(c->ws);
			}
			xcb_aux_sync(con);
			ignore(XCB_ENTER_NOTIFY);
		}
	}
	return nparsed;
}

int cmdfollow(Workspace *ws)
{
	if (ws && cmdclient && ws != cmdclient->ws) {
		cmdsend(ws);
		cmdview(ws);
	}
	return 0;
}

int cmdfull(char **argv)
{
	setfullscreen(cmdclient, !(cmdclient->state & STATE_FULLSCREEN));
	(void)(argv);
	return 0;
}

int cmdgappx(char **argv)
{
	int i, ng, rel, nparsed = 0;

	if (!strcmp(*argv, "width")) {
		argv++;
		nparsed++;
	}
	ng = setws->gappx;
	if (!*argv) {
		respond(cmdresp, "!gap %s", enoargs);
		return -1;
	} else if ((i = parseint(*argv, &rel, 1)) == INT_MIN) {
		respond(cmdresp, "!invalid value for gap: %s", *argv);
		return -1;
	} else {
		nparsed++;
		adjustisetting(i, rel, &ng, border[BORD_WIDTH], 1);
		if (ng != setws->gappx)
			setws->gappx = ng;
	}
	return nparsed;
}

int cmdkill(char **argv)
{
	if (!cmdclient) return 0;
	if (!sendwmproto(cmdclient, WM_DELETE)) {
		xcb_grab_server(con);
		xcb_set_close_down_mode(con, XCB_CLOSE_DOWN_DESTROY_ALL);
		xcb_kill_client(con, cmdclient->win);
		xcb_aux_sync(con);
		xcb_ungrab_server(con);
	}
	xcb_aux_sync(con);
	ignore(XCB_ENTER_NOTIFY);
	(void)(argv);
	return 0;
}

int cmdlayout(char **argv)
{
	for (unsigned int i = 0; layouts[i].name; i++)
		if (!strcmp(layouts[i].name, *argv)) {
			if (&layouts[i] != setws->layout)
				setws->layout = &layouts[i];
			return 1;
		}
	respond(cmdresp, "!invalid layout name: %s", *argv);
	return -1;
}

int cmdmon(char **argv)
{
	int nparsed = 0;
	if (monitors && nextmon(monitors)) {
		cmdusemon = 1;
		nparsed = adjustwsormon(argv);
		cmdusemon = 0;
	}
	return nparsed;
}

int cmdmors(char **argv)
{
	int i, rel = 1;

	if ((i = parseint(*argv, &rel, 1)) == INT_MIN || adjustisetting(i, rel,
				!strcmp("stack", *(argv - 1)) ? &setws->nstack : &setws->nmaster, 0, 0) == -1)
		return -1;
	return 1;
}

int cmdmouse(char **argv)
{
	int arg, nparsed = 0;

	while (*argv) {
		if (!strcmp("mod", *argv)) {
			argv++;
			nparsed++;
			if (!strcmp("alt", *argv) || !strcmp("mod1", *argv))
				mousemod = XCB_MOD_MASK_1;
			else if (!strcmp("super", *argv) || !strcmp("mod4", *argv))
				mousemod = XCB_MOD_MASK_4;
			else if (!strcmp("ctrl", *argv) || !strcmp("control", *argv))
				mousemod = XCB_MOD_MASK_CONTROL;
			else
				goto badvalue;
		} else if ((arg = !strcmp("move", *argv)) || !strcmp("resize", *argv)) {
			argv++;
			nparsed++;
			xcb_button_t *btn = arg ? &mousemove : &mouseresize;
			if (!strcmp("button1", *argv))
				*btn = XCB_BUTTON_INDEX_1;
			else if (!strcmp("button2", *argv))
				*btn = XCB_BUTTON_INDEX_2;
			else if (!strcmp("button3", *argv))
				*btn = XCB_BUTTON_INDEX_3;
			else
				goto badvalue;
		} else {
			break;
badvalue:
			respond(cmdresp, "!set mouse: invalid value for %s: %s", *(argv - 1), *argv);
			return -1;
		}
		argv++;
		nparsed++;
	}
	if (selws->sel)
		grabbuttons(selws->sel, 1);
	return nparsed;
}

int cmdpad(char **argv)
{
	int i, rel, nparsed = 0;

#define PAD(v, o)                                                                  \
	nparsed++;                                                                     \
	if ((i = parseintclamp(*(++argv), &rel, v * -1, o)) == INT_MIN) goto badvalue; \
	v = CLAMP(rel ? v + i : i, 0, o);                                              \
	needsrefresh = 1

	while (*argv) {
		if (!strcmp("l", *argv) || !strcmp("left", *argv)) {
			PAD(setws->padl, setws->mon->w / 3);
		} else if (!strcmp("r", *argv) || !strcmp("right", *argv)) {
			PAD(setws->padr, setws->mon->w / 3);
		} else if (!strcmp("t", *argv) || !strcmp("top", *argv)) {
			PAD(setws->padt, setws->mon->h / 3);
		} else if (!strcmp("b", *argv) || !strcmp("bottom", *argv)) {
			PAD(setws->padb, setws->mon->h / 3);
		} else {
			break;
badvalue:
			respond(cmdresp, "!set pad: invalid value for %s: %s", *(argv - 1), *argv);
			return -1;
		}
		argv++;
		nparsed++;
	}
	needsrefresh = 1;
	return nparsed;
#undef PAD
}

int cmdexit(char **argv)
{
	running = 0;
	(void)(argv);
	return 0;
}

int cmdreload(char **argv)
{
	execcfg();
	(void)(argv);
	return 0;
}

int cmdresize(char **argv)
{
	Client *c = cmdclient, *t;
	Workspace *ws = c->ws;
	float f, *sf;
	int i, nparsed = 0;
	int xgrav = GRAV_NONE, ygrav = GRAV_NONE;
	int x = INT_MIN, y = INT_MIN, w = INT_MIN, h = INT_MIN, bw = INT_MIN;
	int ohoff, relx = 0, rely = 0, relw = 0, relh = 0, relbw = 0;

#define ARG(val, rel, allowzero)						                       \
	nparsed++;                                                                 \
	if ((val = parseint(*(++argv), rel, allowzero)) == INT_MIN) goto badvalue; \

	if (FULLSCREEN(c)) {
		respond(cmdresp, "!unable to resize fullscreen windows");
		return -1;
	}
	while (*argv) {
		if (!strcmp("x", *argv)) {
			nparsed++;
			if (!parsegeom(*(++argv), 'x', &x, &relx, &xgrav)) goto badvalue;
		} else if (!strcmp("y", *argv)) {
			nparsed++;
			if (!parsegeom(*(++argv), 'y', &y, &rely, &ygrav)) goto badvalue;
		} else if (!strcmp("w", *argv) || !strcmp("width", *argv)) {
			ARG(w, &relw, 0);
		} else if (!strcmp("h", *argv) || !strcmp("height", *argv)) {
			ARG(h, &relh, 0);
		} else if (!strcmp("bw", *argv) || !strcmp("border_width", *argv)) {
			ARG(bw, &relbw, 1);
		} else {
			break;
badvalue:
			respond(cmdresp, "!win resize: invalid value for %s: %s", *(argv - 1), *argv);
			return -1;
		}
		argv++;
		nparsed++;
	}

	if (bw != INT_MIN) {
		c->bw = CLAMP(relbw ? c->bw + bw : bw, 0, ws->mon->wh / 6);
		if (c->bw == 0) c->state |= STATE_NOBORDER;
		drawborder(c, c == selws->sel);
	}

	if (!FLOATING(c) && y != INT_MIN)
		movestack(y > 0 || ygrav == GRAV_BOTTOM ? 1 : -1);

	if (!ws->layout->implements_resize) {
		if (x != INT_MIN || w != INT_MIN || h != INT_MIN)
			respond(cmdresp, "!unable to resize windows in %s layout", ws->layout->name);
		goto end;
	}

	if (FLOATING(c)) {
		x = x == INT_MIN || xgrav != GRAV_NONE ? c->x : (relx ? c->x + x : x);
		y = y == INT_MIN || ygrav != GRAV_NONE ? c->y : (rely ? c->y + y : y);
		w = w == INT_MIN ? c->w : (relw ? c->w + w : w);
		h = h == INT_MIN ? c->h : (relh ? c->h + h : h);
		resizehint(c, x, y, w, h, c->bw, 1, 0);
		gravitate(c, xgrav, ygrav, 1);
	} else if (ws->layout->func == tile) {
		if (w != INT_MIN) {
			for (i = 0, t = nexttiled(ws->clients); t && t != c; t = nexttiled(t->next), i++)
				;
			sf = (ws->nmaster && i < ws->nmaster + ws->nstack) ? &ws->msplit : &ws->ssplit;
			f = relw ? ((ws->mon->ww * *sf) + w) / ws->mon->ww : w / ws->mon->ww;
			if (f < 0.05 || f > 0.95) {
				respond(cmdresp, "!width exceeded limit: %f", ws->mon->ww * f);
			} else {
				*sf = f;
				if (h == INT_MIN) ws->layout->func(ws);
			}
		}
		if (h != INT_MIN) {
			ohoff = c->hoff;
			c->hoff = relh ? c->hoff + h : h;
			if (ws->layout->func(ws) < 0) {
				respond(cmdresp, "!height offset exceeded limit: %d", c->hoff);
				c->hoff = ohoff;
			}
		}
	} else if (w != INT_MIN || (h != INT_MIN && ws->layout->invert_split_direction)) {
		if (w != INT_MIN)
			f = relw ? ((ws->mon->ww * ws->msplit) + w) / ws->mon->ww : w / ws->mon->ww;
		else
			f = relh ? ((ws->mon->wh * ws->msplit) + h) / ws->mon->wh : h / ws->mon->wh;
		if (f < 0.05 || f > 0.95) {
			respond(cmdresp, "!%s exceeded limit: %f", w != INT_MIN ? "width" : "height", ws->mon->ww * f);
		} else {
			ws->msplit = f;
			ws->layout->func(ws);
		}
	}
end:
	xcb_aux_sync(con);
	ignore(XCB_ENTER_NOTIFY);
	return nparsed;
#undef ARG
}

int cmdrestart(char **argv)
{
	running = 0, restart = 1;
	(void)(argv);
	return 0;
}

int cmdrule(char **argv)
{
	Client *c;
	Workspace *ws;
	Rule *pr, *nr = NULL;
	int j, nparsed = 0, match;
	unsigned int i, delete = 0, apply = 0;
	Rule r = {
		.x = -1, .y = -1, .w = -1, .h = -1, .ws = -1, .bw = -1,
		.focus = 0, .state = STATE_NONE, .xgrav = GRAV_NONE, .ygrav = GRAV_NONE,
		.cb = NULL, .mon = NULL, .inst = NULL, .class = NULL, .title = NULL,
	};

#define ARG(val)                                                      \
	nparsed++;                                                        \
	if ((j = parseint(*(++argv), NULL, 0)) == INT_MIN) goto badvalue; \
	val = j

	while (*argv) {
		if (!strcmp(*argv, "class")) {
			nparsed++;
			r.class = *(++argv);
		} else if (!strcmp(*argv, "instance")) {
			nparsed++;
			r.inst = *(++argv);
		} else if (!strcmp(*argv, "title")) {
			nparsed++;
			r.title = *(++argv);
		} else if (!strcmp(*argv, "mon")) {
			nparsed++;
			r.mon = *(++argv);
		} else if (!strcmp(*argv, "ws")) {
			nparsed++;
			if ((r.ws = parseintclamp(*(++argv), NULL, 1, globalcfg[GLB_WS_NUM])) == INT_MIN) {
				r.ws = -1;
				match = 0;
				FOR_EACH(ws, workspaces)
					if ((match = !strcmp(ws->name, *argv))) {
						r.ws = ws->num;
						break;
					}
				if (!match) goto badvalue;
			}
		} else if (!strcmp(*argv, "callback")) {
			argv++;
			nparsed++;
			match = 0;
			for (i = 0; callbacks[i].name; i++)
				if ((match = !strcmp(callbacks[i].name, *argv))) {
					r.cb = &callbacks[i];
					break;
				}
			if (!match) goto badvalue;
		} else if (!strcmp(*argv, "x")) {
			nparsed++;
			if (!parsegeom(*(++argv), 'y', &r.y, NULL, &r.ygrav)) goto badvalue;
		} else if (!strcmp(*argv, "y")) {
			nparsed++;
			if (!parsegeom(*(++argv), 'y', &r.y, NULL, &r.ygrav)) goto badvalue;
		} else if (!strcmp("w", *argv) || !strcmp("width", *argv)) {
			ARG(r.w);
		} else if (!strcmp("h", *argv) || !strcmp("height", *argv)) {
			ARG(r.h);
		} else if (!strcmp("bw", *argv) || !strcmp("border_width", *argv)) {
			nparsed++;
			if ((j = parseintclamp(*(++argv), NULL, 0, scr_h / 6)) == INT_MIN) goto badvalue;
			if ((r.bw = j) == 0 && border[BORD_WIDTH])
				r.state |= STATE_NOBORDER;
		} else if (!strcmp(*argv, "float")) {
			nparsed++;
			if ((j = parsebool(*(++argv))) < 0) goto badvalue;
			r.state |= j ? STATE_FLOATING : STATE_NONE;
		} else if (!strcmp(*argv, "stick")) {
			nparsed++;
			if ((j = parsebool(*(++argv))) < 0) goto badvalue;
			r.state |= j ? STATE_STICKY | STATE_FLOATING : STATE_NONE;
		} else if (!strcmp(*argv, "focus")) {
			nparsed++;
			if ((j = parsebool(*(++argv))) < 0) goto badvalue;
			r.focus = j;
		} else if (!strcmp("apply", *argv)) {
			apply = 1;
			if (!strcmp("*", *(argv + 1))) {
				nparsed += 2;
				goto applyall;
			}
		} else if (!strcmp("remove", *argv)) {
			delete = 1;
			if (!strcmp("*", *(argv + 1))) {
				nparsed += 2;
				while (rules) freerule(rules);
				return nparsed;
			}
		} else {
			break;
badvalue:
			respond(cmdresp, "!rule: invalid value for %s: %s", *(argv - 1), *argv);
			return -1;
		}
		argv++;
		nparsed++;
	}

	if ((r.class || r.inst || r.title) && (r.ws != -1 || r.mon || r.focus || r.cb
				|| r.state != STATE_NONE || r.x != -1 || r.y != -1 || r.w != -1
				|| r.h != -1 || r.bw != -1 || r.xgrav != GRAV_NONE || r.ygrav != GRAV_NONE))
	{
#define M(a, b) (a == NULL || (b && !strcmp(a, b)))
		FOR_EACH(pr, rules) {
			if (M(r.class, pr->class) && M(r.inst, pr->inst) && M(r.title, pr->title)) {
				freerule(pr);
				break;
			}
		}

		if (!delete) {
			if ((nr = initrule(&r)) && apply) {
applyall:
				FOR_CLIENTS(c, ws) {
					clientrule(c, nr, 0);
					if (c->cb)
						c->cb->func(c, 0);
				}
			}
		}
	}
	return nparsed;
#undef ARG
#undef M
}

int cmdsend(Workspace *ws)
{
	Client *c = cmdclient;

	if (ws && c && ws != c->ws) {
		Monitor *old = c->ws->mon;
		unfocus(c, 1);
		setworkspace(c, ws->num, c != c->ws->sel);
		if (ws->mon != old && ws->mon->ws == ws)
			relocate(c, ws->mon, old);
		needsrefresh = 1;
	}
	return 0;
}

int cmdset(char **argv)
{
	Workspace *ws = NULL;
	unsigned int j;
	int i, nparsed = 0, names = 0, set = 0;

#define BOOL(val)                                      \
	nparsed++;                                         \
	if ((i = parsebool(*(++argv))) < 0) goto badvalue; \
	globalcfg[GLB_##val] = i

	setws = selws;
	if (!*argv) {
		respond(cmdresp, "!set %s", enoargs);
		return -1;
	}
	while (*argv) {
		if (!strcmp("ws", *argv)) {
			argv++;
			nparsed++;
			if (!strcmp("_", *argv)) {
				if ((i = cmdws_(argv + 1)) == -1) return -1;
				argv += i + 1;
				nparsed += i + 1;
				continue;
			} else if (!(ws = parsewsormon(*argv, 0))) {
				goto badvalue;
			}
			setws = ws;
			set = 1;
		} else if (!strcmp("mon", *argv)) {
			argv++;
			nparsed++;
			if (!globalcfg[GLB_WS_STATIC]) {
				respond(cmdresp, "!unable to set monitor with dynamic workspaces enabled");
				break;
			} else if (!set) {
				respond(cmdresp, "!workspace index or name is required to set the monitor");
				break;
			} else if (!(ws = parsewsormon(*argv, 1))) {
				respond(cmdresp, "!monitor index or name is required to assign workspace");
				break;
			}
			assignws(setws, ws->mon);
		} else if (!strcmp("numws", *argv)) {
			nparsed++;
			if ((i = parseintclamp(*(++argv), NULL, 1, 99)) == INT_MIN)
				goto badvalue;
			if (i > globalcfg[GLB_WS_NUM])
				updworkspaces(i);
		} else if (!strcmp("name", *argv)) {
			nparsed++;
			if (!*(++argv)) goto badvalue;
			strlcpy(setws->name, *argv, sizeof(setws->name));
			names = 1;
		} else if (!strcmp("tile_hints", *argv)) {
			BOOL(TILE_HINTS);
		} else if (!strcmp("tile_tohead", *argv)) {
			BOOL(TILE_TOHEAD);
		} else if (!strcmp("smart_gap", *argv)) {
			BOOL(SMART_GAP);
		} else if (!strcmp("smart_border", *argv)) {
			BOOL(SMART_BORDER);
		} else if (!strcmp("focus_urgent", *argv)) {
			BOOL(FOCUS_URGENT);
		} else if (!strcmp("focus_mouse", *argv)) {
			BOOL(FOCUS_MOUSE);
		} else if (!strcmp("focus_open", *argv)) {
			BOOL(FOCUS_OPEN);
		} else if (!strcmp("static_ws", *argv)) {
			BOOL(WS_STATIC);
		} else if (!strcmp("win_minxy", *argv)) {
			nparsed++;
			if ((i = parseintclamp(*(++argv), NULL, 10, 1000)) == INT_MIN) goto badvalue;
			globalcfg[GLB_MIN_XY] = i;
		} else if (!strcmp("win_minwh", *argv)) {
			nparsed++;
			if ((i = parseintclamp(*(++argv), NULL, 10, 1000)) == INT_MIN) goto badvalue;
			globalcfg[GLB_MIN_WH] = i;
		} else {
			int match = 0;
			for (j = 0; setcmds[j].str; j++)
				if ((match = !strcmp(setcmds[j].str, *argv))) {
					if ((i = setcmds[j].func(argv + 1)) == -1)
						return -1;
					argv += i + 1;
					nparsed += i + 1;
					break;
				}
			if (match)
				continue;
			break;
badvalue:
			respond(cmdresp, "!set: invalid value for %s: %s", *(argv - 1), *argv);
			return -1;
		}
		argv++;
		nparsed++;
	}
#undef BOOL

	needsrefresh = 1;
	if (names)
		setnetwsnames();
	return nparsed;
}

int cmdsplit(char **argv)
{
	int rel = 1;
	float f = 0.0;

	if ((f = parsefloat(*argv, &rel)) != -1.0) {
		float *ff = !strcmp("msplit", *(argv - 1)) ? &setws->msplit : &setws->ssplit;
		if (setws->layout->func && (!rel && !(f -= *ff))) {
			float nf;
			if ((nf = CLAMP(f < 1.0 ? f + *ff : f - 1.0, 0.05, 0.95)) != *ff)
				*ff = nf;
		}
		return 1;
	}
	return -1;
}

int cmdstatus(char **argv)
{
	Status *s;
	char *path = NULL;
	FILE *file = cmdresp;
	int i, num = -1, nparsed = 0;
	unsigned int type = TYPE_WS;

	while (*argv) {
		if (!strcmp("type", *argv)) {
			argv++;
			nparsed++;
			if (!strcmp("ws", *argv))
				type = TYPE_WS;
			else if (!strcmp("full", *argv))
				type = TYPE_FULL;
			else
				goto badvalue;
		} else if (!strcmp("num", *argv)) {
			nparsed++;
			if ((i = parseintclamp(*(++argv), NULL, -1, INT_MAX)) == INT_MIN)
				goto badvalue;
			num = i;
		} else if (!strcmp("file", *argv)) {
			argv++;
			nparsed++;
			if (*argv) {
				size_t len = strlen(*argv) + 1;
				path = ecalloc(1, len);
				strlcpy(path, *argv, len);
			} else {
				goto badvalue;
			}
		} else {
			break;
badvalue:
			respond(cmdresp, "!status: invalid value for %s: %s", *(argv - 1), *argv);
			return -1;
		}
		argv++;
		nparsed++;
	}

	if (path && path[0] && !(file = fopen(path, "w")))
		respond(cmdresp, "!unable to open file in write mode: %s: %s", path, strerror(errno));
	if (file) {
		status_usingcmdresp = file == cmdresp;
		printstatus((s = initstatus(file, path, num, type)));
		if (!s->num) freestatus(s);
	} else {
		respond(cmdresp, "!unable to create status: %s", path ? path : "stdout");
	}
	free(path);
	return nparsed;
}

int cmdstick(char **argv)
{
	Client *c = cmdclient;
	unsigned int all = 0xffffffff;

	if (FULLSCREEN(c)) {
		respond(cmdresp, "!unable to change sticky state of fullscreen windows");
		return 0;
	}
	if ((c->state ^= STATE_STICKY) & STATE_STICKY) {
		c->state &= ~STATE_STICKY;
		PROP(REPLACE, c->win, netatom[NET_WM_DESK], XCB_ATOM_CARDINAL, 32, 1, &c->ws->num);
	} else {
		cmdfloat(NULL);
		c->state |= STATE_STICKY | STATE_FLOATING;
		PROP(REPLACE, c->win, netatom[NET_WM_DESK], XCB_ATOM_CARDINAL, 32, 1, &all);
	}
	(void)(argv);
	return 0;
}

int cmdswap(char **argv)
{
	static Client *last = NULL;
	Client *c = cmdclient, *old, *cur = NULL, *prev = NULL;

	if (FLOATING(c) || (c->state & STATE_FULLSCREEN
				&& c->w == c->ws->mon->w && c->h == c->ws->mon->h))
	{
		respond(cmdresp, "!unable to swap floating or fullscreen windows");
		return 0;
	}
	if (c == nexttiled(c->ws->clients)) {
		FIND_PREV(cur, last, c->ws->clients);
		if (cur != c->ws->clients)
			prev = nexttiled(cur->next);
		if (!prev || prev != last) {
			last = NULL;
			if (!(c = nexttiled(c->next))) return 0;
		} else {
			c = prev;
		}
	}
	if (c != (old = nexttiled(c->ws->clients)) && !cur)
		FIND_PREV(cur, c, c->ws->clients);
	detach(c, 1);
	if (c != old && cur && cur != c->ws->clients) {
		last = old;
		if (old && cur != old) {
			detach(old, 0);
			ATTACH(old, cur->next);
		}
	}
	needsrefresh = 1;
	(void)(argv);
	return 0;
}

int cmdview(Workspace *ws)
{
	if (ws) {
		changews(ws, globalcfg[GLB_WS_STATIC] ? 0 : !cmdusemon,
				cmdusemon || (globalcfg[GLB_WS_STATIC] && selws->mon != ws->mon));
		needsrefresh = 1;
	}
	return 0;
}

int cmdwin(char **argv)
{
	Client *c;
	int e = 0, nparsed = 0;

	cmdclient = selws->sel;
	while (*argv) {
		if ((c = parseclient(*argv, &e))) {
			cmdclient = c;
		} else if (e == -1) {
			respond(cmdresp, "!invalid window id: %s", *argv);
			return -1;
		} else {
			int match = 0;
			for (unsigned int ui = 0; wincmds[ui].str; ui++)
				if ((match = !strcmp(wincmds[ui].str, *argv))) {
					if ((e = wincmds[ui].func(argv + 1)) == -1) return -1;
					nparsed += e;
					argv += e;
					break;
				}
			if (!match) break;
		}
		argv++;
		nparsed++;
	}
	return nparsed;
}

int cmdws(char **argv)
{
	int nparsed = 0;

	if (workspaces && workspaces->next)
		nparsed = adjustwsormon(argv);
	return nparsed;
}

int cmdws_(char **argv)
{
	float f;
	unsigned int i;
	int j, nparsed = 0, pad = 0, first, apply = 0;

	while (*argv) {
		int *s;
		float *ff;
		if (!strcmp(*argv, "apply")) {
			apply = 1;
		} else if (!strcmp(*argv, "layout")) {
			argv++;
			nparsed++;
			pad = 0;
			int match = 0;
			for (i = 0; layouts[i].name; i++)
				if ((match = !strcmp(layouts[i].name, *argv))) {
					wsdef.layout = &layouts[i];
					break;
				}
			if (!match) goto badvalue;
		} else if ((s = !strcmp(*argv, "master") ? &wsdef.nmaster
					: !strcmp(*argv, "stack") ? &wsdef.nstack : NULL))
		{
			pad = 0;
			nparsed++;
			if ((j = parseintclamp(*(++argv), NULL, 0, INT_MAX - 1)) == INT_MIN) goto badvalue;
			*s = j;
		} else if ((ff = !strcmp(*argv, "msplit") ? &wsdef.msplit
					: !strcmp(*argv, "ssplit") ? &wsdef.ssplit : NULL))
		{
			pad = 0;
			nparsed++;
			if ((f = parsefloat(*(++argv), NULL)) == -1.0) goto badvalue;
			*ff = f;
		} else if (!strcmp(*argv, "gap")) {
			pad = 0;
			nparsed++;
			if ((j = parseintclamp(*(++argv), NULL, 0, scr_h / 6)) == INT_MIN) goto badvalue;
			wsdef.gappx = j;
		} else if (pad || (first = !strcmp(*argv, "pad"))) {
			if (!pad) {
				pad = 1;
				argv++;
				nparsed++;
			}
			if (!strcmp("l", *argv) || !strcmp("left", *argv)) {
				if ((j = parseintclamp(*(++argv), NULL, 0, scr_h / 3)) == INT_MIN) goto badvalue;
				wsdef.padl = j;
			} else if (!strcmp("r", *argv) || !strcmp("right", *argv)) {
				if ((j = parseintclamp(*(++argv), NULL, 0, scr_h / 3)) == INT_MIN) goto badvalue;
				wsdef.padr = j;
			} else if (!strcmp("t", *argv) || !strcmp("top", *argv)) {
				if ((j = parseintclamp(*(++argv), NULL, 0, scr_h / 3)) == INT_MIN) goto badvalue;
				wsdef.padt = j;
			} else if (!strcmp("b", *argv) || !strcmp("bottom", *argv)) {
				if ((j = parseintclamp(*(++argv), NULL, 0, scr_h / 3)) == INT_MIN) goto badvalue;
				wsdef.padb = j;
			} else if (first) {
				goto badvalue;
			} else {
				pad = first = 0;
				continue;
			}
			nparsed++;
		} else {
			break;
badvalue:
			respond(cmdresp, "!set ws=_: invalid value for %s: %s", *(argv - 1), *argv);
			return -1;
		}
		argv++;
		nparsed++;
	}

	if (apply) {
		Workspace *ws;
		FOR_EACH(ws, workspaces) {
			ws->layout = wsdef.layout;
			ws->gappx = wsdef.gappx;
			ws->nmaster = wsdef.nmaster;
			ws->nstack = wsdef.nstack;
			ws->msplit = wsdef.msplit;
			ws->ssplit = wsdef.ssplit;
			ws->padl = wsdef.padl;
			ws->padr = wsdef.padr;
			ws->padt = wsdef.padt;
			ws->padb = wsdef.padb;
		}
	}
	return nparsed;
}
