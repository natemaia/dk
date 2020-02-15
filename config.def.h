/* In order to configure yaxwm, copy this file to config.h and edit it
 * see license file for copyright and license details
 * vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
 */

/* primary modifier for binds including mouse move/resize */
#define MOD    ALT

/* save some repetitive typing with the workspace keybinds */
#define WSBIND(ws, key) { PRESS, MOD,       key, view,   {.ui = ws} },\
                        { PRESS, MOD|SHIFT, key, send,   {.ui = ws} },\
                        { PRESS, MOD|CTRL,  key, follow, {.ui = ws} }

/* enable focus follows mouse */
static const int focusmouse = 1;
/* no borders when there's only one tiled window or when in monocle layout */
static const int smartborder = 1;

/* commands */
static const char *term[] = { "st", NULL };
static const char *menu[] = { "dmenu_run", NULL };
static const char *scrot[] = { "scrot", NULL };
static const char *scrots[] = { "scrot", "-s", NULL };
/* volume control commands */
static const char *voltg[] = { "pamixer", "-t", NULL };
static const char *volup[] = { "pamixer", "-i", "2", NULL };
static const char *voldn[] = { "pamixer", "-d", "2", NULL };

/* simple example of a client callback function for mpv album art */
/* gravitate window to the bottom-left of the screen, matching gap offset */
static void mpvart(Client *c) { gravitate(c, Bottom, Right, 1); }

static int borders[] = {
	[Width] = 1,          /* border width in pixels */
	[Focus] = 0x6699cc,   /* focused window border colours, hex 0x000000-0xffffff */
	[Unfocus] = 0x000000, /* unfocused window border colours, hex 0x000000-0xffffff */
};

static WsRule wsrules[] = {
	/* workspace default settings and how many to allocate
	 * if more are allocated later the values from the first rule will be used
	 *
	 * name,  nmaster,  nstack,  gappx,  splitratio,   layout function (NULL is floating) */
	{ "0",     1,        3,        0,       0.5,          tile },
	{ "1",     1,        3,        0,       0.5,          tile },
	{ "2",     1,        3,        0,       0.5,          tile },
	{ "3",     1,        3,        0,       0.5,          tile },
	{ "4",     1,        3,        0,       0.5,          tile },
	{ "5",     1,        3,        0,       0.5,          tile },
	{ "6",     1,        3,        0,       0.5,          tile },
	{ "7",     1,        3,        0,       0.5,          tile },
	{ "8",     1,        3,        0,       0.5,          tile },
	{ "9",     1,        3,        0,       0.5,          tile },
};

static Rule rules[] = {
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

static Bind binds[] = {
	/* type,   modifiers, keysym,    function,      arg */
	{ PRESS,   MOD|SHIFT,      XK_Return, runcmd,       {.v = term} },    /* terminal emulator */
	{ PRESS,   MOD,            XK_p,      runcmd,       {.v = menu} },    /* menu program */
	{ PRESS,   0,              XK_Print,  runcmd,       {.v = scrot} },   /* screenshot program */
	{ RELEASE, MOD,            XK_Print,  runcmd,       {.v = scrots} },  /* selection box screenshot */
	{ PRESS,   0,              MUTE,      runcmd,       {.v = voltg} },   /* volume mute command */
	{ PRESS,   0,              VOLUP,     runcmd,       {.v = volup} },   /* volume up command */
	{ PRESS,   0,              VOLDOWN,   runcmd,       {.v = voldn} },   /* volume down command */
	{ PRESS,   MOD,            XK_Insert, runcmd,       {.v = volup} },   /* volume up command */
	{ PRESS,   MOD,            XK_Delete, runcmd,       {.v = voldn} },   /* volume down command */
	{ PRESS,   MOD,            XK_q,      killclient,   {0} },            /* close active window */
	{ PRESS,   MOD,            XK_Tab,    swapclient,   {0} },            /* swap window with master and vise versa */
	{ PRESS,   MOD|SHIFT,      XK_space,  togglefloat,  {0} },            /* toggle active window floating state */
	{ PRESS,   MOD|SHIFT,      XK_q,      resetorquit,  {.i = 0} },       /* quit yaxwm */
	{ PRESS,   MOD|SHIFT,      XK_r,      resetorquit,  {.i = 1} },       /* restart yaxwm */
	{ PRESS,   MOD,            XK_j,      changefocus,  {.i = +1} },      /* focus next window */
	{ PRESS,   MOD,            XK_k,      changefocus,  {.i = -1} },      /* focus previous window */
	{ PRESS,   MOD|SHIFT,      XK_j,      movestack,    {.i = +1} },      /* move focused tiled window up the stack or master */
	{ PRESS,   MOD|SHIFT,      XK_k,      movestack,    {.i = -1} },      /* move focused tiled window down the stack or master */
	{ PRESS,   MOD,            XK_i,      setnmaster,   {.i = +1} },      /* increase number of windows in master */
	{ PRESS,   MOD,            XK_d,      setnmaster,   {.i = -1} },      /* decrease number of windows in master */
	{ PRESS,   MOD|SHIFT,      XK_i,      setnstack,    {.i = +1} },      /* increase number of windows in first stack */
	{ PRESS,   MOD|SHIFT,      XK_d,      setnstack,    {.i = -1} },      /* decrease number of windows in first stack */
	{ PRESS,   MOD,            XK_h,      setsplit,     {.f = -0.01} },   /* increase master area */
	{ PRESS,   MOD,            XK_l,      setsplit,     {.f = +0.01} },   /* decrease master area */
	{ PRESS,   MOD|SHIFT,      XK_equal,  setgappx,     {.i = 0} },       /* reset gap size */
	{ PRESS,   MOD,            XK_equal,  setgappx,     {.i = +2} },      /* increase gap size */
	{ PRESS,   MOD,            XK_minus,  setgappx,     {.i = -2} },      /* decrease gap size */
	{ PRESS,   MOD|SHIFT|CTRL, XK_equal,  setborderpx,  {.i = 0} },       /* reset gap size */
	{ PRESS,   MOD|CTRL,       XK_equal,  setborderpx,  {.i = +2} },      /* increase gap size */
	{ PRESS,   MOD|CTRL,       XK_minus,  setborderpx,  {.i = -2} },      /* decrease gap size */
	{ PRESS,   MOD,            XK_t,      setlayout,    {.v = tile} },    /* set active workspace tiled */
	{ PRESS,   MOD,            XK_m,      setlayout,    {.v = monocle} }, /* set active workspace monocle */
	{ PRESS,   MOD,            XK_f,      setlayout,    {.v = NULL} },    /* set active workspace floating */
	WSBIND(0, XK_1), WSBIND(1, XK_2), WSBIND(2, XK_3), WSBIND(3, XK_4), WSBIND(4, XK_5),
	WSBIND(5, XK_6), WSBIND(6, XK_7), WSBIND(7, XK_8), WSBIND(8, XK_9), WSBIND(9, XK_0),
};
