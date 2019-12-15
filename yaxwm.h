#define MODKEY    XCB_MOD_MASK_1 /* modifier used for most binds */
#define XK_VolDn  0x1008ff11     /* dedicated volume down on most keyboards, not defined by X11/keysym.h */
#define XK_VolTg  0x1008ff12     /* dedicated mute toggle on most keyboards, not defined by X11/keysym.h */
#define XK_VolUp  0x1008ff13     /* dedicated volume up on most keyboards, not defined by X11/keysym.h */

/* save some repetitive typing */
#define WSKEYS(ws, key) { XCB_KEY_PRESS, MODKEY,                      key, view,   {.ui = ws} },\
                        { XCB_KEY_PRESS, MODKEY|XCB_MOD_MASK_SHIFT,   key, send,   {.ui = ws} },\
                        { XCB_KEY_PRESS, MODKEY|XCB_MOD_MASK_CONTROL, key, follow, {.ui = ws} }

static uint       nmaster    = 1;    /* number of clients in the master area, min/max 0-? */
static float      splitratio = 0.5;  /* ratio of space between master/stack, min/max 0.1-0.9 */
static const uint nworkspace = 10;   /* number of workspace or virtual desktops, default 0-9 */

static const uint border     = 2;        /* window border width in pixels 0-? */
static const int  focuscol   = 0x4682b4; /* focused border colour,   hex 0x000000-0xffffff */
static const int  unfocuscol = 0x000000; /* unfocused border colour, hex 0x000000-0xffffff */

static Rule rules[] = {
	/* window class regex match                            workspace  floating    monitor,  compiled regex */
	{ "(Steam|Lxappearance|Pavucontrol|Transmission-gtk)", -1,         1,          -1,            {0} },
};

static const Layout layouts[] = {
	{ tile }, { NULL }, /* layout functions, first is default */
};

static Key keys[] = {
	/* type          modifier(s)                keysym     function  arg */
	{ XCB_KEY_PRESS, MODKEY,                    XK_q,      killc,    {0} },
	{ XCB_KEY_PRESS, MODKEY|XCB_MOD_MASK_SHIFT, XK_space,  floatc,   {0} },
	{ XCB_KEY_PRESS, MODKEY,                    XK_Tab,    swap,     {0} },
	{ XCB_KEY_PRESS, MODKEY|XCB_MOD_MASK_SHIFT, XK_q,      quitres,  {.i = 0} },
	{ XCB_KEY_PRESS, MODKEY|XCB_MOD_MASK_SHIFT, XK_r,      quitres,  {.i = 1} },
	{ XCB_KEY_PRESS, MODKEY,                    XK_j,      setfocus, {.i = +1} },
	{ XCB_KEY_PRESS, MODKEY,                    XK_k,      setfocus, {.i = -1} },
	{ XCB_KEY_PRESS, MODKEY,                    XK_h,      setsplit, {.f = -0.01} },
	{ XCB_KEY_PRESS, MODKEY,                    XK_l,      setsplit, {.f = +0.01} },
	{ XCB_KEY_PRESS, MODKEY,                    XK_t,      layout,   {.v = &layouts[0]} },
	{ XCB_KEY_PRESS, MODKEY,                    XK_f,      layout,   {.v = &layouts[1]} },
	{ XCB_KEY_PRESS, MODKEY|XCB_MOD_MASK_SHIFT, XK_Return, runcmd,   {.v = (char *[]){"st", NULL}} },
	{ XCB_KEY_PRESS, MODKEY,                    XK_p,      runcmd,   {.v = (char *[]){"dmenu_run", NULL}} },
	{ XCB_KEY_PRESS, 0,                         XK_VolTg,  runcmd,   {.v = (char *[]){"pamixer", "-t", NULL}} },
	{ XCB_KEY_PRESS, 0,                         XK_VolUp,  runcmd,   {.v = (char *[]){"pamixer", "-i", "2", NULL}} },
	{ XCB_KEY_PRESS, 0,                         XK_VolDn,  runcmd,   {.v = (char *[]){"pamixer", "-d", "2", NULL}} },
	WSKEYS(0, XK_1), WSKEYS(1, XK_2), WSKEYS(2, XK_3), WSKEYS(3, XK_4), WSKEYS(4, XK_5),
	WSKEYS(5, XK_6), WSKEYS(6, XK_7), WSKEYS(7, XK_8), WSKEYS(8, XK_9), WSKEYS(9, XK_0),
};
