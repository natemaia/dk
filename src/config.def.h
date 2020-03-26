/* In order to configure yaxwm, copy this file to config.h and edit it
 * see license file for copyright and license details
 * vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
 */


/* simple example of a client callback function for mpv album art
 * move mpv album art window to the bottom left of the screen and focus the last window */
static void mpvart(Client *c) { gravitate(c, Bottom, Right, 1); focus(c->snext); }

/* callbacks recognized for use with window rules.
 * Callback functions have the following prototype: void function(Client *); */
static Callback callbacks[] = {
	{ "mpvart", mpvart },
};

/* primary keywords and parser functions
 * Keyword functions have the following prototype: void function(char **); */
static Keyword keywords[] = {
	{ "rule", cmdrule },
	{ "set",  cmdset  },
	{ "win",  cmdwin  },
	{ "wm",   cmdwm   },
	{ "ws",   cmdws   },
};

/* "set" keyword options, used by cmdset() to parse arguments
 * Keyword functions have the following prototype: void function(char **); */
static Keyword setcmds[] = {
	{ "border", cmdborder  },
	{ "gap",    cmdgappx   },
	{ "layout", cmdlayout  },
	{ "master", cmdnmaster },
	{ "mouse",  cmdmouse   },
	{ "pad",    cmdpad     },
	{ "split",  cmdsplit   },
	{ "stack",  cmdnstack  },
};

/* "win" keyword options, used by cmdwin() to parse arguments
 * Keyword functions have the following prototype: void function(char **); */
static Keyword wincmds[] = {
	{ "float",    cmdfloat    },
	{ "focus",    cmdfocus    },
	{ "kill",     cmdkill     },
	{ "mvresize", cmdmvresize },
	{ "stick",    cmdstick    },
	{ "swap",     cmdswap     },
};

/* "ws" names used by cmdws() to parse arguments.
 * Command functions have the following prototype: void function(int); */
static Command wscommands[] = {
	{ "follow", cmdfollow },
	{ "send",   cmdsend   },
	{ "view",   cmdview   },
};

/* "layout" names used by cmdlayout() to parse arguments.
 * Layout functions have the following prototype: int function(Workspace *); */
static Layout layouts[] = {
	{ "mono", mono },
	{ "none", NULL },
	{ "tile", tile },
};


/* Basic configuration still not handled by commands */
static WorkspaceRule workspacerules[] = {
	/* workspace default settings and how many to allocate if more are
	 * allocated later the values from the first rule will be use
	 * name, master, stack, gap, split, ssplit, padr, padl, padt, padb,  layout,     next */
	{ "0",     1,      3,    0,   0.5,   0.5,    0,    0,    0,    0,   &layouts[0], NULL },
	{ "1",     1,      3,    0,   0.5,   0.5,    0,    0,    0,    0,   &layouts[0], NULL },
	{ "2",     1,      3,    0,   0.5,   0.5,    0,    0,    0,    0,   &layouts[0], NULL },
	{ "3",     1,      3,    0,   0.5,   0.5,    0,    0,    0,    0,   &layouts[0], NULL },
	{ "4",     1,      3,    0,   0.5,   0.5,    0,    0,    0,    0,   &layouts[0], NULL },
	{ "5",     1,      3,    0,   0.5,   0.5,    0,    0,    0,    0,   &layouts[0], NULL },
	{ "6",     1,      3,    0,   0.5,   0.5,    0,    0,    0,    0,   &layouts[0], NULL },
	{ "7",     1,      3,    0,   0.5,   0.5,    0,    0,    0,    0,   &layouts[0], NULL },
	{ "8",     1,      3,    0,   0.5,   0.5,    0,    0,    0,    0,   &layouts[0], NULL },
	{ "9",     1,      3,    0,   0.5,   0.5,    0,    0,    0,    0,   &layouts[0], NULL },
};

