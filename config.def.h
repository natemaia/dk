/* In order to configure yaxwm, copy this file to config.h and edit it */

#define MODKEY    XCB_MOD_MASK_1 /* modifier for most binds including mouse move/resize */
#define XK_VolDn  0x1008ff11     /* dedicated volume down on most keyboards */
#define XK_VolTg  0x1008ff12     /* dedicated mute toggle on most keyboards */
#define XK_VolUp  0x1008ff13     /* dedicated volume up on most keyboards */

/* save some repetitive typing with the workspace keybinds */
#define WSKEYS(ws, key) { XCB_KEY_PRESS, MODKEY,                      key, view,   {.ui = ws} },\
                        { XCB_KEY_PRESS, MODKEY|XCB_MOD_MASK_SHIFT,   key, send,   {.ui = ws} },\
                        { XCB_KEY_PRESS, MODKEY|XCB_MOD_MASK_CONTROL, key, follow, {.ui = ws} }

/* number of clients in the master area, min/max 0-? */
static uint nmaster = 1;

/* ratio of space between master/stack, min/max 0.1-0.9 */
static float splitratio = 0.5;

/* names of workspaces and how many to have, default is 10 */
static char *workspaces[] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9" };

/* borders width and colour, 256 colour or hex 0x000000-0xffffff */
static int borders[] = { [Width] = 1, [Focus] = 0x6699cc, [Unfocus] = 0x000000 };

/* layout functions, first is default */
static void (*layouts[])(Workspace *) = { tile, NULL };

/* client rule matching, when monitor and workspace are -1 then use the current workspace
 *                       when workspace is > -1 and monitor is > -1 the workspace takes precedence
 *                       when workspace is -1 and monitor is > -1 use the active workspace on that monitor
 * eg.
 * { "regex",  NULL,           -1, 0 },  - set to the current workspace, tiled
 * { "regex", "HDMI-A-0",       5, 1 },  - set to workspace 5 regardless what monitor it's on, floating
 * { "regex", "DisplayPort-0", -1, 1 },  - set to the active workspace on monitor DisplayPort-0, floating
 */
static Rule rules[] = {
	/* window class/instance regex (case insensitive)        monitor  workspace  floating */
	{ "^(steam|lxappearance|pavucontrol|transmission-gtk)$",  NULL,      -1,        1 },
};

static Bind binds[] = {
	/* type            modifier(s)                keysym     function      arg */
	{ XCB_KEY_PRESS,   MODKEY,                    XK_q,      killclient,   {0} },
	{ XCB_KEY_PRESS,   MODKEY,                    XK_Tab,    swapclient,   {0} },
	{ XCB_KEY_PRESS,   MODKEY|XCB_MOD_MASK_SHIFT, XK_space,  togglefloat,  {0} },
	{ XCB_KEY_PRESS,   MODKEY|XCB_MOD_MASK_SHIFT, XK_q,      resetorquit,  {.i = 0} },
	{ XCB_KEY_PRESS,   MODKEY|XCB_MOD_MASK_SHIFT, XK_r,      resetorquit,  {.i = 1} },
	{ XCB_KEY_PRESS,   MODKEY,                    XK_j,      changefocus,  {.i = +1} },
	{ XCB_KEY_PRESS,   MODKEY,                    XK_k,      changefocus,  {.i = -1} },
	{ XCB_KEY_PRESS,   MODKEY,                    XK_i,      setnmaster,   {.i = +1} },
	{ XCB_KEY_PRESS,   MODKEY,                    XK_d,      setnmaster,   {.i = -1} },
	{ XCB_KEY_PRESS,   MODKEY,                    XK_h,      setsplit,     {.f = -0.01} },
	{ XCB_KEY_PRESS,   MODKEY,                    XK_l,      setsplit,     {.f = +0.01} },
	{ XCB_KEY_PRESS,   MODKEY,                    XK_t,      setlayout,    {.v = &layouts[0]} },
	{ XCB_KEY_PRESS,   MODKEY,                    XK_f,      setlayout,    {.v = &layouts[1]} },
	{ XCB_KEY_PRESS,   MODKEY|XCB_MOD_MASK_SHIFT, XK_Return, runcmd,       {.v = (char *[]){"st", NULL}} },
	{ XCB_KEY_PRESS,   MODKEY,                    XK_Print,  runcmd,       {.v = (char *[]){"scrot", NULL}} },
	{ XCB_KEY_PRESS,   MODKEY,                    XK_p,      runcmd,       {.v = (char *[]){"dmenu_run", NULL}} },
	{ XCB_KEY_RELEASE, MODKEY|XCB_MOD_MASK_SHIFT, XK_Print,  runcmd,       {.v = (char *[]){"scrot", "-s", NULL}} },
	{ XCB_KEY_PRESS,   0,                         XK_VolTg,  runcmd,       {.v = (char *[]){"pamixer", "-t", NULL}} },
	{ XCB_KEY_PRESS,   0,                         XK_VolUp,  runcmd,       {.v = (char *[]){"pamixer", "-i", "2", NULL}} },
	{ XCB_KEY_PRESS,   0,                         XK_VolDn,  runcmd,       {.v = (char *[]){"pamixer", "-d", "2", NULL}} },
	WSKEYS(0, XK_1), WSKEYS(1, XK_2), WSKEYS(2, XK_3), WSKEYS(3, XK_4), WSKEYS(4, XK_5),
	WSKEYS(5, XK_6), WSKEYS(6, XK_7), WSKEYS(7, XK_8), WSKEYS(8, XK_9), WSKEYS(9, XK_0),
};
