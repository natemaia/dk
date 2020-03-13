/* In order to configure yaxwm, copy this file to config.h and edit it
 * see license file for copyright and license details
 * vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
 */

/* ---------------------------- */
/* Command keywords and options */
/* ---------------------------- */

/* simple example of a client callback function for mpv album art */
/* move mpv album art window to the bottom left of the screen and focus the last window */
static void mpvart(Client *c) { gravitate(c, Bottom, Right, 1); focus(c->snext); }

/* callbacks recognized for use with window rules */
static Callback callbacks[] = {
	/* name,     function */
	{ "mpvart",  mpvart },
};

/* primary keywords and parser functions */
static Keyword keywords[] = {
	{ "set",  cmdset  },
	{ "win",  cmdwin  },
	{ "wm",   cmdwm   },
	{ "ws",   cmdws   },
	{ "rule", cmdrule },
};

/* "set" keyword options, used by cmdset() to parse arguments */
static Keyword setcmds[] = {
	{ "border",  cmdborder  },
	{ "gap",     cmdgappx   },
	{ "layout",  cmdlayout  },
	{ "master",  cmdnmaster },
	{ "mouse",   cmdmouse   },
	{ "split",   cmdsplit   },
	{ "stack",   cmdnstack  },
};

/* "win" keyword options, used by cmdwin() to parse arguments */
static Keyword wincmds[] = {
	{ "float",    cmdfloat    },
	{ "focus",    cmdfocus    },
	{ "kill",     cmdkill     },
	{ "mvstack",  cmdmvstack  },
	{ "mvresize", cmdmvresize },
	{ "stick",    cmdstick    },
	{ "swap",     cmdswap     },
};

/* "rule" keyword options, used by cmdrule() to parse arguments */
static Keyword rulecmds[] = {
	{ "win", cmdwinrule },
	/* { "ws",  cmdwsrule }, /1* unfinished *1/ */
};

/* "ws" names used by cmdws() to parse arguments */
static Command wscommands[] = {
	/* name,     function */
	{ "view",    cmdview   },
	{ "send",    cmdsend   },
	{ "follow",  cmdfollow },
};

/* "layout" names used by cmdlayout() to parse arguments */
static Layout layouts[] = {
	/* name,     function (NULL is floating) */
	{ "tile",     tile    },
	{ "monocle",  monocle },
	{ "none",     NULL    },
};

/* ------------------------------------------------- */
/* Basic configuration still not handled by commands */
/* ------------------------------------------------- */

static WorkspaceRule workspacerules[] = {
	/* workspace default settings and how many to allocate if more are
	 * allocated later the values from the first rule will be used
	 * format: name, master, stack, gap, split, layout */
	{ "0", 1, 3, 0, 0.5, &layouts[0] },
	{ "1", 1, 3, 0, 0.5, &layouts[0] },
	{ "2", 1, 3, 0, 0.5, &layouts[0] },
	{ "3", 1, 3, 0, 0.5, &layouts[0] },
	{ "4", 1, 3, 0, 0.5, &layouts[0] },
	{ "5", 1, 3, 0, 0.5, &layouts[0] },
	{ "6", 1, 3, 0, 0.5, &layouts[0] },
	{ "7", 1, 3, 0, 0.5, &layouts[0] },
	{ "8", 1, 3, 0, 0.5, &layouts[0] },
	{ "9", 1, 3, 0, 0.5, &layouts[0] },
};

