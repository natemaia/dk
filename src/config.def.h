/* In order to customize settings or add new commands
 * copy this file to config.h and edit it
 *
 * see license file for copyright and license details */
#pragma once

static unsigned int border[] = {
	[BORD_WIDTH]     = 1,          /* int: total border width in pixels */
	[BORD_FOCUS]     = 0xFF6699cc, /* hex: focused window border colour (inner) */
	[BORD_URGENT]    = 0xFFee5555, /* hex: urgent window border colour (inner) */
	[BORD_UNFOCUS]   = 0xFF444444, /* hex: unfocused window border colour (inner) */
	[BORD_O_WIDTH]   = 0,          /* int: outer border width in pixels */
	[BORD_O_FOCUS]   = 0xFF222222, /* hex: focused window border colour (outer) */
	[BORD_O_URGENT]  = 0xFF222222, /* hex: urgent window border colour (outer) */
	[BORD_O_UNFOCUS] = 0xFF222222, /* hex: unfocused window border colour (outer) */
};

static int globalcfg[] = {
	[GLB_FOCUS_MOUSE]  = 1,  /* bool: enable focus follows mouse */
	[GLB_FOCUS_OPEN]   = 1,  /* bool: enable focus on open */
	[GLB_FOCUS_URGENT] = 1,  /* bool: enable focus urgent windows */
	[GLB_MIN_WH]       = 50, /* int:  minimum window size allowed when resizing */
	[GLB_MIN_XY]       = 10, /* int:  minimum window area allowed inside the screen when moving */
	[GLB_NUMWS]        = 0,  /* bool: number of workspaces currently allocated */
	[GLB_TILEHINTS]    = 0,  /* bool: respect size hints in tiled layouts */
	[GLB_SMART_BORDER] = 1,  /* bool: disable borders in layouts with only one visible window */
	[GLB_SMART_GAP]    = 1,  /* bool: disable gaps in layouts with only one visible window */
	[GLB_TILETOHEAD]   = 0,  /* bool: place new clients at the tail of the stack */
	[GLB_STATICWS]     = 0,  /* bool: use static workspace assignment */
};

static const char *cursors[CURS_LAST] = {
	/* see: https://tronche.com/gui/x/xlib/appendix/b/ */
	[CURS_MOVE]   = "fleur",
	[CURS_NORMAL] = "arrow",
	[CURS_RESIZE] = "sizing",
};

/* default modifier and buttons for mouse move/resize */
static xcb_mod_mask_t mousemod = XCB_MOD_MASK_1;
static xcb_button_t mousemove = XCB_BUTTON_INDEX_1;
static xcb_button_t mouseresize = XCB_BUTTON_INDEX_3;

static void albumart(Client *c, int closed)
{
	/* example of a simple callback for album art windows */
	switch (closed) {
	case 0: /* opened */
		c->ws->padr = c->w + (2 * c->ws->gappx);
		gravitate(c, GRAV_RIGHT, GRAV_CENTER, 1);
		focus(c->snext);
		break;
	case 1: /* closed */
		c->ws->padr = 0;
		break;
	}
}

static const Callback callbacks[] = {
	/* name,       function */
	{ "albumart",  albumart },
};

static const Cmd keywords[] = {
	{ "mon",   cmdmon  },
	{ "rule",  cmdrule },
	{ "set",   cmdset  },
	{ "win",   cmdwin  },
	{ "ws",    cmdws   },
};

static const Cmd setcmds[] = {
	{ "border",  cmdborder  },
	{ "gap",     cmdgappx   },
	{ "layout",  cmdlayout  },
	{ "master",  cmdmors    },
	{ "mouse",   cmdmouse   },
	{ "pad",     cmdpad     },
	{ "msplit",  cmdsplit   },
	{ "ssplit",  cmdsplit   },
	{ "stack",   cmdmors    },
};

static const Cmd wincmds[] = {
	{ "cycle",    cmdcycle    },
	{ "fakefull", cmdfakefull },
	{ "float",    cmdfloat    },
	{ "full",     cmdfull     },
	{ "focus",    cmdfocus    },
	{ "kill",     cmdkill     },
	{ "resize",   cmdresize   },
	{ "stick",    cmdstick    },
	{ "swap",     cmdswap     },
};

static const Layout layouts[] = {
	{ "tile",     tile    }, /* first is default */
	{ "mono",     mono    },
	{ "grid",     grid    },
	{ "spiral",   spiral  },
	{ "dwindle",  dwindle },
	{ "none",     NULL    }, /* no layout means floating */
};

static const WsCmd wscmds[] = {
	{ "follow", cmdfollow },
	{ "send",   cmdsend   },
	{ "view",   cmdview   },
};

/* workspaces defaults */
static Workspace wsdef = {
	1,           /* nmaster */
	3,           /* nstack  */
	0,           /* gappx   */
	0,           /* padl    */
	0,           /* padr    */
	0,           /* padt    */
	0,           /* padb    */
	0.55,        /* msplit  */
	0.55,        /* ssplit  */
	&layouts[0], /* layout  */

	/* unused values - inherited from Workspace struct */
	0, { '\0' }, NULL, NULL, NULL, NULL, NULL
};
