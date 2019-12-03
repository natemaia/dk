/*
* In order to compile you will need the xcb headers, then run
*     cc wm.c -Wall -o wm -lxcb -lxcb-keysyms -lxcb-util
*/

#include <err.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <locale.h>
#include <sys/wait.h>
#include <xcb/xcb.h>
#include <xcb/xcb_util.h>
#include <xcb/xcb_cursor.h>
#include <xcb/xcb_keysyms.h>
/* #include <xcb/randr.h> */
/* #include <xcb/xproto.h> */
/* #include <xcb/xcb_atom.h> */
/* #include <xcb/xcb_icccm.h> */
/* for whatever reason xcb_keysyms.h doesn't have any actual keysym definitions */
#include <X11/keysym.h>

#define BORDER        1
#define FOCUSCOL      0x999999
#define UNFOCUSCOL    0x000000
#define MODKEY        XCB_MOD_MASK_1
#define LEN(x)        (sizeof(x) / sizeof(x[0]))
#define KEY(k)        (k & ~(numlockmask|XCB_MOD_MASK_LOCK))
#define BUTTONMASK    XCB_EVENT_MASK_BUTTON_PRESS|XCB_EVENT_MASK_BUTTON_RELEASE
#define MOTIONMASK    XCB_EVENT_MASK_BUTTON_RELEASE|XCB_EVENT_MASK_BUTTON_MOTION|XCB_EVENT_MASK_POINTER_MOTION_HINT

typedef union Arg Arg;
typedef struct Key Key;
typedef struct Event Event;
typedef struct Client Client;

union Arg {
	int i;
	unsigned int ui;
	float f;
	const void *v;
};

struct Key {
	unsigned int mod;
	xcb_keysym_t keysym;
	void (*func)(const Arg *);
	const Arg arg;
};

struct Event {
	uint32_t event;	
	int (*func)(xcb_generic_event_t *e);
};

struct Client {
	float mina, maxa;
	int x, y, w, h, bw;
	int oldx, oldy, oldw, oldh, oldbw;
	int basew, baseh, incw, inch, maxw, maxh, minw, minh;
	int workspace, isfixed, isfloating, isurgent, oldstate;
	Client *next, *snext;
	/* Monitor *mon; */
	xcb_window_t win;
};

enum { INACTIVE, ACTIVE };
enum { Normal, Move, Resize, CurLast };

char *argv0;
int running = 1;
uint32_t ptrv[3];    /* mouse pointer values */
unsigned int sw, sh; /* screen size in pixels */
unsigned int numlockmask = 0;

static xcb_connection_t  *conn;
static xcb_screen_t      *scr;
static xcb_window_t      root, focuswin, ptrwin = 0;
static xcb_key_symbols_t *syms = NULL;
static xcb_cursor_t      cursor[CurLast];
static xcb_generic_error_t *error = NULL;
static xcb_get_geometry_reply_t *geom;

static void deploy(void);
static void cleanup(void);
static void grabkeys(void);
static void initcursor(void);
static void events_loop(void);
static void checkotherwm(void);
static void focus(xcb_window_t win, int mode);
static void grabbuttons(xcb_window_t win, int focused);
static void initclient(xcb_window_t w, xcb_get_window_attributes_reply_t *wa);

static int focusin(xcb_generic_event_t *ev);
static int keypress(xcb_generic_event_t *ev);
static int maprequest(xcb_generic_event_t *ev);
static int enternotify(xcb_generic_event_t *ev);
static int buttonpress(xcb_generic_event_t *ev);
static int motionnotify(xcb_generic_event_t *ev);
static int destroynotify(xcb_generic_event_t *ev);
static int configurenotify(xcb_generic_event_t *ev);

static void sigchld(int unused);
static void quit(const Arg *arg);
static void spawn(const Arg *arg);
static void reset(const Arg *arg);
static void updatenumlockmask(void);
static void testerr(const char* file, const int line);

const Key keys[] = {
	/* mod(s)                    keysym    function   arg */
	{ MODKEY|XCB_MOD_MASK_SHIFT, XK_q,      quit,     {0} },
	{ MODKEY|XCB_MOD_MASK_SHIFT, XK_r,      reset,    {0} },
	{ MODKEY|XCB_MOD_MASK_SHIFT, XK_Return, spawn,    {.v = (char *[]){"st", NULL}} },
	{ MODKEY,                    XK_p,      spawn,    {.v = (char *[]){"dmenu_run", NULL}} },
};

const Event events[] = {
	{ XCB_FOCUS_IN, focusin },
	{ XCB_KEY_PRESS, keypress },
	{ XCB_MAP_REQUEST, maprequest },
	{ XCB_ENTER_NOTIFY, enternotify },
	{ XCB_BUTTON_PRESS, buttonpress },
	{ XCB_BUTTON_RELEASE, buttonpress },
	{ XCB_MOTION_NOTIFY, motionnotify },
	{ XCB_DESTROY_NOTIFY, destroynotify },
	{ XCB_CONFIGURE_NOTIFY, configurenotify },
	/* { XCB_UNMAP_NOTIFY, unmapnotify }, */                 /* needed to free internal state */
	/* { XCB_CLIENT_MESSAGE, clientmessage }, */             /* handle fullscreen and active window */
	/* { XCB_PROPERTY_NOTIFY, propertynotify }, */           /* update window hints and type window */
	/* { XCB_CONFIGURE_REQUEST, configurerequest }, */       /* modify existing internal state */
	{ XCB_NONE, NULL }
};

static int buttonpress(xcb_generic_event_t *ev)
{
	xcb_button_press_event_t *b = (xcb_button_press_event_t *)ev;
	ptrwin = b->child;
	if (!ptrwin || ptrwin == root)
		return -1;
	if (ptrwin != focuswin)
		focus(ptrwin, ACTIVE);
	if ((ev->response_type & ~0x80) == XCB_BUTTON_RELEASE) {
		xcb_ungrab_pointer(conn, XCB_CURRENT_TIME);
		return -1;
	}
	ptrv[0] = XCB_STACK_MODE_ABOVE;
	xcb_configure_window(conn, ptrwin, XCB_CONFIG_WINDOW_STACK_MODE, ptrv);
	geom = xcb_get_geometry_reply(conn, xcb_get_geometry(conn, ptrwin), NULL);
	if (b->detail == 1) {
		ptrv[2] = 1;
		xcb_warp_pointer(conn, XCB_NONE, ptrwin, 0, 0, 0, 0, geom->width/2, geom->height/2);
		xcb_grab_pointer(conn, 0, root, MOTIONMASK, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, root, cursor[Move], XCB_CURRENT_TIME);
	} else if (b->detail == 3) {
		ptrv[2] = 3;
		xcb_warp_pointer(conn, XCB_NONE, ptrwin, 0, 0, 0, 0, geom->width, geom->height);
		xcb_grab_pointer(conn, 0, root, MOTIONMASK, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, root, cursor[Resize], XCB_CURRENT_TIME);
	}
	return 0;
}

static void cleanup(void)
{
	xcb_key_symbols_free(syms);
	for (int i = 0; i < CurLast; i++)
		xcb_free_cursor(conn, cursor[i]);
	xcb_disconnect(conn);
}

static void checkotherwm(void)
{ /* this should cause an error if some other window manager is running */
	uint32_t v[] = { XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT };

	xcb_void_cookie_t c = xcb_change_window_attributes_checked(conn, root, XCB_CW_EVENT_MASK, v);
	if (xcb_request_check(conn, c))
		errx(1, "another window manager is already running");
}

static int configurenotify(xcb_generic_event_t *ev)
{
	xcb_configure_notify_event_t *e = (xcb_configure_notify_event_t *)ev;
	if (e->window != focuswin)
		focus(e->window, INACTIVE);
	focus(focuswin, ACTIVE);
	return 0;
}

static void deploy(void)
{
	sigchld(0);
	initcursor();
	focuswin = root;
	syms = xcb_key_symbols_alloc(conn);
	uint32_t v[] = {
		XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
		XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
		XCB_EVENT_MASK_BUTTON_PRESS |
		XCB_EVENT_MASK_ENTER_WINDOW |
		XCB_EVENT_MASK_LEAVE_WINDOW |
		XCB_EVENT_MASK_STRUCTURE_NOTIFY |
		XCB_EVENT_MASK_PROPERTY_CHANGE,
		cursor[Normal]
	};
	xcb_void_cookie_t c = xcb_change_window_attributes_checked(conn, root, XCB_CW_EVENT_MASK|XCB_CW_CURSOR, v);
	if ((error = xcb_request_check(conn, c)))
		testerr(__FILE__, __LINE__);
	grabkeys();
}

static int destroynotify(xcb_generic_event_t *ev)
{
	xcb_destroy_notify_event_t *e = (xcb_destroy_notify_event_t *)ev;
	xcb_kill_client(conn, e->window);
	return 0;
}

static int enternotify(xcb_generic_event_t *ev)
{
	xcb_enter_notify_event_t *e = (xcb_enter_notify_event_t *)ev;
	focus(e->event, ACTIVE);
	return 0;
}

static void events_loop(void)
{
	const Event *e;
	xcb_generic_event_t *ev;

	while (running && (ev = xcb_wait_for_event(conn))) {
		for (e = events; e->func != NULL; e++)
			if ((ev->response_type & ~0x80) == e->event)
				e->func(ev);
		xcb_flush(conn);
		free(ev);
	}
}

static void focus(xcb_window_t win, int mode)
{
	uint32_t v[] = { mode ? FOCUSCOL : UNFOCUSCOL };

	xcb_change_window_attributes(conn, win, XCB_CW_BORDER_PIXEL, v);
	if (mode) {
		grabbuttons(win, 1);
		xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, win, XCB_CURRENT_TIME);
		if (win != focuswin) {
			focus(focuswin, INACTIVE);
			focuswin = win;
		}
	}
}

static int focusin(xcb_generic_event_t *ev)
{
	xcb_focus_in_event_t *e = (xcb_focus_in_event_t *)ev;
	focus(e->event, ACTIVE);
	return 0;
}

static void grabbuttons(xcb_window_t win, int focused)
{
	updatenumlockmask();
	{ /* new scope to use updated numlockmask */
		unsigned int btns[] = { XCB_BUTTON_INDEX_1, XCB_BUTTON_INDEX_2, XCB_BUTTON_INDEX_3 };
		unsigned int mods[] = { 0, XCB_MOD_MASK_LOCK, numlockmask, numlockmask|XCB_MOD_MASK_LOCK };

		xcb_ungrab_button(conn, XCB_BUTTON_INDEX_ANY, win, XCB_GRAB_ANY);
		if (!focused)
			xcb_grab_button(conn, 0, win, BUTTONMASK, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, 0, XCB_NONE, XCB_BUTTON_INDEX_ANY, XCB_MOD_MASK_ANY);
		for (unsigned int i = 0; i < LEN(btns); i++)
			for (unsigned int j = 0; j < LEN(mods); j++)
				xcb_grab_button(conn, 0, win, BUTTONMASK, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, 0, XCB_NONE, btns[i], MODKEY|mods[j]);
	}
}

static void grabkeys(void)
{
	xcb_keycode_t *c;

	updatenumlockmask();
	{ /* new scope to use updated numlockmask */
		unsigned int mods[] = { 0, XCB_MOD_MASK_LOCK, numlockmask, numlockmask|XCB_MOD_MASK_LOCK };

		xcb_ungrab_key(conn, XCB_GRAB_ANY, root, XCB_MOD_MASK_ANY);
		for (unsigned int i = 0; i < LEN(keys); i++)
			if ((c = xcb_key_symbols_get_keycode(syms, keys[i].keysym))) {
				for (unsigned int j = 0; j < LEN(mods); j++)
					xcb_grab_key(conn, 1, root, keys[i].mod|mods[j], *c, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
				free(c);
			}
	}
}

static void initcursor(void)
{
	xcb_cursor_context_t *ctx;
	static const char *c[] = { [Normal] = "arrow", [Move] = "fleur", [Resize] = "sizing" };
	if (xcb_cursor_context_new(conn, scr, &ctx) >= 0) {
		for (int i = 0; i < CurLast; i++)
			cursor[i] = xcb_cursor_load_cursor(ctx, c[i]);
		xcb_cursor_context_free(ctx);
	}
}

static int keypress(xcb_generic_event_t *ev)
{
	xcb_key_press_event_t *e = (xcb_key_press_event_t*)ev;
	xcb_keysym_t sym = xcb_key_press_lookup_keysym(syms, e, 0);

	for (unsigned int i = 0; i < LEN(keys); i++)
		if (sym == keys[i].keysym && KEY(keys[i].mod) == KEY(e->state) && keys[i].func) {
			keys[i].func(&(keys[i].arg));
			break;
		}
	return 0;
}

static void initclient(xcb_window_t w, xcb_get_window_attributes_reply_t *wa)
{
	uint32_t b[] = { BORDER };
	uint32_t v[] = { XCB_EVENT_MASK_ENTER_WINDOW|XCB_EVENT_MASK_FOCUS_CHANGE|XCB_EVENT_MASK_STRUCTURE_NOTIFY }; /* XCB_EVENT_MASK_PROPERTY_CHANGE */

	xcb_configure_window(conn, w, XCB_CONFIG_WINDOW_BORDER_WIDTH, b);
	xcb_change_window_attributes(conn, w, XCB_CW_EVENT_MASK, v);
	grabbuttons(w, 0);
	xcb_map_window(conn, w);
	focus(w, ACTIVE);
}

static int maprequest(xcb_generic_event_t *ev)
{
	xcb_map_request_event_t *e = (xcb_map_request_event_t *)ev;
	xcb_get_window_attributes_cookie_t c = xcb_get_window_attributes(conn, e->window);
	xcb_get_window_attributes_reply_t *wa = xcb_get_window_attributes_reply(conn, c, &error);
	testerr(__FILE__, __LINE__);
	if (!wa || wa->override_redirect)
		return -1;
	initclient(e->window, wa);
	return 0;
}

static int motionnotify(xcb_generic_event_t *ev)
{
	if (ptrv[2] == 1 || ptrv[2] == 3) {
		xcb_query_pointer_reply_t *p = xcb_query_pointer_reply(conn, xcb_query_pointer(conn, root), 0);
		if (!(geom = xcb_get_geometry_reply(conn, xcb_get_geometry(conn, ptrwin), NULL)))
			return -1;
		if (ptrv[2] == 1) {
			ptrv[0] = (p->root_x + geom->width / 2 > sw - (BORDER*2)) ? sw - geom->width - (BORDER*2) : p->root_x - geom->width / 2;
			ptrv[1] = (p->root_y + geom->height / 2 > sh - (BORDER*2)) ? (sh - geom->height - (BORDER*2)) : p->root_y - geom->height / 2;
			if (p->root_x < geom->width/2)
				ptrv[0] = 0;
			if (p->root_y < geom->height/2)
				ptrv[1] = 0;
			xcb_configure_window(conn, ptrwin, XCB_CONFIG_WINDOW_X|XCB_CONFIG_WINDOW_Y, ptrv);
		} else {
			ptrv[0] = p->root_x - geom->x;
			ptrv[1] = p->root_y - geom->y;
			xcb_configure_window(conn, ptrwin, XCB_CONFIG_WINDOW_WIDTH|XCB_CONFIG_WINDOW_HEIGHT, ptrv);
		}
	}
	return 0;
}

static void quit(const Arg *arg)
{
	running = 0;
}

static void reset(const Arg *arg)
{
	char *const argv[] = { argv0, NULL };
	execvp(argv[0], argv);
}

static void sigchld(int unused)
{
	if (signal(SIGCHLD, sigchld) == SIG_ERR)
		errx(1, "can't install SIGCHLD handler");
	while(0 < waitpid(-1, NULL, WNOHANG));
}

static void spawn(const Arg *arg)
{
	if (fork() == 0) {
		if (conn)
			close(xcb_get_file_descriptor(conn));
		setsid();
		execvp(((char **)arg->v)[0], (char **)arg->v);
		fprintf(stderr, "dwm: execvp %s", ((char **)arg->v)[0]);
		perror(" failed");
		exit(0);
	}
}

static void testerr(const char* file, const int line)
{
	if (error) {
		fprintf(stderr, "%s:%d - request returned error %i, \"%s\"\n", file, line, (int)error->error_code, xcb_event_get_error_label(error->error_code));
		free(error);
		exit(1);
	}
}

static void updatenumlockmask(void)
{
	unsigned int i, j;
	xcb_keycode_t target, *temp;

	xcb_get_modifier_mapping_reply_t *mods = xcb_get_modifier_mapping_reply(conn, xcb_get_modifier_mapping(conn), &error);
	testerr(__FILE__, __LINE__);
	xcb_keycode_t *codes = xcb_get_modifier_mapping_keycodes(mods);

	if ((temp = xcb_key_symbols_get_keycode(syms, XK_Num_Lock))) {
		target = *temp;
		free(temp);
		for (i = 0; i < 8; i++)
			for (j = 0; j < mods->keycodes_per_modifier; j++)
				if (codes[i * mods->keycodes_per_modifier + j] == target)
					numlockmask = (1 << i);
	}
	free(mods);
}

int main(int argc, char *argv[])
{
	argv0 = argv[0];
	atexit(cleanup);
	if (!setlocale(LC_CTYPE, ""))
		errx(1, "no locale support");
	if (xcb_connection_has_error((conn = xcb_connect(NULL, NULL))))
		errx(1, "error connecting to X");
	scr = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
	root = scr->root;
	sw = scr->width_in_pixels;
	sh = scr->height_in_pixels;
	checkotherwm();
	deploy();
	events_loop();
	cleanup();
	return 0;
}
