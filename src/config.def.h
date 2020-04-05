/* In order to add callbacks or layouts to yaxwm,
 * copy this file to config.h and edit it
 *
 * see license file for copyright and license details */


/* example of a simple callback for album art windows */
static void albumart(Client *c)
{
	/* add padding to fit the window on the right */
	c->ws->padr = c->w + (2 * c->ws->gappx);

	/* move window to the right center of the screen, */
	gravitate(c, Right, Center, 1);

	/* focus the previously focused window (stack next) */
	focus(c->snext);

	/* apply changes and layout the workspace */
	layoutws(c->ws);
}

/* "callback" names recognized for use with rules.
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
