/* In order to customize settings or add new commands
 * copy this file to yaxwm.h and edit it
 *
 * see license file for copyright and license details */


static unsigned int border[] = {
	[Width] = 1,             /* total border width in pixels */
	[Focus] = 0xFF6699cc,    /* focused window border colour (inner) */
	[Urgent] = 0xFFee5555,   /* urgent window border colour (inner) */
	[Unfocus] = 0xFF444444,  /* unfocused window border colour (inner) */
	/* outer border settings */
	[Outer] = 0,             /* outer border width in pixels */
	[OFocus] = 0xFF222222,   /* outer focused window border colour when using double border */
	[OUrgent] = 0xFF222222,  /* outer urgent window border colour when using double border */
	[OUnfocus] = 0xFF222222, /* outer unfocused window border colour when using double border */
};

static int globalcfg[] = {
	[SmartGap] = 1,    /* disable gaps in layouts with only one visible window */
	[SmartBorder] = 1, /* disable borders in layouts with only one visible window */
	[SizeHints] = 0,   /* respect size hints in tiled layouts */
	[FocusMouse] = 1,  /* enable focus follows mouse */
	[FocusUrgent] = 1, /* enable focus urgent windows */
	[NumWs] = 0,       /* number of workspaces currently allocated */
	[MinXY] = 10,      /* minimum window area allowed inside the screen when moving */
	[MinWH] = 50,      /* minimum window size allowed when resizing */
};

static const char *cursors[] = {
	/* see: https://tronche.com/gui/x/xlib/appendix/b/ */
	[Move] = "fleur",
	[Normal] = "arrow",
	[Resize] = "sizing",
};

/* default modifier and buttons for mouse move/resize */
static xcb_mod_mask_t mousemod = XCB_MOD_MASK_4;
static xcb_button_t mousemove = XCB_BUTTON_INDEX_1;
static xcb_button_t mouseresize = XCB_BUTTON_INDEX_3;

static void albumart(Client *c, int closed)
{ /* example of a simple callback for album art windows */
	if (closed)
		c->ws->padr = 0; /* remove padding */
	else {
		c->ws->padr = c->w + (2 * c->ws->gappx); /* padding to the right */
		gravitate(c, Right, Center, 1); /* right center of the screen, respect gaps */
		focus(c->snext); /* don't take focus */
	}
}

/* "callback" names recognized for use with rules.
 * Callback functions have the following prototype: void function(Client *, int); */
static Callback callbacks[] = {
	{ "albumart", albumart },
};

/* "layout" names used by cmdlayout() to parse arguments.
 * Layout functions have the following prototype: int function(Workspace *); */
static Layout layouts[] = {
	{ "tile", tile }, /* first is initial default */
	{ "mono", mono },
	{ "none", NULL },
};

static WsDefault wsdef = { /* settings for newly created workspaces */
	1,           /* nmaster */
	3,           /* nstack */
	0,           /* gappx */
	0,           /* padl */
	0,           /* padr */
	0,           /* padt */
	0,           /* padb */
	0.55,        /* msplit */
	0.55,        /* ssplit */
	&layouts[0]  /* layout */
};

/* primary keywords and parser functions
 * Keyword functions have the following prototype: void function(char **); */
static const Keyword keywords[] = {
	{ "mon",   cmdmon   },
	{ "rule",  cmdrule  },
	{ "set",   cmdset   },
	{ "win",   cmdwin   },
	{ "wm",    cmdwm    },
	{ "ws",    cmdws    },
	{ "print", cmdprint },
};

/* "set" keyword options, used by cmdset() to parse arguments
 * Keyword functions have the following prototype: void function(char **); */
static const Keyword setcmds[] = {
	{ "border", cmdborder  },
	{ "gap",    cmdgappx   },
	{ "layout", cmdlayout  },
	{ "master", cmdnmaster },
	{ "mouse",  cmdmouse   },
	{ "pad",    cmdpad     },
	{ "msplit", cmdmsplit   },
	{ "ssplit", cmdssplit  },
	{ "stack",  cmdnstack  },
};

/* "win" keyword options, used by cmdwin() to parse arguments
 * Keyword functions have the following prototype: void function(char **); */
static const Keyword wincmds[] = {
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

/* "ws" and "mon" commands used by cmdws() and cmdmon() to parse arguments.
 * Command functions have the following prototype: void function(int); */
static const Command wsmoncmds[] = {
	{ "follow", cmdfollow },
	{ "send",   cmdsend   },
	{ "view",   cmdview   },
};

