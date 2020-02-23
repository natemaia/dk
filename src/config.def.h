/* In order to configure yaxwm, copy this file to config.h and edit it
 * see license file for copyright and license details
 * vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
 */

/* enable focus follows mouse */
static int focusmouse = 1;

/* modifier and mouse buttons used to activate move and resize */
static xcb_mod_mask_t mousemod = XCB_MOD_MASK_1;
static xcb_button_t   mousemove = XCB_BUTTON_INDEX_1;
static xcb_button_t   mouseresize = XCB_BUTTON_INDEX_3;

/* simple example of a client callback function for mpv album art */
/* move mpv album art window to the bottom left of the screen and focus the last window */
static void mpvart(Client *c) { gravitate(c, Bottom, Right, 1); focus(c->snext); }

static int borders[] = {
	[Width] = 1,          /* border width in pixels */
	[Smart] = 1,          /* disable borders in monocle layout or with only one tiled window */
	[Focus] = 0x6699cc,   /* focused window border colours, hex 0x000000-0xffffff */
	[Unfocus] = 0x000000, /* unfocused window border colours, hex 0x000000-0xffffff */
};

static Layout layouts[] = {
	/* name,      layout (NULL is floating) */
	{ "tile",     tile    },
	{ "monocle",  monocle },
	{ "none",     NULL    },
};

static ClientRule clientrules[] = {
	/* In order to get monitor and window info you can use the following commands:
	 * window class/instance: `xprop` (the regex matching is case insensitive)
	 * monitor name: `xrandr` (or use an index 0-n, the order is not guaranteed)
	 *
	 * class/instance,                    monitor,   workspace,  floating,  callback function */
	{ "^firefox$",                        "0",          -1,         0,        NULL }, /* active workspace on monitor 0, tiled */
	{ "^chromium$",                       "HDMI-A-0",   -1,         1,        NULL }, /* active workspace on HDMI-A-0, floating */
	{ "^gimp$",                            NULL,         2,         1,        NULL }, /* workspace 2, floating */
	{ "^(steam|lxappearance)$",            NULL,        -1,         1,        NULL }, /* current workspace, floating */
	{ "^(pavucontrol|transmission-gtk)$",  NULL,        -1,         1,        NULL }, /* current workspace, floating */
	{ "^gl$",                              NULL,        -1,         1,        mpvart }, /* current workspace, floating, with callback */
};

static WorkspaceRule workspacerules[] = {
	/* workspace default settings and how many to allocate
	 * if more are allocated later the values from the first rule will be used
	 *
	 * name,  nmaster,  nstack,  gappx,  splitratio,   layout (NULL is floating) */
	{ "0",     1,        3,        0,       0.5,       tile },
	{ "1",     1,        3,        0,       0.5,       tile },
	{ "2",     1,        3,        0,       0.5,       tile },
	{ "3",     1,        3,        0,       0.5,       tile },
	{ "4",     1,        3,        0,       0.5,       tile },
	{ "5",     1,        3,        0,       0.5,       tile },
	{ "6",     1,        3,        0,       0.5,       tile },
	{ "7",     1,        3,        0,       0.5,       tile },
	{ "8",     1,        3,        0,       0.5,       tile },
	{ "9",     1,        3,        0,       0.5,       tile },
};
