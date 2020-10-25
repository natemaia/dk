#include "cmd.h"


int cmdborder(char **argv)
{
	Client *c;
	Workspace *ws;
	int i, nparsed = 0, rel, col = 0, first;
	int bw = border[BORD_WIDTH], old = border[BORD_WIDTH], ow = border[BORD_O_WIDTH];

	while (*argv) {
		int outer = 0;
		if (!strcmp(*argv, "width")
				|| (outer = !strcmp("outer", *argv) || !strcmp("outer_width", *argv)))
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
			if (!strcmp("focus", *argv)) {
				if (parsecolour(*(++argv), &border[BORD_FOCUS]) < 0) goto badvalue;
			} else if (!strcmp("urgent", *argv)) {
				if (parsecolour(*(++argv), &border[BORD_URGENT]) < 0) goto badvalue;
			} else if (!strcmp("unfocus", *argv)) {
				if (parsecolour(*(++argv), &border[BORD_UNFOCUS]) < 0) goto badvalue;
			} else if (!strcmp("outer_focus", *argv)) {
				if (parsecolour(*(++argv), &border[BORD_O_FOCUS]) < 0) goto badvalue;
			} else if (!strcmp("outer_urgent", *argv)) {
				if (parsecolour(*(++argv), &border[BORD_O_URGENT]) < 0) goto badvalue;
			} else if (!strcmp("outer_unfocus", *argv)) {
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
			fprintf(cmdresp, "!invalid %s value: %s\n", *(argv - 1), *argv);
			return -1;
		}
		argv++;
		nparsed++;
	}
	if (bw - ow < 1 && (unsigned int)ow != border[BORD_O_WIDTH])
		fprintf(cmdresp, "!border outer exceeds limit: %d - maximum: %d\n", ow, bw - 1);
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
		fprintf(cmdresp, "!unable to cycle floating or fullscreen windows\n");
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
		fprintf(cmdresp, "!unable to float fullscreen, sticky, or fixed windows\n");
		return -1;
	}
	if ((c->state ^= STATE_FLOATING) & STATE_FLOATING) {
		if (c->old_x + c->old_y == c->ws->mon->wx + c->ws->mon->wy)
			quadrant(c, &c->old_x, &c->old_y, &c->old_w, &c->old_h);
		resizehint(c, c->old_x, c->old_y, c->old_w, c->old_h, c->bw, 0, 1);
	} else {
		SAVEOLD(c);
	}
	needsrefresh = 1;
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
		fprintf(cmdresp, "!%s focus: %s\n", ebadarg, *argv);
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
		fprintf(cmdresp, "!gap %s\n", enoargs);
		return -1;
	} else if ((i = parseint(*argv, &rel, 1)) == INT_MIN) {
		fprintf(cmdresp, "!invalid value for gap: %s\n", *argv);
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
	return 0;
}

int cmdlayout(char **argv)
{
	for (unsigned int i = 0; i < LEN(layouts); i++)
		if (!strcmp(layouts[i].name, *argv)) {
			if (&layouts[i] != setws->layout)
				setws->layout = &layouts[i];
			return 1;
		}
	fprintf(cmdresp, "!invalid layout name: %s\n", *argv);
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
			fprintf(cmdresp, "!invalid value for %s: %s\n", *(argv - 1), *argv);
			return -1;
		}
		argv++;
		nparsed++;
	}
	if (selws->sel)
		grabbuttons(selws->sel, 1);
	return nparsed;
}

int cmdmors(char **argv)
{
	int i, rel = 1;

	if ((i = parseint(*argv, &rel, 1)) == INT_MIN
			|| adjustisetting(i, rel,
				!strcmp("stack", *(argv - 1)) ? &setws->nstack : &setws->nmaster, 0, 0) == -1)
		return -1;
	return 1;
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
			fprintf(cmdresp, "!invalid value for %s: %s\n", *(argv - 1), *argv);
			return -1;
		}
		argv++;
		nparsed++;
	}
	needsrefresh = 1;
	return nparsed;
#undef PAD
}

int cmdresize(char **argv)
{
	Client *c = cmdclient, *t;
	float f, *sf;
	int i, ohoff, nparsed = 0;
	int xgrav = GRAV_NONE, ygrav = GRAV_NONE;
	int x = INT_MIN, y = INT_MIN, w = INT_MIN, h = INT_MIN, bw = INT_MIN;
	int relx = 0, rely = 0, relw = 0, relh = 0, relbw = 0;

#define ARG(val, rel, z)						                       \
	nparsed++;                                                         \
	if ((val = parseint(*(++argv), rel, z)) == INT_MIN) goto badvalue; \

	if (FULLSCREEN(c) || (!FLOATING(c) && c->ws->layout->func != tile)) {
		fprintf(cmdresp, "!unable to resize fullscreen or non floating/tile windows\n");
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
			fprintf(cmdresp, "!invalid value for %s: %s\n", *(argv - 1), *argv);
			return -1;
		}
		argv++;
		nparsed++;
	}

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
						needsrefresh = 1;
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
				needsrefresh = 1;
			}
		}
	} else {
		fprintf(cmdresp, "!unable to resize windows in %s layout\n", c->ws->layout->name);
		return -1;
	}
	eventignore(XCB_ENTER_NOTIFY);
	return nparsed;
#undef ARG
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

	if ((apply = !strcmp("apply", *argv))) {
		argv++;
		nparsed++;
		if (!strcmp("all", *argv)) {
			nparsed++;
			goto applyall;
		}
	} else if ((delete = !strcmp("remove", *argv) || !strcmp("delete", *argv))) {
		argv++;
		nparsed++;
		if (!strcmp("all", *argv)) {
			nparsed++;
			while (rules)
				freerule(rules);
			return nparsed;
		}
	}
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
			if ((r.ws = parseintclamp(*(++argv), NULL, 1, globalcfg[GLB_NUMWS])) == INT_MIN) {
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
			for (i = 0; i < LEN(callbacks); i++)
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
			r.bw = j;
			if (r.bw == 0 && border[BORD_WIDTH])
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
		} else {
			break;
badvalue:
			fprintf(cmdresp, "!invalid value for %s: %s\n", *(argv - 1), *argv);
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
		fprintf(cmdresp, "!set %s\n", enoargs);
		return -1;
	}
	while (*argv) {
		if (!strcmp("ws", *argv)) {
			argv++;
			nparsed++;
			if (!strcmp("default", *argv)) {
				if ((i = cmdwsdef(argv + 1)) == -1) return -1;
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
			nparsed++;
			if ((i = parseintclamp(*(++argv), NULL, 1, 99)) == INT_MIN)
				goto badvalue;
			if (i > globalcfg[GLB_NUMWS])
				updworkspaces(i);
		} else if (!strcmp("name", *argv)) {
			nparsed++;
			if (!*(++argv)) goto badvalue;
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
			nparsed++;
			if ((i = parseintclamp(*(++argv), NULL, 10, 1000)) == INT_MIN) goto badvalue;
			globalcfg[GLB_MIN_XY] = i;
		} else if (!strcmp("win_minwh", *argv)) {
			nparsed++;
			if ((i = parseintclamp(*(++argv), NULL, 10, 1000)) == INT_MIN) goto badvalue;
			globalcfg[GLB_MIN_WH] = i;
		} else {
			int match = 0;
			for (j = 0; j < LEN(setcmds); j++)
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
			fprintf(cmdresp, "!invalid %s value: %s\n", *(argv - 1), *argv);
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

	if ((f = parsefloat(*argv, &rel)) != -1.0)
		return adjustfsetting(f, rel,
				!strcmp("msplit", *(argv - 1)) ? &setws->msplit : &setws->ssplit);
	return -1;
}

int cmdstick(char **argv)
{
	Client *c = cmdclient;
	unsigned int all = 0xffffffff;

	if (FULLSCREEN(c)) {
		fprintf(cmdresp, "!unable to change sticky state of fullscreen windows\n");
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
		fprintf(cmdresp, "!unable to swap floating or fullscreen windows\n");
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

int cmdwin(char **argv)
{
	int e = 0, nparsed = 0;

	if ((cmdclient = parseclient(*argv, &e))) {
		argv++;
		nparsed++;
	} else if (e == -1) {
		fprintf(cmdresp, "!invalid window id: %s", *argv);
		return e;
	} else {
		cmdclient = selws->sel;
	}
	if (cmdclient) {
		if (!*argv) {
			fprintf(cmdresp, "!win %s\n", enoargs);
			return -1;
		}
		while (*argv) {
			int match = 0;
			for (unsigned int ui = 0; ui < LEN(wincmds); ui++)
				if ((match = !strcmp(wincmds[ui].str, *argv))) {
					if ((e = wincmds[ui].func(argv + 1)) == -1)
						return -1;
					nparsed += e + 1;
				}
			if (!match) break;
			argv++;
			nparsed++;
		}
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

int cmdwsdef(char **argv)
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
			for (i = 0; i < LEN(layouts); i++)
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
			fprintf(cmdresp, "!invalid %s value: %s\n", *(argv - 1), *argv);
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

int cmdview(Workspace *ws)
{
	if (ws) {
		changews(ws, globalcfg[GLB_STATICWS] ? 0 : !cmdusemon,
				cmdusemon || (globalcfg[GLB_STATICWS] && selws->mon != ws->mon));
		needsrefresh = 1;
	}
	return 0;
}
