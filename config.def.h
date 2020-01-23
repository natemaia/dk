/* In order to configure yaxwm, copy this file to config.h and edit it
 *
 * vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
 */

/* primary modifier for binds including mouse move/resize */
#define MODKEY    XCB_MOD_MASK_1

/* dedicated media keys on many keyboards */
#define MUTE      0x1008ff12
#define VOLUP     0x1008ff13
#define VOLDOWN   0x1008ff11

/* save some repetitive typing with the workspace keybinds */
#define WSBIND(ws, key) { XCB_KEY_PRESS, MODKEY,                      key, view,   {.ui = ws} },\
                        { XCB_KEY_PRESS, MODKEY|XCB_MOD_MASK_SHIFT,   key, send,   {.ui = ws} },\
                        { XCB_KEY_PRESS, MODKEY|XCB_MOD_MASK_CONTROL, key, follow, {.ui = ws} }

/* commands */
static const char *term[] = { "st", NULL };
static const char *menu[] = { "dmenu_run", NULL };
static const char *scrot[] = { "scrot", NULL };
static const char *scrots[] = { "scrot", "-s", NULL };
static const char *voltg[] = { "pamixer", "-t", NULL };
static const char *volup[] = { "pamixer", "-i", "2", NULL };
static const char *voldn[] = { "pamixer", "-d", "2", NULL };

static int borders[] = {
	[Width] = 1,         /* border width in pixels */
	[Focus] = 0x6699cc,  /* focused and unfocused border colours, 0-256 colour or hex 0x000000-0xffffff */
	[Unfocus] = 0x000000,
};

static WsRule wsrules[] = {
	/* workspace default settings and how many to allocate
	 * if more are allocated later the values from the first rule will be used
	 *
	 * name,  nmaster,  splitratio,   layout function (NULL is floating) */
	{ "0",     1,        0.5,          tile },
	{ "1",     1,        0.5,          tile },
	{ "2",     1,        0.5,          tile },
	{ "3",     1,        0.5,          tile },
	{ "4",     1,        0.5,          tile },
	{ "5",     1,        0.5,          tile },
	{ "6",     1,        0.5,          tile },
	{ "7",     1,        0.5,          tile },
	{ "8",     1,        0.5,          tile },
	{ "9",     1,        0.5,          tile },
};

static Rule rules[] = {
	/* In order to get monitor and window info you can use the following commands:
	 * window class/instance: `xprop` (the regex matching is case insensitive)
	 * monitor name: `xrandr` (or use an index 0-n, the order is not guaranteed)
	 *
	 * eg. { "chromium", "HDMI-A-0", -1, 1, {0} }
	 *
	 * class/instance                      monitor     workspace  floating,                                  compiled regex */
	{ "^firefox$",                          "0",        -1,         0, /* active workspace on monitor 0, tiled */ {0} },
	{ "^gimp$",                             NULL,        2,         1, /* workspace 2, floating */                {0} },
	{ "^(steam|lxappearance)$",             NULL,       -1,         1, /* current workspace, floating */          {0} },
	{ "^(pavucontrol|transmission-gtk)$",   NULL,       -1,         1, /* current workspace, floating */          {0} },
};

static Bind binds[] = {
	/* type,             mods,                    keysym,    function,     arg */
	{ XCB_KEY_PRESS,   MODKEY|XCB_MOD_MASK_SHIFT, XK_Return, runcmd,       {.v = term} },
	{ XCB_KEY_PRESS,   MODKEY,                    XK_p,      runcmd,       {.v = menu} },
	{ XCB_KEY_PRESS,   MODKEY,                    XK_Print,  runcmd,       {.v = scrot} },
	{ XCB_KEY_RELEASE, MODKEY|XCB_MOD_MASK_SHIFT, XK_Print,  runcmd,       {.v = scrots} },
	{ XCB_KEY_PRESS,   0,                         MUTE,      runcmd,       {.v = voltg} },
	{ XCB_KEY_PRESS,   0,                         VOLUP,     runcmd,       {.v = volup} },
	{ XCB_KEY_PRESS,   0,                         VOLDOWN,   runcmd,       {.v = voldn} },
	{ XCB_KEY_PRESS,   MODKEY,                    XK_q,      killclient,   {0} },          /* close active window */
	{ XCB_KEY_PRESS,   MODKEY,                    XK_Tab,    swapclient,   {0} },          /* swap window with master and vise versa */
	{ XCB_KEY_PRESS,   MODKEY|XCB_MOD_MASK_SHIFT, XK_space,  togglefloat,  {0} },          /* toggle active window floating state */
	{ XCB_KEY_PRESS,   MODKEY|XCB_MOD_MASK_SHIFT, XK_q,      resetorquit,  {.i = 0} },     /* quit yaxwm */
	{ XCB_KEY_PRESS,   MODKEY|XCB_MOD_MASK_SHIFT, XK_r,      resetorquit,  {.i = 1} },     /* restart yaxwm */
	{ XCB_KEY_PRESS,   MODKEY,                    XK_j,      changefocus,  {.i = +1} },    /* focus next window */
	{ XCB_KEY_PRESS,   MODKEY,                    XK_k,      changefocus,  {.i = -1} },    /* focus previous window */
	{ XCB_KEY_PRESS,   MODKEY,                    XK_i,      setnmaster,   {.i = +1} },    /* increase number of windows in master */
	{ XCB_KEY_PRESS,   MODKEY,                    XK_d,      setnmaster,   {.i = -1} },    /* decrease number of windows in master */
	{ XCB_KEY_PRESS,   MODKEY,                    XK_h,      setsplit,     {.f = -0.01} }, /* increase master area */
	{ XCB_KEY_PRESS,   MODKEY,                    XK_l,      setsplit,     {.f = +0.01} }, /* decrease master area */
	{ XCB_KEY_PRESS,   MODKEY,                    XK_t,      setlayout,    {.v = tile} },  /* set active workspace tiled */
	{ XCB_KEY_PRESS,   MODKEY,                    XK_f,      setlayout,    {.v = NULL} },  /* set active workspace floating */
	WSBIND(0, XK_1), WSBIND(1, XK_2), WSBIND(2, XK_3), WSBIND(3, XK_4), WSBIND(4, XK_5),
	WSBIND(5, XK_6), WSBIND(6, XK_7), WSBIND(7, XK_8), WSBIND(8, XK_9), WSBIND(9, XK_0),
};
