/* In order to customize settings or add new commands
 * copy this file to config.h and edit it
 *
 * see license file for copyright and license details */
#pragma once


unsigned int border[BORD_LAST] = {
	[BORD_WIDTH]     = 1,          /* int: total border width in pixels */
	[BORD_FOCUS]     = 0xFF6699cc, /* hex: focused window border colour (inner) */
	[BORD_URGENT]    = 0xFFee5555, /* hex: urgent window border colour (inner) */
	[BORD_UNFOCUS]   = 0xFF444444, /* hex: unfocused window border colour (inner) */
	[BORD_O_WIDTH]   = 0,          /* int: outer border width in pixels */
	[BORD_O_FOCUS]   = 0xFF222222, /* hex: focused window border colour (outer) */
	[BORD_O_URGENT]  = 0xFF222222, /* hex: urgent window border colour (outer) */
	[BORD_O_UNFOCUS] = 0xFF222222, /* hex: unfocused window border colour (outer) */
};

GlobalCfg globalcfg[GLB_LAST] = {
	/* setting           value,  type,       string */
	[GLB_FOCUS_MOUSE]  = { 1,   TYPE_BOOL,  "focus_mouse"  }, /* enable focus follows mouse */
	[GLB_FOCUS_OPEN]   = { 1,   TYPE_BOOL,  "focus_open"   }, /* enable focus on open */
	[GLB_FOCUS_URGENT] = { 1,   TYPE_BOOL,  "focus_urgent" }, /* enable focus urgent windows */
	[GLB_MIN_WH]       = { 50,  TYPE_INT,   "win_minwh"    }, /* minimum window size allowed when resizing */
	[GLB_MIN_XY]       = { 10,  TYPE_INT,   "win_minxy"    }, /* minimum window area allowed inside the screen when moving */
	[GLB_SMART_BORDER] = { 1,   TYPE_BOOL,  "smart_border" }, /* disable borders in layouts with only one visible window */
	[GLB_SMART_GAP]    = { 1,   TYPE_BOOL,  "smart_gap"    }, /* disable gaps in layouts with only one visible window */
	[GLB_TILE_HINTS]   = { 0,   TYPE_BOOL,  "tile_hints"   }, /* respect size hints in tiled layouts */
	[GLB_TILE_TOHEAD]  = { 0,   TYPE_BOOL,  "tile_tohead"  }, /* place new clients at the tail of the stack */
	[GLB_WS_NUM]       = { 0,   TYPE_NUMWS, "numws"        }, /* number of workspaces currently allocated */
	[GLB_WS_STATIC]    = { 0,   TYPE_BOOL,  "static_ws"    }, /* use static workspace assignment */
};

/* default modifier and buttons for mouse move/resize */
xcb_mod_mask_t mousemod = XCB_MOD_MASK_1;
xcb_button_t mousemove = XCB_BUTTON_INDEX_1;
xcb_button_t mouseresize = XCB_BUTTON_INDEX_3;

const char *cursors[] = {
	/* see: https://tronche.com/gui/x/xlib/appendix/b/ */
	[CURS_MOVE] = "fleur",
	[CURS_NORMAL] = "left_ptr",
	[CURS_RESIZE] = "sizing",
};

void albumart(Client *c, int closed)
{
	/*
	 * basic example of a user-defined callback
	 *
	 * on open:
	 *	apply padding, gravitate the window to the right-center, and avoid focus grab
	 * on close:
	 *	remove padding
	 */

	if (closed) {
		c->ws->padr = 0;
	} else {
		c->ws->padr = c->w + (2 * c->ws->gappx);
		gravitate(c, GRAV_RIGHT, GRAV_CENTER, 1);
		focus(c->snext);
	}
}

int focusmaster(char **argv)
{
	/*
	 * basic example of a new win command
	 *
	 * focus the master window on the current workspace
	 */
	focus(nexttiled(selws->clients));
	(void)(argv);
	return 0;
}

int tstack(Workspace *ws)
{
	/*
	 * basic example of a new user-defined layout
	 *
	 * an inverted version of the bottomstack layout for dwm:
	 *   https://dwm.suckless.org/patches/bottomstack/
	 *
	 * additions to work with dk padding, gaps, and other features.
	 */

	Client *c;
	int i, n, mh, mw, mx, sx, sw;

	if (!(n = tilecount(ws))) return 1;

	/* apply the workspace padding */
	int wx = ws->mon->wx + ws->padl;
	int wy = ws->mon->wy + ws->padt;
	int ww = ws->mon->ww - ws->padl - ws->padr;
	int wh = ws->mon->wh - ws->padt - ws->padb;

	/* apply smart gap */
	int g = !globalcfg[GLB_SMART_GAP].val || n > 1 ? ws->gappx : 0;
	mw = (ww - g) / MAX(1, ws->nmaster);

	/* adjust sizes for master-less instances */
	if (n > ws->nmaster) {
		mh = ws->nmaster ? (ws->msplit * wh) - (g / 2) : 0;
		sw = (ww - g) / (n - ws->nmaster);
	} else {
		mh = wh - g;
		sw = ww;
	}

	for (i = 0, mx = sx = wx + g, c = nexttiled(ws->clients); c; c = nexttiled(c->next), i++) {
		/* apply smart border */
		int bw = !globalcfg[GLB_SMART_BORDER].val || n > 1 ? c->bw : 0;
		if (i < ws->nmaster) { /* master windows */
			resizehint(c, mx, (wy + wh) - mh, mw - g - (2 * bw), mh - g - (2 * bw), bw, 0, 0);
			mx += W(c) + g;
		} else { /* stack windows */
			resizehint(c, sx, wy + g, sw - g - (2 * bw), wh - (mh + (2 * g)) - (2 * bw), bw, 0, 0);
			sx += W(c) + g;
		}
	}
	xcb_aux_sync(con);
	return 1;
}


Callback callbacks[] = {
	/* command,    function */
	{ "albumart",  albumart },

	/* don't add below the terminating null */
	{ NULL,        NULL     }
};

Cmd keywords[] = {
	/* command,  function */
	{ "mon",     cmdmon     },
	{ "quit",    cmdexit    },
	{ "exit",    cmdexit    },
	{ "reload",  cmdreload  },
	{ "restart", cmdrestart },
	{ "rule",    cmdrule    },
	{ "set",     cmdset     },
	{ "status",  cmdstatus  },
	{ "win",     cmdwin     },
	{ "ws",      cmdws      },

	/* don't add below the terminating null */
	{ NULL,      NULL    }
};

Cmd setcmds[] = {
	/* command,   function */
	{ "border",   cmdborder },
	{ "gap",      cmdgappx  },
	{ "layout",   cmdlayout },
	{ "master",   cmdmors   },
	{ "mouse",    cmdmouse  },
	{ "pad",      cmdpad    },
	{ "msplit",   cmdsplit  },
	{ "ssplit",   cmdsplit  },
	{ "stack",    cmdmors   },

	/* don't add below the terminating null */
	{ NULL,       NULL      }
};

Cmd wincmds[] = {
	/* command,   function */
	{ "cycle",    cmdcycle    },
	{ "fakefull", cmdfakefull },
	{ "float",    cmdfloat    },
	{ "full",     cmdfull     },
	{ "focus",    cmdfocus    },
	{ "kill",     cmdkill     },
	{ "resize",   cmdresize   },
	{ "stick",    cmdstick    },
	{ "swap",     cmdswap     },
	{ "focusm",   focusmaster },

	/* don't add below the terminating null */
	{ NULL,       NULL        }
};

Layout layouts[] = {
	/* command,   function,  implements_resize,  invert_split_direction */
	{ "tile",      ltile,          1,                      0 }, /* first is default */
	{ "rtile",     rtile,          1,                      0 },
	{ "mono",      mono,           0,                      0 },
	{ "grid",      grid,           0,                      0 },
	{ "spiral",    spiral,         1,                      0 },
	{ "dwindle",   dwindle,        1,                      0 },
	{ "tstack",    tstack,         1,                      1 },
	{ "none",      NULL,           1,                      0 }, /* NULL layout function is floating */
	{ "float",     NULL,           1,                      0 },

	/* don't add below the terminating null */
	{ NULL,        NULL,           0,                      0 }
};

WsCmd wscmds[] = {
	{ "follow", cmdfollow },
	{ "send",   cmdsend   },
	{ "view",   cmdview   },

	/* don't add below the terminating null */
	{ NULL,     NULL      }
};


/* workspaces defaults */
Workspace wsdef = {
	1,           /* nmaster */
	3,           /* nstack  */
	0,           /* gappx   */
	0,           /* padl    */
	0,           /* padr    */
	0,           /* padt    */
	0,           /* padb    */
	0.5,         /* msplit  */
	0.5,         /* ssplit  */
	&layouts[0], /* layout  */

	/* unused values - inherited from Workspace struct */
	0, { '\0' }, NULL, NULL, NULL, NULL, NULL
};
