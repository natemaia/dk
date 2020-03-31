/* In order to configure yaxwm, copy this file to config.h and edit it
 * see license file for copyright and license details
 * vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
 */


/* simple example of a callback function for album art windows */
static void albumart(Client *c)
{ /* add padding for the window width + gaps,
	 move window to the left center of the screen,
	 focus the last focused window */
	c->ws->padr = c->w + (2 * c->ws->gappx);
	gravitate(c, Center, Right, 1);
	focus(c->snext);
	layoutws(c->ws);
}

/* callbacks recognized for use with window rules.
 * Callback functions have the following prototype: void function(Client *); */
static Callback callbacks[] = {
	{ "albumart", albumart },
};


/* "layout" names used by cmdlayout() to parse arguments.
 * Layout functions have the following prototype: int function(Workspace *); */
static Layout layouts[] = {
	{ "tile", tile },
	{ "mono", mono },
	{ "none", NULL },
};


/* Basic configuration still not handled by commands */
static WsRule wsrules[] = {
	/* workspace default settings and how many to allocate if more are
	 * allocated later the values from the first rule will be use
	 * name, master, stack, gap, split, ssplit, padr, padl, padt, padb,  layout,     next */
	{ "1",     1,      3,    0,  0.55,   0.55,   0,    0,    0,    0,   &layouts[0], NULL },
	{ "2",     1,      3,    0,  0.55,   0.55,   0,    0,    0,    0,   &layouts[0], NULL },
	{ "3",     1,      3,    0,  0.55,   0.55,   0,    0,    0,    0,   &layouts[0], NULL },
	{ "4",     1,      3,    0,  0.55,   0.55,   0,    0,    0,    0,   &layouts[0], NULL },
	{ "5",     1,      3,    0,  0.55,   0.55,   0,    0,    0,    0,   &layouts[0], NULL },
	{ "6",     1,      3,    0,  0.55,   0.55,   0,    0,    0,    0,   &layouts[0], NULL },
	{ "7",     1,      3,    0,  0.55,   0.55,   0,    0,    0,    0,   &layouts[0], NULL },
	{ "8",     1,      3,    0,  0.55,   0.55,   0,    0,    0,    0,   &layouts[0], NULL },
	{ "9",     1,      3,    0,  0.55,   0.55,   0,    0,    0,    0,   &layouts[0], NULL },
	{ "10",    1,      3,    0,  0.55,   0.55,   0,    0,    0,    0,   &layouts[0], NULL },
};

