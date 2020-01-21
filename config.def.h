/* In order to configure yaxwm, copy this file to config.h and edit it */

static WorkspaceDefault default_workspaces[] = {
	/* name,  nmaster,  splitratio,   layout function (NULL is floating) */
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
	 * class/instance                      monitor     workspace  floating,                                  compiled regex
	 * eg.
	 * { "chromium",                        "HDMI-A-0", -1,         1,                                            {0} }, */
	{ "^firefox$",                          "0",        -1,         0, /* active workspace on monitor 0, tiled */ {0} },
	{ "^gimp$",                             NULL,        2,         1, /* workspace 2, floating */                {0} },
	{ "^(steam|lxappearance)$",             NULL,       -1,         1, /* current workspace, floating */          {0} },
	{ "^(pavucontrol|transmission-gtk)$",   NULL,       -1,         1, /* current workspace, floating */          {0} },
};

static int borders[] = {
	[Width] = 1,         /* border width in pixels */
	[Focus] = 0x6699cc,  /* focused and unfocused border colours, 0-256 colour or hex 0x000000-0xffffff */
	[Unfocus] = 0x000000
};

/* commands */
static const char *term[] = { "st", NULL };
static const char *menu[] = { "dmenu_run", NULL };
static const char *scrot[] = { "scrot", NULL };
static const char *voltg[] = { "pamixer", "-t", NULL };
static const char *volup[] = { "pamixer", "-i", "2", NULL };
static const char *voldn[] = { "pamixer", "-d", "2", NULL };

/* modifier for most binds including mouse move/resize */
#define MODKEY    XCB_MOD_MASK_1

/* media keys (not defined in X11/keysyms.h) */
#define XK_VolTg  0x1008ff12  /* dedicated mute toggle on most keyboards */
#define XK_VolUp  0x1008ff13  /* dedicated volume up on most keyboards */
#define XK_VolDn  0x1008ff11  /* dedicated volume down on most keyboards */

/* save some repetitive typing with the workspace keybinds */
#define WSKEYS(ws, key) { MODKEY,                      key, view,   {.ui = ws} },\
                        { MODKEY|XCB_MOD_MASK_SHIFT,   key, send,   {.ui = ws} },\
                        { MODKEY|XCB_MOD_MASK_CONTROL, key, follow, {.ui = ws} }

static Bind binds[] = {
	/* modifier(s)               keysym     function      arg */
	{ MODKEY|XCB_MOD_MASK_SHIFT, XK_Return, runcmd,       {.v = term} },
	{ MODKEY,                    XK_p,      runcmd,       {.v = menu} },
	{ MODKEY,                    XK_Print,  runcmd,       {.v = scrot} },
	{ 0,                         XK_VolTg,  runcmd,       {.v = voltg} },
	{ 0,                         XK_VolUp,  runcmd,       {.v = volup} },
	{ 0,                         XK_VolDn,  runcmd,       {.v = voldn} },
	{ MODKEY,                    XK_q,      killclient,   {0} },
	{ MODKEY,                    XK_Tab,    swapclient,   {0} },
	{ MODKEY|XCB_MOD_MASK_SHIFT, XK_space,  togglefloat,  {0} },
	{ MODKEY|XCB_MOD_MASK_SHIFT, XK_q,      resetorquit,  {.i = 0} },
	{ MODKEY|XCB_MOD_MASK_SHIFT, XK_r,      resetorquit,  {.i = 1} },
	{ MODKEY,                    XK_j,      changefocus,  {.i = +1} },
	{ MODKEY,                    XK_k,      changefocus,  {.i = -1} },
	{ MODKEY,                    XK_i,      setnmaster,   {.i = +1} },
	{ MODKEY,                    XK_d,      setnmaster,   {.i = -1} },
	{ MODKEY,                    XK_h,      setsplit,     {.f = -0.01} },
	{ MODKEY,                    XK_l,      setsplit,     {.f = +0.01} },
	{ MODKEY,                    XK_t,      setlayout,    {.v = tile} },
	{ MODKEY,                    XK_f,      setlayout,    {.v = NULL} },
	WSKEYS(0, XK_1),
	WSKEYS(1, XK_2),
	WSKEYS(2, XK_3),
	WSKEYS(3, XK_4),
	WSKEYS(4, XK_5),
	WSKEYS(5, XK_6),
	WSKEYS(6, XK_7),
	WSKEYS(7, XK_8),
	WSKEYS(8, XK_9),
	WSKEYS(9, XK_0),
};

/* vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
*/
