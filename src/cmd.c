#include "cmd.h"

void cmdborder(char **argv)
{
	Client *c;
	Workspace *ws;
	int i, rel, col = 0, first;
	int bw = border[BORD_WIDTH], old = border[BORD_WIDTH], ow = border[BORD_O_WIDTH];

	while (*argv) {
		int outer;
		if ((outer = !strcmp("outer", *argv) || !strcmp("outer_width", *argv))
				|| !strcmp(*argv, "width"))
		{
			col = 0;
			if ((i = parseint(*(++argv), &rel, 1)) == INT_MIN) break;
			adjustisetting(i, rel, outer ? &ow : &bw, selws->gappx + (outer ? bw : 0), 1);
		} else if (col || (first = !strcmp(*argv, "colour") || !strcmp(*argv, "color"))) {
			if (!col) {
				col = 1;
				argv++;
			}
			if (!strcmp("focus", *argv)) {
				if (parsecolour(*(++argv), &border[BORD_FOCUS]) < 0) break;
			} else if (!strcmp("urgent", *argv)) {
				if (parsecolour(*(++argv), &border[BORD_URGENT]) < 0) break;
			} else if (!strcmp("unfocus", *argv)) {
				if (parsecolour(*(++argv), &border[BORD_UNFOCUS]) < 0) break;
			} else if (!strcmp("outer_focus", *argv)) {
				if (parsecolour(*(++argv), &border[BORD_O_FOCUS]) < 0) break;
			} else if (!strcmp("outer_urgent", *argv)) {
				if (parsecolour(*(++argv), &border[BORD_O_URGENT]) < 0) break;
			} else if (!strcmp("outer_unfocus", *argv)) {
				if (parsecolour(*(++argv), &border[BORD_O_UNFOCUS]) < 0) break;
			} else if (first) {
				fprintf(cmdresp, "!%s border colour: %s\n", ebadarg, *argv);
				break;
			} else {
				col = first = 0;
				continue;
			}
		} else {
			fprintf(cmdresp, "!%s border: %s\n", ebadarg, *argv);
			break;
		}
		argv++;
	}

	if (bw - ow < 1 && (unsigned int)ow != border[BORD_O_WIDTH])
		fprintf(cmdresp, "!border outer exceeds limit: %d - maximum: %d\n", ow, bw - 1);
	else if (bw - ow > 0)
		border[BORD_O_WIDTH] = ow;
	border[BORD_WIDTH] = bw;
	FOR_CLIENTS(c, ws) {
		if (!(c->state & STATE_NOBORDER)) {
			if (c->bw == old) c->bw = bw;
			drawborder(c, c == selws->sel);
		}
	}
}

void cmdcycle(char **argv)
{
	Client *c = cmdclient, *first;

	if (FLOATING(c) || FULLSCREEN(c)) return;
	if (c == (first = nexttiled(selws->clients)) && !nexttiled(c->next))
		return;
	if (!(c = nexttiled(selws->sel->next))) c = first;
	focus(first);
	movestack(-1);
	focus(c);
	(void)(argv);
}

void cmdfakefull(char **argv)
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
}

void cmdfloat(char **argv)
{
	Client *c = cmdclient;

	if (!c->ws->layout->func) return;
	if (argv && *argv && !strcmp(*argv, "all")) {
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
		return;
	}
	if (FULLSCREEN(c) || c->state & (STATE_STICKY | STATE_FIXED))
		return;
	if ((c->state ^= STATE_FLOATING) & STATE_FLOATING) {
		if (c->old_x + c->old_y == c->ws->mon->wx + c->ws->mon->wy)
			quadrant(c, &c->old_x, &c->old_y, &c->old_w, &c->old_h);
		resizehint(c, c->old_x, c->old_y, c->old_w, c->old_h, c->bw, 0, 1);
	} else {
		SAVEOLD(c);
	}
	needsrefresh = 1;
}

void cmdfocus(char **argv)
{
	int i = 0, opt;
	Client *c = cmdclient;

	if (FULLSCREEN(c) || !c->ws->clients->next) return;
	if (c != selws->sel) {
		focus(c);
		return;
	}
	if ((opt = parseopt(*argv, opts)) < 0 && (i = parseint(*argv, NULL, 0)) == INT_MIN) {
		fprintf(cmdresp, "!%s focus: %s\n", ebadarg, *argv);
		return;
	}
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
		}
	}
}

void cmdfollow(Workspace *ws)
{
	cmdsend(ws);
	cmdview(ws);
}

void cmdfull(char **argv)
{
	setfullscreen(cmdclient, !(cmdclient->state & STATE_FULLSCREEN));
	(void)(argv);
}

void cmdgappx(char **argv)
{
	int i, ng, rel;

	if (!strcmp(*argv, "width")) argv++;
	ng = setws->gappx;
	if (!*argv) {
		fprintf(cmdresp, "!gap %s\n", enoargs);
	} else if ((i = parseint(*argv, &rel, 1)) != INT_MIN) {
		adjustisetting(i, rel, &ng, border[BORD_WIDTH], 1);
		if (ng != setws->gappx) setws->gappx = ng;
	}
}

void cmdkill(char **argv)
{
	if (!sendwmproto(cmdclient, WM_DELETE)) {
		xcb_grab_server(con);
		xcb_set_close_down_mode(con, XCB_CLOSE_DOWN_DESTROY_ALL);
		xcb_kill_client(con, cmdclient->win);
		xcb_aux_sync(con);
		xcb_ungrab_server(con);
	} else {
		xcb_flush(con);
	}
	(void)(argv);
}

void cmdlayout(char **argv)
{
	for (unsigned int i = 0; i < LEN(layouts); i++)
		if (!strcmp(layouts[i].name, *argv)) {
			if (&layouts[i] != setws->layout)
				setws->layout = &layouts[i];
			return;
		}
	fprintf(cmdresp, "!invalid layout name: %s\n", *argv);
}

void cmdmon(char **argv)
{
	if (monitors && nextmon(monitors)) {
		cmdusemon = 1;
		adjustwsormon(argv);
		cmdusemon = 0;
	}
}

void cmdmouse(char **argv)
{
	int arg;

	while (*argv) {
		if (!strcmp("mod", *argv)) {
			argv++;
			if (!strcmp("alt", *argv) || !strcmp("mod1", *argv))
				mousemod = XCB_MOD_MASK_1;
			else if (!strcmp("super", *argv) || !strcmp("mod4", *argv))
				mousemod = XCB_MOD_MASK_4;
			else if (!strcmp("ctrl", *argv) || !strcmp("control", *argv))
				mousemod = XCB_MOD_MASK_CONTROL;
			else {
				fprintf(cmdresp, "!invalid modifier: %s\n", *argv);
				break;
			}
		} else if ((arg = !strcmp("move", *argv)) || !strcmp("resize", *argv)) {
			argv++;
			xcb_button_t *btn = arg ? &mousemove : &mouseresize;
			if (!strcmp("button1", *argv))
				*btn = XCB_BUTTON_INDEX_1;
			else if (!strcmp("button2", *argv))
				*btn = XCB_BUTTON_INDEX_2;
			else if (!strcmp("button3", *argv))
				*btn = XCB_BUTTON_INDEX_3;
			else {
				fprintf(cmdresp, "!invalid button: %s\n", *argv);
				break;
			}
		} else {
			fprintf(cmdresp, "!%s mouse: %s\n", ebadarg, *argv);
			break;
		}
		if (*argv)
			argv++;
	}
	if (selws->sel)
		grabbuttons(selws->sel, 1);
}

void cmdnmaster(char **argv)
{
	int i, rel = 1;

	if ((i = parseint(*argv, &rel, 1)) != INT_MIN)
		adjustisetting(i, rel, &setws->nmaster, 0, 0);
}

void cmdnstack(char **argv)
{
	int i, rel = 1;

	if ((i = parseint(*argv, &rel, 1)) != INT_MIN)
		adjustisetting(i, rel, &setws->nstack, 0, 0);
}

void cmdpad(char **argv)
{
	int i, rel;

#define PAD(v, o)                                                   \
	if ((i = parseintclamp(*(++argv), &rel, v * -1, o)) == INT_MIN) \
		break;                                                      \
	v = CLAMP(rel ? v + i : i, 0, o);                               \
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
			fprintf(cmdresp, "!%s pad: %s\n", ebadarg, *argv);
			break;
		}
		argv++;
	}
#undef PAD
}

void cmdresize(char **argv)
{
	Client *c = cmdclient, *t;
	int i, ohoff;
	float f, *sf;
	int xgrav = GRAV_NONE, ygrav = GRAV_NONE;
	int x = INT_MIN, y = INT_MIN, w = INT_MIN, h = INT_MIN, bw = INT_MIN;
	int relx = 0, rely = 0, relw = 0, relh = 0, relbw = 0;

#define ARG(val, relptr, z)						         \
	if ((i = parseint(*(++argv), relptr, z)) == INT_MIN) \
		break;                                           \
	val = i

	if (FULLSCREEN(c)) return;
	while (*argv) {
		if (!strcmp("x", *argv)) {
			argv++;
			if (!parsegeom(*argv, 'x', &x, &relx, &xgrav)) break;
		} else if (!strcmp("y", *argv)) {
			argv++;
			if (!parsegeom(*argv, 'y', &y, &rely, &ygrav)) break;
		} else if (!strcmp("w", *argv) || !strcmp("width", *argv)) {
			ARG(w, &relw, 0);
		} else if (!strcmp("h", *argv) || !strcmp("height", *argv)) {
			ARG(h, &relh, 0);
		} else if (!strcmp("bw", *argv) || !strcmp("border_width", *argv)) {
			ARG(bw, &relbw, 1);
		} else {
			fprintf(cmdresp, "!%s resize: %s\n", ebadarg, *argv);
			break;
		}
		argv++;
	}
#undef ARG

	if (FLOATING(c)) {
		x = x == INT_MIN || xgrav != GRAV_NONE ? c->x : (relx ? c->x + x : x);
		y = y == INT_MIN || ygrav != GRAV_NONE ? c->y : (rely ? c->y + y : y);
		w = w == INT_MIN ? c->w : (relw ? c->w + w : w);
		h = h == INT_MIN ? c->h : (relh ? c->h + h : h);
		bw = bw == -1 ? c->bw : (relbw ? c->bw + bw : bw);
		resizehint(c, x, y, w, h, bw, 1, 0);
		gravitate(c, xgrav, ygrav, 1);
	} else if (c->ws->layout->func == tile) {
		if (bw != INT_MIN) {
			c->bw = relbw ? c->bw + bw : bw;
			if (y == INT_MIN && !w && !h)
				drawborder(c, c == selws->sel);
		}
		if (y != INT_MIN)
			movestack(y > 0 || ygrav == GRAV_BOTTOM ? 1 : -1);
		if (w) {
			sf = &c->ws->ssplit;
			for (i = 0, t = nexttiled(c->ws->clients); t; t = nexttiled(t->next), i++)
				if (t == c) {
					if (c->ws->nmaster && i < c->ws->nmaster + c->ws->nstack)
						sf = &c->ws->msplit;
					f = relw ? ((c->ws->mon->ww * *sf) + w) / c->ws->mon->ww : w / c->ws->mon->ww;
					if (f < 0.05 || f > 0.95) {
						fprintf(cmdresp, "!width exceeded limit: %f\n", c->ws->mon->ww * f);
					} else {
						*sf = f;
						if (!h) needsrefresh = 1;
					}
					break;
				}
		}
		if (h) {
			ohoff = c->hoff;
			c->hoff = relh ? c->hoff + h : h;
			if (c->ws->layout->func(c->ws) == -1) {
				fprintf(cmdresp, "!height exceeded limit: %d\n", c->hoff);
				c->hoff = ohoff;
			}
		}
	} else {
		fprintf(cmdresp, "!unable to resize windows in %s layout\n", c->ws->layout->name);
		return;
	}
	eventignore(XCB_ENTER_NOTIFY);
}

void cmdrule(char **argv)
{
	int j;
	Client *c;
	Workspace *ws;
	Rule *pr, *nr = NULL;
	unsigned int i, delete = 0, apply = 0;
	Rule r = {
		.x = -1, .y = -1, .w = -1, .h = -1, .ws = -1, .bw = -1,
		.focus = 0, .state = STATE_NONE, .xgrav = GRAV_NONE, .ygrav = GRAV_NONE,
		.cb = NULL, .mon = NULL, .inst = NULL, .class = NULL, .title = NULL,
	};

	if ((apply = !strcmp("apply", *argv))) {
		argv++;
		if (!strcmp("all", *argv))
			goto applyall;
	} else if ((delete = !strcmp("remove", *argv) || !strcmp("delete", *argv))) {
		argv++;
		if (!strcmp("all", *argv)) {
			while (rules)
				freerule(rules);
			return;
		}
	}
#define ARG(val)                                       \
	if ((j = parseint(*(++argv), NULL, 0)) == INT_MIN) \
		break;                                         \
	val = j

	while (*argv) {
		if (!r.class && !strcmp(*argv, "class")) {
			r.class = *(++argv);
		} else if (!r.inst && !strcmp(*argv, "instance")) {
			r.inst = *(++argv);
		} else if (!r.title && !strcmp(*argv, "title")) {
			r.title = *(++argv);
		} else if (!strcmp(*argv, "mon")) {
			r.mon = *(++argv);
		} else if (!strcmp(*argv, "ws")) {
			if ((r.ws = parseintclamp(*(++argv), NULL, 1, 99)) == INT_MIN) {
				r.ws = -1;
				FOR_EACH(ws, workspaces)
					if (!strcmp(ws->name, *argv)) {
						r.ws = ws->num;
						break;
					}
			}
		} else if (!strcmp(*argv, "callback")) {
			argv++;
			for (i = 0; i < LEN(callbacks); i++)
				if (!strcmp(callbacks[i].name, *argv)) {
					r.cb = &callbacks[i];
					break;
				}
		} else if (!strcmp(*argv, "x")) {
			if (!parsegeom(*(++argv), 'y', &r.y, NULL, &r.ygrav)) break;
		} else if (!strcmp(*argv, "y")) {
			if (!parsegeom(*(++argv), 'y', &r.y, NULL, &r.ygrav)) break;
		} else if (!strcmp("w", *argv) || !strcmp("width", *argv)) {
			ARG(r.w);
		} else if (!strcmp("h", *argv) || !strcmp("height", *argv)) {
			ARG(r.h);
		} else if (!strcmp("bw", *argv) || !strcmp("border_width", *argv)) {
			if ((j = parseintclamp(*(++argv), NULL, 0, scr_h / 6)) == INT_MIN) break;
			r.bw = j;
			if (r.bw == 0 && border[BORD_WIDTH])
				r.state |= STATE_NOBORDER;
		} else if (!strcmp(*argv, "float")) {
			if ((j = parsebool(*(++argv))) < 0) break;
			r.state |= j ? STATE_FLOATING : STATE_NONE;
		} else if (!strcmp(*argv, "stick")) {
			if ((j = parsebool(*(++argv))) < 0) break;
			r.state |= j ? STATE_STICKY | STATE_FLOATING : STATE_NONE;
		} else if (!strcmp(*argv, "focus")) {
			if ((j = parsebool(*(++argv))) < 0) break;
			r.focus = j;
		} else {
			fprintf(cmdresp, "!%s rule: %s\n", ebadarg, *argv);
			break;
		}
		if (*argv)
			argv++;
	}
#undef ARG

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
#undef M
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
}

void cmdsend(Workspace *ws)
{
	Monitor *old;
	Client *c = cmdclient;

	if (!ws || !c || ws == c->ws)
		return;
	old = c->ws->mon;
	unfocus(c, 1);
	setworkspace(c, ws->num, c != c->ws->sel);
	if (ws->mon != old && ws->mon->ws == ws)
		relocate(c, ws->mon, old);
	needsrefresh = 1;
}

void cmdset(char **argv)
{
	Workspace *ws = NULL;
	unsigned int j;
	int i, names = 0, set = 0;

#define BOOL(val)                       \
	if ((i = parsebool(*(++argv))) < 0) \
		break; \
	globalcfg[GLB_##val] = i

	setws = selws;
	if (!*argv) {
		fprintf(cmdresp, "!set %s\n", enoargs);
		return;
	}
	while (*argv) {
		if (!strcmp("ws", *argv)) {
			argv++;
			if (!strcmp("default", *argv)) {
				cmdwsdef(argv + 1);
				break;
			} else if (!(ws = parsewsormon(*argv, 0))) {
				fprintf(cmdresp, "!%s ws: %s\n", ebadarg, *argv);
				break;
			}
			setws = ws;
			set = 1;
		} else if (!strcmp("mon", *argv)) {
			argv++;
			if (!globalcfg[GLB_STATICWS]) {
				fprintf(cmdresp, "!unable to set monitor with dynamic workspaces enabled\n");
				break;
			} else if (!set) {
				fprintf(cmdresp, "!workspace index or name is required to set the monitor\n");
				break;
			} else if (!(ws = parsewsormon(*argv, 1))) {
				fprintf(cmdresp, "!monitor index or name is required to assign workspace\n");
				break;
			}
			assignws(setws, ws->mon);
		} else if (!strcmp("numws", *argv)) {
			if ((i = parseintclamp(*(++argv), NULL, 1, 99)) == INT_MIN)
				break;
			if (i > globalcfg[GLB_NUMWS])
				updworkspaces(i);
		} else if (!strcmp("name", *argv)) {
			if (!*(++argv)) {
				fprintf(cmdresp, "!set ws name %s\n", enoargs);
				break;
			}
			strlcpy(setws->name, *argv, sizeof(setws->name));
			names = 1;
		} else if (!strcmp("tile_hints", *argv)) {
			BOOL(TILEHINTS);
		} else if (!strcmp("tile_tohead", *argv)) {
			BOOL(TILETOHEAD);
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
			BOOL(STATICWS);
		} else if (!strcmp("win_minxy", *argv)) {
			argv++;
			if ((i = parseintclamp(*argv, NULL, 10, 1000)) == INT_MIN)
				break;
			globalcfg[GLB_MIN_XY] = i;
		} else if (!strcmp("win_minwh", *argv)) {
			argv++;
			if ((i = parseintclamp(*argv, NULL, 10, 1000)) == INT_MIN)
				break;
			globalcfg[GLB_MIN_WH] = i;
		} else {
			for (j = 0; j < LEN(setcmds); j++)
				if (!strcmp(setcmds[j].str, *argv)) {
					((void (*)(char **))setcmds[j].func)(argv + 1);
					goto finish;
				}
			fprintf(cmdresp, "!%s set: %s\n", ebadarg, *argv);
		}
		argv++;
	}
#undef BOOL

finish:
	needsrefresh = 1;
	if (names) setnetwsnames();

}

void cmdmsplit(char **argv)
{
	int rel = 1;
	float f = 0.0;

	if ((f = parsefloat(*argv, &rel)) != -1.0)
		adjustfsetting(f, rel, &setws->msplit);
}

void cmdssplit(char **argv)
{
	int rel = 1;
	float f = 0.0;

	if ((f = parsefloat(*argv, &rel)) != -1.0)
		adjustfsetting(f, rel, &setws->ssplit);
}

void cmdstick(char **argv)
{
	Client *c = cmdclient;
	unsigned int all = 0xffffffff;

	if (FULLSCREEN(c)) return;
	if ((c->state ^= STATE_STICKY) & STATE_STICKY) {
		c->state &= ~STATE_STICKY;
		PROP(REPLACE, c->win, netatom[NET_WM_DESK], XCB_ATOM_CARDINAL, 32, 1, &c->ws->num);
	} else {
		cmdfloat(NULL);
		c->state |= STATE_STICKY | STATE_FLOATING;
		PROP(REPLACE, c->win, netatom[NET_WM_DESK], XCB_ATOM_CARDINAL, 32, 1, &all);
	}
	(void)(argv);
}

void cmdswap(char **argv)
{
	static Client *last = NULL;
	Client *c = cmdclient, *old, *cur = NULL, *prev = NULL;

	if (FLOATING(c) || (c->state & STATE_FULLSCREEN
				&& c->w == c->ws->mon->w && c->h == c->ws->mon->h))
		return;
	if (c == nexttiled(c->ws->clients)) {
		FIND_PREV(cur, last, c->ws->clients);
		if (cur != c->ws->clients)
			prev = nexttiled(cur->next);
		if (!prev || prev != last) {
			last = NULL;
			if (!(c = nexttiled(c->next))) return;
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
}

void cmdwin(char **argv)
{
	int e = 0;

	if ((cmdclient = parseclient(*argv, &e)))
		argv++;
	else if (e == -1)
		return;
	else
		cmdclient = selws->sel;
	if (cmdclient) {
		if (*argv) {
			for (unsigned int ui = 0; ui < LEN(wincmds); ui++)
				if (!strcmp(wincmds[ui].str, *argv)) {
					wincmds[ui].func(argv + 1);
					return;
				}
			fprintf(cmdresp, "!%s win: %s\n", ebadarg, *argv);
		} else {
			fprintf(cmdresp, "!win %s\n", enoargs);
		}
	}
}

void cmdws(char **argv)
{
	if (workspaces && workspaces->next)
		adjustwsormon(argv);
}

void cmdwsdef(char **argv)
{
	float f;
	unsigned int i;
	int j, pad = 0, first, apply = 0;


#define PAD(v)                                                             \
		if ((j = parseintclamp(*(++argv), NULL, 0, scr_h / 3)) == INT_MIN) \
			break;                                                         \
		v = j

	while (*argv) {
		int *s;
		float *ff;
		if (!strcmp(*argv, "apply")) {
			apply = 1;
		} else if (!strcmp(*argv, "layout")) {
			argv++;
			pad = 0;
			for (i = 0; i < LEN(layouts); i++)
				if (!strcmp(layouts[i].name, *argv)) {
					wsdef.layout = &layouts[i];
					break;
				}
		} else if ((s = !strcmp(*argv, "master") ? &wsdef.nmaster
					: !strcmp(*argv, "stack") ? &wsdef.nstack : NULL))
		{
			pad = 0;
			argv++;
			if ((j = parseintclamp(*argv, NULL, 0, INT_MAX - 1)) == INT_MIN) break;
			*s = j;
		} else if ((ff = !strcmp(*argv, "msplit") ? &wsdef.msplit
					: !strcmp(*argv, "ssplit") ? &wsdef.ssplit : NULL))
		{
			pad = 0;
			argv++;
			if ((f = parsefloat(*argv, NULL)) == -1.0) break;
			*ff = f;
		} else if (!strcmp(*argv, "gap")) {
			pad = 0;
			argv++;
			if ((j = parseintclamp(*argv, NULL, 0, scr_h / 6)) == INT_MIN) break;
			wsdef.gappx = j;
		} else if (pad || (first = !strcmp(*argv, "pad"))) {
			if (!pad) {
				pad = 1;
				argv++;
			}
			if (!strcmp("l", *argv) || !strcmp("left", *argv)) {
				PAD(wsdef.padl);
			} else if (!strcmp("r", *argv) || !strcmp("right", *argv)) {
				PAD(wsdef.padr);
			} else if (!strcmp("t", *argv) || !strcmp("top", *argv)) {
				PAD(wsdef.padt);
			} else if (!strcmp("b", *argv) || !strcmp("bottom", *argv)) {
				PAD(wsdef.padb);
			} else if (first) {
				fprintf(cmdresp, "!%s pad: %s\n", ebadarg, *argv);
				break;
			} else {
				pad = first = 0;
				continue;
			}
		} else {
			fprintf(cmdresp, "!%s workspace default: %s\n", ebadarg, *argv);
			break;
		}
		argv++;
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
#undef PAD
}

void cmdview(Workspace *ws)
{
	if (!ws) return;
	changews(ws, globalcfg[GLB_STATICWS] ? 0 : !cmdusemon,
			cmdusemon || (globalcfg[GLB_STATICWS] && selws->mon != ws->mon));
	needsrefresh = 1;
}

