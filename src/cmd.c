/* dk window manager
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


int adjustisetting(int i, int rel, int *val, int other, int border)
{
	int n;
	int max = setws->mon->wh - setws->padb - setws->padt;

	if (!rel && !(i -= *val)) return -1;
	n = CLAMP(*val + i, 0, border ? (max / 6) - other : max / globalcfg[GLB_MIN_WH].val);
	if (n != *val) {
		*val = n;
		needsrefresh = 1;
	}
	return 0;
}

int adjustwsormon(char **argv)
{
	int opt, nparsed = 0, e = 0;
	int (*fn)(Workspace *) = cmdview;
	Workspace *ws = NULL, *cur = selws;
	Monitor *m = NULL, *cm = cur->mon;

	if (*argv) {
		for (unsigned int i = 0; wscmds[i].str; i++)
			if (!strcmp(wscmds[i].str, *argv)) {
				fn = wscmds[i].func;
				argv++, nparsed++;
				break;
			}
		if (fn != cmdview && (cmdc = parseclient(*argv, &e))) {
			cur = cmdc->ws;
			cm = cur->mon;
			argv++, nparsed++;
		} else if (e == -1) {
			respond(cmdresp, "!invalid window id: %s", *argv);
			return e;
		} else {
			cmdc = selws->sel;
		}
	}
	if (!*argv) {
		respond(cmdresp, "!%s %s", cmdusemon ? "mon" : "ws", enoargs);
		return -1;
	}
	if ((opt = parseopt(*argv, directions)) >= 0) {
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
			while (!ws && r < globalcfg[GLB_WS_NUM].val) {
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
		if ((cmdc && ws != cmdc->ws) || ws != selws || selws->mon != ws->mon) {
			lytchange = fn != cmdsend && ws->layout != selws->layout;
			fn(ws);
			wschange = 1;
		}
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

#define COLOUR(type) if (!(++argv) || parsecolour(*argv, &border[BORD_##type]) < 0) goto badvalue

	while (*argv) {
		int outer = 0;
		if (!strcmp(*argv, "w") || !strcmp(*argv, "width") || (outer = !strcmp("ow", *argv)
					|| !strcmp("outer", *argv) || !strcmp("outer_width", *argv)))
		{
			col = 0, nparsed++, argv++;
			if (!argv || (i = parseint(*argv, &rel, 1)) == INT_MIN) goto badvalue;
			adjustisetting(i, rel, outer ? &ow : &bw, selws->gappx + (outer ? bw : 0), 1);
		} else if (col || (first = !strcmp(*argv, "colour") || !strcmp(*argv, "color"))) {
			if (!col)
				col = 1, argv++, nparsed++;
			if (!strcmp("f", *argv) || !strcmp("focus", *argv)) {
				COLOUR(FOCUS);
			} else if (!strcmp("u", *argv) || !strcmp("urgent", *argv)) {
				COLOUR(URGENT);
			} else if (!strcmp("r", *argv) || !strcmp("unfocus", *argv)) {
				COLOUR(UNFOCUS);
			} else if (!strcmp("of", *argv) || !strcmp("outer_focus", *argv)) {
				COLOUR(O_FOCUS);
			} else if (!strcmp("ou", *argv) || !strcmp("outer_urgent", *argv)) {
				COLOUR(O_URGENT);
			} else if (!strcmp("or", *argv) || !strcmp("outer_unfocus", *argv)) {
				COLOUR(O_UNFOCUS);
			} else if (first) {
				goto badvalue;
			} else {
				col = first = 0;
				continue;
			}
			first = 0, nparsed++;
		} else {
			break;
badvalue:
			respond(cmdresp, "!set border: invalid %s value: %s", *(argv - 1), *argv);
			return -1;
		}
		argv++, nparsed++;
	}
	if (bw - ow < 1 && (unsigned int)ow != border[BORD_O_WIDTH])
		respond(cmdresp, "!border outer exceeds limit: %d - maximum: %d", ow, bw - 1);
	else if (bw - ow > 0)
		border[BORD_O_WIDTH] = ow;
	border[BORD_WIDTH] = bw;
	FOR_CLIENTS(c, ws)
		if (!(c->state & STATE_NOBORDER) && c->bw == old)
			c->bw = bw;
	return nparsed;

#undef COLOUR
}

int cmdcycle(__attribute__((unused)) char **argv)
{
	Client *c = cmdc, *first;

	if (FLOATING(c) || FULLSCREEN(c) || tilecount(c->ws) <= 1) {
		respond(cmdresp, "!unable to cycle floating, fullscreen, or single tiled windows");
		return -1;
	}
	if (c == (first = nexttiled(selws->clients)) && !nexttiled(c->next)) return 0;
	if (!(c = nexttiled(selws->sel->next))) c = first;

	/* TODO: avoid using focus() twice here, possibly just
	 *
	 * cmdc = first;
	 */
	focus(first);
	movestack(-1);
	focus(c);

	return 0;
}

int cmdfakefull(__attribute__((unused)) char **argv)
{
	Client *c = cmdc;

	if ((c->state ^= STATE_FAKEFULL) & STATE_FULLSCREEN) {
		if (c->w != c->ws->mon->w || c->h != c->ws->mon->h)
			c->bw = c->old_bw;
		if (!(c->state & STATE_FAKEFULL))
			resize(c, c->ws->mon->x, c->ws->mon->y, c->ws->mon->w, c->ws->mon->h, c->bw);
		needsrefresh = 1;
	}
	return 0;
}

int cmdfloat(char **argv)
{
	int nparsed = 0;
	Client *c = cmdc;

	if (!c->ws->layout->func) return nparsed;
	if (argv && *argv && !strcmp(*argv, "all")) {
		nparsed++;
		FOR_EACH(c, cmdc->ws->clients) {
			cmdc = c;
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
		respond(cmdresp, "!unable to change floating state of fullscreen, sticky, or fixed windows");
	} else {
		if ((c->state ^= STATE_FLOATING) & STATE_FLOATING) {
			if (c->old_x + c->old_y == c->ws->mon->wx + c->ws->mon->wy
					|| c->old_x + c->old_y == c->ws->mon->x + c->ws->mon->y)
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
	Client *c = cmdc;

	if (FULLSCREEN(c) || !c->ws->clients->next) return nparsed;
	if (c != selws->sel) {
		focus(c);
		if (FLOATING(c)) setstackmode(c->win, XCB_STACK_MODE_ABOVE);
		return nparsed;
	}
	if ((opt = parseopt(*argv, directions)) < 0 && (i = parseint(*argv, NULL, 0)) == INT_MIN) {
		respond(cmdresp, "!%s win focus: %s", ebadarg, *argv);
		return -1;
	}
	nparsed++;
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
	}
	xcb_aux_sync(con);
	ignore(XCB_ENTER_NOTIFY);
	return nparsed;
}

int cmdfollow(Workspace *ws)
{
	if (ws && cmdc && ws != cmdc->ws) {
		cmdsend(ws);
		cmdview(ws);
	}
	return 0;
}

int cmdfull(__attribute__((unused)) char **argv)
{
	setfullscreen(cmdc, !(cmdc->state & STATE_FULLSCREEN));
	return 0;
}

int cmdgappx(char **argv)
{
	int i, rel, nparsed = 0;

	if (!strcmp(*argv, "width")) argv++, nparsed++;
	if (!*argv) {
		respond(cmdresp, "!gap %s", enoargs);
		return -1;
	} else if ((i = parseint(*argv, &rel, 1)) == INT_MIN) {
		respond(cmdresp, "!invalid value for gap: %s", *argv);
		return -1;
	} else {
		nparsed++;
		adjustisetting(i, rel, &setws->gappx, border[BORD_WIDTH], 1);
	}
	return nparsed;
}

int cmdkill(__attribute__((unused)) char **argv)
{
	if (!sendwmproto(cmdc, WM_DELETE)) {
		xcb_grab_server(con);
		xcb_set_close_down_mode(con, XCB_CLOSE_DOWN_DESTROY_ALL);
		xcb_kill_client(con, cmdc->win);
		xcb_aux_sync(con);
		xcb_ungrab_server(con);
	}
	xcb_aux_sync(con);
	ignore(XCB_ENTER_NOTIFY);
	return 0;
}

int cmdlayout(char **argv)
{
	for (unsigned int i = 0; layouts[i].name; i++)
		if (!strcmp(layouts[i].name, *argv)) {
			Client *c = NULL;
			if (tilecount(setws) == 1) c = nexttiled(setws->clients);
			if ((lytchange = &layouts[i] != setws->layout)) {
				needsrefresh = 1;
				if ((setws->layout = &layouts[i])->func == NULL && c) {
					c->w = c->ws->mon->ww / 1.5;
					c->h = c->ws->mon->wh / 1.3;
					gravitate(c, GRAV_CENTER, GRAV_CENTER, 0);
				}
			}
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
	{
		return -1;
	}
	return 1;
}

int cmdmouse(char **argv)
{
	int arg, nparsed = 0;
	xcb_mod_mask_t oldmod = mousemod;
	xcb_button_t oldmove = mousemove, oldresize = mouseresize;

	while (*argv) {
		if (!strcmp("mod", *argv)) {
			argv++, nparsed++;
			if (!strcmp("alt", *argv) || !strcmp("mod1", *argv))
				mousemod = XCB_MOD_MASK_1;
			else if (!strcmp("super", *argv) || !strcmp("mod4", *argv))
				mousemod = XCB_MOD_MASK_4;
			else if (!strcmp("ctrl", *argv) || !strcmp("control", *argv))
				mousemod = XCB_MOD_MASK_CONTROL;
			else
				goto badvalue;
		} else if ((arg = !strcmp("move", *argv)) || !strcmp("resize", *argv)) {
			argv++, nparsed++;
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
		argv++, nparsed++;
	}
	if (selws && selws->sel && (oldmove != mousemove
				|| oldresize != mouseresize || oldmod != mousemod))
		grabbuttons(selws->sel);
	return nparsed;
}

int cmdmvstack(char **argv)
{
	char arg[8];
	char *dir = "y";
	char *newargv[] = { dir, arg, NULL };

	if (!cmdc || FLOATING(cmdc) || FULLSCREEN(cmdc)) {
		respond(cmdresp, "!no window available to move in the stack");
		return -1;
	}
	if (!strcmp("up", *argv)) {
		strlcpy(arg, "-20", sizeof(arg));
	} else if (!strcmp("down", *argv)) {
		strlcpy(arg, "+20", sizeof(arg));
	} else if (parseint(*argv, NULL, 0) != INT_MIN) {
		strlcpy(arg, *argv, sizeof(arg));
	} else {
		respond(cmdresp, "!win mvstack: invalid value: %s", *argv);
		return -1;
	}
	cmdresize(newargv);
	return 1;
}

int cmdpad(char **argv)
{
	int i, rel, orig, nparsed = 0;

#define PAD(v, max)                                                                       \
	argv++, nparsed++, orig = v;                                                          \
	if (!argv || (i = parseintclamp(*argv, &rel, v * -1, max)) == INT_MIN) goto badvalue; \
	v = CLAMP(rel ? v + i : i, 0, max);                                                   \
	needsrefresh = needsrefresh || v != orig

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
		argv++, nparsed++;
	}
	return nparsed;
#undef PAD
}

int cmdexit(__attribute__((unused)) char **argv)
{
	running = 0;
	return 0;
}

int cmdreload(__attribute__((unused)) char **argv)
{
	execcfg();
	return 0;
}

int cmdresize(char **argv)
{
	Client *c = cmdc, *t;
	Workspace *ws = c ? c->ws : selws;
	float f, *sf;
	int i, nparsed = 0;
	int xgrav = GRAV_NONE, ygrav = GRAV_NONE;
	int x = INT_MIN, y = INT_MIN, w = INT_MIN, h = INT_MIN, bw = INT_MIN;
	int ohoff, relx = 0, rely = 0, relw = 0, relh = 0, relbw = 0;

#define ARG(val, rel, allowzero)                                                    \
	argv++, nparsed++;                                                              \
	if (!argv || (val = parseint(*argv, rel, allowzero)) == INT_MIN) goto badvalue; \

	if (!c) {
		respond(cmdresp, "!no window available to resize");
		return -1;
	} else if (FULLSCREEN(c)) {
		respond(cmdresp, "!unable to resize fullscreen windows");
		return -1;
	}
	while (*argv) {
		if (!strcmp("x", *argv)) {
			argv++, nparsed++;
			if (!argv || !parsegeom(*argv, 'x', &x, &relx, &xgrav)) goto badvalue;
		} else if (!strcmp("y", *argv)) {
			argv++, nparsed++;
			if (!argv || !parsegeom(*argv, 'y', &y, &rely, &ygrav)) goto badvalue;
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
		argv++, nparsed++;
	}

	if (bw != INT_MIN) {
		if (c->bw != (bw = CLAMP(relbw ? c->bw + bw : bw, 0, ws->mon->wh / 6))) {
			if ((c->bw = bw) == 0) c->state |= STATE_NOBORDER;
			needsrefresh = 1;
		}
	}

	if (!FLOATING(c) && y != INT_MIN) movestack(y > 0 || ygrav == GRAV_BOTTOM ? 1 : -1);

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
	} else if (ISTILE(ws)) {
		if (w != INT_MIN) {
			if (ws->layout->func == rtile) w += w * -2;
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

int cmdrestart(__attribute__((unused)) char **argv)
{
	running = 0, restart = 1;
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
		.focus = 0, .state = STATE_NONE, .type = 0, .xgrav = GRAV_NONE, .ygrav = GRAV_NONE,
		.cb = NULL, .mon = NULL, .inst = NULL, .class = NULL, .title = NULL,
	};

#define ARG(val)                                                           \
	argv++, nparsed++;                                                     \
	if (!argv || (j = parseint(*argv, NULL, 0)) == INT_MIN) goto badvalue; \
	val = j
#define STR(val)                        \
	argv++, nparsed++;                  \
	if (!argv || !*argv) goto badvalue; \
	val = *argv
#define STATE(val)                                          \
	argv++, nparsed++;                                      \
	if (!argv || (j = parsebool(*argv)) < 0) goto badvalue; \
	r.state |= j ? val : STATE_NONE

	while (*argv) {
		if (!strcmp(*argv, "class") || !strcmp(*argv, "match_class")) {
			STR(r.class);
		} else if (!strcmp(*argv, "instance") || !strcmp(*argv, "match_instance")) {
			STR(r.inst);
		} else if (!strcmp(*argv, "title") || !strcmp(*argv, "match_title")) {
			STR(r.title);
		} else if (!strcmp(*argv, "type") || !strcmp(*argv, "match_type")) {
			argv++, nparsed++;
			if (!argv || !*argv) goto badvalue;
			if (!strcmp(*argv, "splash"))      r.type = netatom[NET_TYPE_SPLASH];
			else if (!strcmp(*argv, "dialog")) r.type = netatom[NET_TYPE_DIALOG];
			else                               goto badvalue;

		} else if (!strcmp(*argv, "mon")) {
			STR(r.mon);
		} else if (!strcmp(*argv, "ws")) {
			argv++, nparsed++;
			if (!argv) goto badvalue;
			if ((r.ws = parseintclamp(*argv, NULL, 1, globalcfg[GLB_WS_NUM].val)) == INT_MIN) {
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
			argv++, nparsed++;
			if (argv)
				for (i = 0; callbacks[i].name; i++)
					if (!strcmp(callbacks[i].name, *argv)) {
						r.cb = &callbacks[i];
						break;
					}
			if (!r.cb) goto badvalue;
		} else if (!strcmp(*argv, "x")) {
			argv++, nparsed++;
			if (!argv || !parsegeom(*argv, 'x', &r.x, NULL, &r.xgrav)) goto badvalue;
			DBG("cmdrule: x gravity set: %s", gravities[r.xgrav])
		} else if (!strcmp(*argv, "y")) {
			argv++, nparsed++;
			if (!argv || !parsegeom(*argv, 'y', &r.y, NULL, &r.ygrav)) goto badvalue;
			DBG("cmdrule: y gravity set: %s", gravities[r.ygrav])
		} else if (!strcmp("w", *argv) || !strcmp("width", *argv)) {
			ARG(r.w);
		} else if (!strcmp("h", *argv) || !strcmp("height", *argv)) {
			ARG(r.h);
		} else if (!strcmp("bw", *argv) || !strcmp("border_width", *argv)) {
			argv++, nparsed++;
			if (!argv || (j = parseintclamp(*argv, NULL, 0, primary->h / 6)) == INT_MIN) goto badvalue;
			if ((r.bw = j) == 0 && border[BORD_WIDTH]) r.state |= STATE_NOBORDER;
		} else if (!strcmp(*argv, "float")) {
			STATE(STATE_FLOATING);
		} else if (!strcmp(*argv, "stick")) {
			STATE(STATE_STICKY | STATE_FLOATING);
		} else if (!strcmp(*argv, "ignore")) {
			STATE(STATE_IGNORECFG);
		} else if (!strcmp(*argv, "focus")) {
			argv++, nparsed++;
			if (!argv || (j = parsebool(*argv)) < 0) goto badvalue;
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
		argv++, nparsed++;
	}

	if ((r.class || r.inst || r.title || r.type) && (r.ws != -1 || r.mon || r.focus || r.cb
				|| r.state != STATE_NONE || r.x != -1 || r.y != -1 || r.w != -1
				|| r.h != -1 || r.bw != -1 || r.xgrav != GRAV_NONE || r.ygrav != GRAV_NONE))
	{
#define M(a, b) (a == NULL || (b && !strcmp(a, b)))
		FOR_EACH(pr, rules) { /* free any existing rule that uses the same matches */
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
					if (c->cb) c->cb->func(c, 0);
				}
				needsrefresh = 1;
			}
		}
	}
	return nparsed;
#undef ARG
#undef STR
#undef M
}

int cmdsend(Workspace *ws)
{
	Client *c = cmdc;

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
	unsigned int j;
	Workspace *ws = NULL;
	int i = -1, nparsed = 0, names = 0;

	if (!argv || !*argv) {
		respond(cmdresp, "!set %s", enoargs);
		return -1;
	}

	setws = selws;
	while (*argv) {
		if (!strcmp("ws", *argv)) {
			argv++, nparsed++;
			if (!argv) goto badvalue;
			if (!strcmp("_", *argv)) {
				setws = &wsdef;
				wsdef.mon = selmon;
				if ((i = cmdws_(argv + 1)) == -1) return -1;
				setws = selws;
				argv += i + 1, nparsed += i + 1;
				continue;
			} else if (!(ws = parsewsormon(*argv, 0))) {
				goto badvalue;
			}
			setws = ws;
		} else if (!strcmp("mon", *argv)) {
			argv++, nparsed++;
			if (!globalcfg[GLB_WS_STATIC].val) {
				respond(cmdresp, "!unable to set workspace monitor without static_ws=true");
				return -1;
			} else if (!argv || !(ws = parsewsormon(*argv, 1))) {
				respond(cmdresp, "!invalid monitor index or name: %s", argv ? *argv : NULL);
				return -1;
			}
			assignws(setws, ws->mon);
		} else if (!strcmp("name", *argv)) {
			argv++, nparsed++;
			if (!argv || !*argv) goto badvalue;
			if (strcmp(setws->name, *argv)) {
				strlcpy(setws->name, *argv, sizeof(setws->name));
				names = 1;
				wschange = 1;
			}
		} else {
			int match = 0;
			for (j = 0; j < LEN(globalcfg); j++) {
				if ((match = !strcmp(globalcfg[j].str, *argv))) {
					argv++, nparsed++;
					switch (globalcfg[j].type) {
					case TYPE_BOOL:
						if (!argv || (i = parsebool(*argv)) < 0) goto badvalue;
						needsrefresh = needsrefresh || globalcfg[j].val != i;
						globalcfg[j].val = i;
						break;
					case TYPE_NUMWS:
						if (!argv || (i = parseintclamp(*argv, NULL, 1, 99)) == INT_MIN) goto badvalue;
						if (i > globalcfg[j].val) updworkspaces(i);
						break;
					case TYPE_INT:
						if (!argv || (i = parseintclamp(*argv, NULL, 10, 1000)) == INT_MIN) goto badvalue;
						needsrefresh = needsrefresh || globalcfg[j].val != i;
						globalcfg[j].val = i;
						break;
					}
					argv++, nparsed++;
					break;
				}
			}
			if (!match)
				for (j = 0; setcmds[j].str; j++)
					if ((match = !strcmp(setcmds[j].str, *argv))) {
						if ((i = setcmds[j].func(argv + 1)) == -1) return -1;
						argv += i + 1, nparsed += i + 1;
						break;
					}
			if (match) continue;
			break;
badvalue:
			respond(cmdresp, "!set: invalid value for %s: %s", *(argv - 1), *argv);
			return -1;
		}
		argv++, nparsed++;
	}
	if (names) setnetwsnames();
	return nparsed;
}

int cmdsplit(char **argv)
{
	int rel = 0;
	float f = 0.0;

	if ((f = parsefloat(*argv, &rel)) && f != NAN) {
		float *ff = !strcmp("msplit", *(argv - 1)) ? &setws->msplit : &setws->ssplit;
		if (setws->layout->func && f != 0.0) {
			float nf = rel ? CLAMP(f < 1.0 ? f + *ff : f - 1.0, 0.05, 0.95) : CLAMP(f, 0.05, 0.95);
			if (nf != *ff) *ff = nf, needsrefresh = 1;
		}
		return 1;
	}
	return -1;
}

int cmdstatus(char **argv)
{
	int i, nparsed = 0;
	Status s = {
		.num = -1, .type = STAT_BAR, .file = cmdresp, .path = NULL, .next = NULL
	};

	while (*argv) {
		if (!strcmp("type", *argv)) {
			argv++, nparsed++;
			if (!strcmp("ws", *argv))          s.type = STAT_WS, wschange = 1;
			else if (!strcmp("bar", *argv))    s.type = STAT_BAR;
			else if (!strcmp("win", *argv))    s.type = STAT_WIN, winchange = 1;
			else if (!strcmp("full", *argv))   s.type = STAT_FULL;
			else if (!strcmp("layout", *argv)) s.type = STAT_LYT, lytchange = 1;
			else                               goto badvalue;
		} else if (!strcmp("num", *argv)) {
			argv++, nparsed++;
			if (!argv || (i = parseintclamp(*argv, NULL, -1, INT_MAX)) == INT_MIN) goto badvalue;
			s.num = i;
		} else if (!strcmp("file", *argv)) {
			argv++, nparsed++;
			if (!*argv) goto badvalue;
			s.path = *argv;
		} else {
			break;
badvalue:
			respond(cmdresp, "!status: invalid value for %s: %s", *(argv - 1), *argv);
			return -1;
		}
		argv++, nparsed++;
	}

	if (s.path && s.path[0] && !(s.file = fopen(s.path, "w")))
		respond(cmdresp, "!unable to open file in write mode: %s: %s", s.path, strerror(errno));
	if (s.file) {
		if (s.num == 1) {
			printstatus(&s, 0);
		} else {
			status_usingcmdresp = s.file == cmdresp;
			printstatus(initstatus(&s), 1);
		}
	} else {
		respond(cmdresp, "!unable to create status: %s", s.path ? s.path : "stdout");
	}
	return nparsed;
}

int cmdstick(__attribute__((unused)) char **argv)
{
	Client *c = cmdc;
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
	return 0;
}

int cmdswap(__attribute__((unused)) char **argv)
{
	static Client *last = NULL;
	Client *c = cmdc, *old, *cur = NULL, *prev = NULL;

	if (FLOATING(c) || (c->state & STATE_FULLSCREEN
				&& c->w == c->ws->mon->w && c->h == c->ws->mon->h) || tilecount(c->ws) <= 1)
	{
		respond(cmdresp, "!unable to swap floating, fullscreen, or single tiled windows");
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
	return 0;
}

int cmdview(Workspace *ws)
{
	if (ws) {
		changews(ws, globalcfg[GLB_WS_STATIC].val ? 0 : !cmdusemon,
				cmdusemon || (globalcfg[GLB_WS_STATIC].val && selws->mon != ws->mon));
	}
	return 0;
}

int cmdwin(char **argv)
{
	Client *c;
	int e = 0, nparsed = 0;

	while (*argv) {
		if ((c = parseclient(*argv, &e))) {
			cmdc = c;
		} else if (e == -1) {
			respond(cmdresp, "!invalid window id: %s", *argv);
			return -1;
		} else {
			int match = 0;
			for (unsigned int ui = 0; wincmds[ui].str; ui++)
				if ((match = !strcmp(wincmds[ui].str, *argv))) {
					if (cmdc && (e = wincmds[ui].func(argv + 1)) == -1) return -1;
					nparsed += e;
					argv += e;
					break;
				}
			if (!match) break;
		}
		argv++, nparsed++;
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
	int j, nparsed = 0, apply = 0;

	while (*argv) {
		int *s;
		float *ff;
		if (!strcmp(*argv, "apply")) {
			apply = 1;
		} else if (!strcmp(*argv, "layout")) {
			argv++, nparsed++;
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
			argv++, nparsed++;
			if (!argv || (j = parseintclamp(*argv, NULL, 0, INT_MAX - 1)) == INT_MIN) goto badvalue;
			*s = j;
		} else if ((ff = !strcmp(*argv, "msplit") ? &wsdef.msplit
					: !strcmp(*argv, "ssplit") ? &wsdef.ssplit : NULL))
		{
			argv++, nparsed++;
			if (!argv || (f = parsefloat(*argv, NULL)) == NAN) goto badvalue;
			*ff = CLAMP(f, 0.05, 0.95);
		} else if (!strcmp(*argv, "gap")) {
			argv++, nparsed++;
			if (!argv || (j = parseintclamp(*argv, NULL, 0, primary->h / 6)) == INT_MIN) goto badvalue;
			wsdef.gappx = j;
		} else if (!strcmp(*argv, "pad")) {
			setws = &wsdef;
			j = cmdpad(argv + 1);
			setws = selws;
			if (j == -1) return -1;
			nparsed += j;
			argv += j;
		} else {
			break;
badvalue:
			respond(cmdresp, "!set ws=_: invalid value for %s: %s", *(argv - 1), *argv);
			return -1;
		}
		argv++, nparsed++;
	}

	if (apply) {
		Workspace *ws;
		FOR_EACH(ws, workspaces) {
			lytchange = lytchange || ws->layout != wsdef.layout;
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
