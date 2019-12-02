/*
* In order to compile you will need the xcb headers, then run
*     cc wm.c -Wall -o wm -lxcb -lxcb-keysyms -lxcb-util
*/

#include <err.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <sys/wait.h>

#include <xcb/xcb.h>
/* #include <xcb/randr.h> */
/* #include <xcb/xproto.h> */
#include <xcb/xcb_util.h>
/* #include <xcb/xcb_atom.h> */
/* #include <xcb/xcb_icccm.h> */
#include <xcb/xcb_keysyms.h>

#include <X11/keysym.h>
#include <X11/cursorfont.h>

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

enum { INACTIVE, ACTIVE };
enum { CurNormal, CurMove, CurResize, CurLast };

unsigned int numlockmask = 0;

static xcb_connection_t  *conn;
static xcb_screen_t      *scr;
static xcb_window_t      focuswin;
static xcb_window_t      root;
static xcb_key_symbols_t *syms = NULL;
static xcb_cursor_t      cursor[CurLast];
static xcb_generic_error_t *error = NULL;

static void deploy(void);
static void cleanup(void);
static void spawn(const Arg *arg);
static void focus(xcb_window_t, int);
static void subscribe(xcb_window_t);
static void quit(const Arg *arg);

const Key keys[] = {
	/* modifier                  key        function   arg */
	{ MODKEY|XCB_MOD_MASK_SHIFT, XK_q,      quit,     {0} },
	{ MODKEY|XCB_MOD_MASK_SHIFT, XK_Return, spawn,    {.v = (char *[]){"st", NULL}} },
	{ MODKEY,                    XK_p,      spawn,    {.v = (char *[]){"dmenu_run", NULL}} },
};

static void testerr(const char* file, const int line)
{
	if (error) {
		fprintf(stderr, "%s:%d - request returned error %i, \"%s\"\n", file, line, (int)error->error_code, xcb_event_get_error_label(error->error_code));
		free(error);
		error = NULL;
		assert(0);
	}
}

static void sigchld(int unused)
{
	if (signal(SIGCHLD, sigchld) == SIG_ERR)
		errx(1, "can't install SIGCHLD handler");
	while(0 < waitpid(-1, NULL, WNOHANG));
}

static void checkotherwm(void)
{ /* this should cause an error if some other window manager is running */
	uint32_t values[] = { XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT };
	xcb_void_cookie_t wm_cookie = xcb_change_window_attributes_checked(conn, root, XCB_CW_EVENT_MASK, values);
	if (xcb_request_check(conn, wm_cookie))
		errx(1, "another window manager is already running");
}

static void quit(const Arg *arg)
{
	cleanup();
	xcb_disconnect(conn);
	exit(0);
}

static void updatenumlockmask(void)
{ /* taken from i3 */
	xcb_get_modifier_mapping_reply_t *reply = xcb_get_modifier_mapping_reply(conn, xcb_get_modifier_mapping(conn), &error);
	testerr(__FILE__, __LINE__);
	xcb_keycode_t *codes = xcb_get_modifier_mapping_keycodes(reply);
	xcb_keycode_t target, *temp;
	unsigned int i, j;

	if ((temp = xcb_key_symbols_get_keycode(syms, XK_Num_Lock))) {
		target = *temp;
		free(temp);
	} else {
		return;
	}
	for (i = 0; i < 8; i++)
		for (j = 0; j < reply->keycodes_per_modifier; j++)
			if(codes[i * reply->keycodes_per_modifier + j] == target)
				numlockmask = (1 << i);
	free(reply);
}

static void grabbuttons(void)
{
	xcb_ungrab_button(conn, XCB_BUTTON_INDEX_ANY, root, XCB_GRAB_ANY);
	xcb_grab_button(conn, 0, root, BUTTONMASK, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, root, XCB_NONE, 1, MODKEY);
	xcb_grab_button(conn, 0, root, BUTTONMASK, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, root, XCB_NONE, 3, MODKEY);
}

static void grabkeys(void)
{
	xcb_keycode_t *c;
	unsigned int mods[] = { 0, XCB_MOD_MASK_LOCK, numlockmask, numlockmask|XCB_MOD_MASK_LOCK };

	xcb_ungrab_key(conn, XCB_GRAB_ANY, root, XCB_MOD_MASK_ANY);
	for (unsigned int i = 0; i < LEN(keys); i++)
		if ((c = xcb_key_symbols_get_keycode(syms, keys[i].keysym))) {
			for (unsigned int j = 0; j < LEN(mods); j++)
				xcb_grab_key(conn, 1, root, keys[i].mod|mods[j], *c, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
			free(c);
		}
}

static int keypress(xcb_generic_event_t *e)
{
	xcb_key_press_event_t *ev = (xcb_key_press_event_t*)e;
	xcb_keysym_t sym = xcb_key_press_lookup_keysym(syms, ev, 0);

	for (unsigned int i = 0; i < LEN(keys); i++)
		if (sym == keys[i].keysym && KEY(keys[i].mod) == KEY(ev->state) && keys[i].func)
			keys[i].func(&(keys[i].arg));
	return 0;
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

static void initcursor(void)
{
	xcb_font_t cursor_font = xcb_generate_id(conn);
	xcb_open_font(conn, cursor_font, strlen("cursor"), "cursor");
	cursor[CurNormal] = xcb_generate_id(conn);
	xcb_create_glyph_cursor(conn, cursor[CurNormal], cursor_font, cursor_font, XC_left_ptr, XC_left_ptr+1, 0, 0, 0, 65535, 63353, 63353);
	cursor[CurResize] = xcb_generate_id(conn);
	xcb_create_glyph_cursor(conn, cursor[CurResize], cursor_font, cursor_font, XC_sizing, XC_sizing+1, 0, 0, 0, 65535, 63353, 63353);
	cursor[CurMove] = xcb_generate_id(conn);
	xcb_create_glyph_cursor(conn, cursor[CurMove], cursor_font, cursor_font, XC_fleur, XC_fleur+1, 0, 0, 0, 65535, 63353, 63353);
	xcb_close_font(conn, cursor_font);
}

static void cleanup(void)
{
	xcb_key_symbols_free(syms);
	xcb_free_cursor(conn, cursor[CurNormal]);
	xcb_free_cursor(conn, cursor[CurResize]);
	xcb_free_cursor(conn, cursor[CurMove]);

	if (conn != NULL)
		xcb_disconnect(conn);
}

static void deploy(void)
{
	sigchld(0);
	initcursor();
	focuswin = scr->root;
	syms = xcb_key_symbols_alloc(conn);
	uint32_t cw_values[] = { XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY, cursor[CurNormal] };
	xcb_void_cookie_t cookie = xcb_change_window_attributes_checked(conn, root, XCB_CW_EVENT_MASK|XCB_CW_CURSOR, cw_values);
	if ((error = xcb_request_check(conn, cookie)))
		testerr(__FILE__, __LINE__);
	updatenumlockmask();
	grabkeys();
	grabbuttons();
	xcb_flush(conn);
}

static void focus(xcb_window_t win, int mode)
{
	uint32_t values[1];
	values[0] = mode ? FOCUSCOL : UNFOCUSCOL;
	xcb_change_window_attributes(conn, win, XCB_CW_BORDER_PIXEL, values);
	if (mode == ACTIVE) {
		xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, win, XCB_CURRENT_TIME);
		if (win != focuswin) {
			focus(focuswin, INACTIVE);
			focuswin = win;
		}
	}
}

static void subscribe(xcb_window_t win)
{
	uint32_t values[2];

	values[0] = XCB_EVENT_MASK_ENTER_WINDOW;
	values[1] = XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;
	xcb_change_window_attributes(conn, win, XCB_CW_EVENT_MASK, values);
	values[0] = BORDER;
	xcb_configure_window(conn, win, XCB_CONFIG_WINDOW_BORDER_WIDTH, values);
}

static void event_loop(void)
{
	uint32_t values[3];
	xcb_window_t win = 0;
	xcb_generic_event_t *ev;
	xcb_get_geometry_reply_t *geom;

	while ((ev = xcb_wait_for_event(conn))) {
		switch (ev->response_type & ~0x80) {
			case XCB_CREATE_NOTIFY: {
				xcb_create_notify_event_t *e = (xcb_create_notify_event_t *)ev;
				if (e && !e->override_redirect) {
					subscribe(e->window);
					focus(e->window, ACTIVE);
				}
				break;
			}
			case XCB_DESTROY_NOTIFY: {
				xcb_destroy_notify_event_t *e = (xcb_destroy_notify_event_t *)ev;
				xcb_kill_client(conn, e->window);
				break;
			}
			case XCB_ENTER_NOTIFY: {
				xcb_enter_notify_event_t *e = (xcb_enter_notify_event_t *)ev;
				focus(e->event, ACTIVE);
				break;
			}
			case XCB_MAP_NOTIFY: {
				xcb_map_notify_event_t *e = (xcb_map_notify_event_t *)ev;
				if (!e->override_redirect) {
					xcb_map_window(conn, e->window);
					focus(e->window, ACTIVE);
				}
				break;
			}
			case XCB_KEY_PRESS:
				keypress(ev);
				break;
			case XCB_BUTTON_PRESS: {
				xcb_button_press_event_t *b = (xcb_button_press_event_t *)ev;
				win = b->child;
				if (!win || win == scr->root)
					break;
				values[0] = XCB_STACK_MODE_ABOVE;
				xcb_configure_window(conn, win, XCB_CONFIG_WINDOW_STACK_MODE, values);
				geom = xcb_get_geometry_reply(conn, xcb_get_geometry(conn, win), NULL);
				if (b->detail == 1) {
					values[2] = 1;
					xcb_warp_pointer(conn, XCB_NONE, win, 0, 0, 0, 0, geom->width/2, geom->height/2);
				} else {
					values[2] = 3;
					xcb_warp_pointer(conn, XCB_NONE, win, 0, 0, 0, 0, geom->width, geom->height);
				}
				xcb_grab_pointer(conn, 0, scr->root, MOTIONMASK, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
						scr->root, cursor[b->detail == 1 ? CurMove : CurResize], XCB_CURRENT_TIME);
				xcb_flush(conn);
				break;
			}
			case XCB_MOTION_NOTIFY: {
				xcb_query_pointer_reply_t *p = xcb_query_pointer_reply(conn, xcb_query_pointer(conn, scr->root), 0);
				if (values[2] == 1) {
					geom = xcb_get_geometry_reply(conn, xcb_get_geometry(conn, win), NULL);
					if (!geom)
						break;
					values[0] = (p->root_x + geom->width / 2 > scr->width_in_pixels - (BORDER*2))
						? scr->width_in_pixels - geom->width - (BORDER*2)
						: p->root_x - geom->width / 2;
					values[1] = (p->root_y + geom->height / 2 > scr->height_in_pixels - (BORDER*2))
						? (scr->height_in_pixels - geom->height - (BORDER*2))
						: p->root_y - geom->height / 2;
					if (p->root_x < geom->width/2)
						values[0] = 0;
					if (p->root_y < geom->height/2)
						values[1] = 0;
					xcb_configure_window(conn, win, XCB_CONFIG_WINDOW_X|XCB_CONFIG_WINDOW_Y, values);
					xcb_flush(conn);
				} else if (values[2] == 3) {
					geom = xcb_get_geometry_reply(conn, xcb_get_geometry(conn, win), NULL);
					values[0] = p->root_x - geom->x;
					values[1] = p->root_y - geom->y;
					xcb_configure_window(conn, win, XCB_CONFIG_WINDOW_WIDTH|XCB_CONFIG_WINDOW_HEIGHT, values);
					xcb_flush(conn);
				}
				break;
			}
			case XCB_BUTTON_RELEASE:
				focus(win, ACTIVE);
				xcb_ungrab_pointer(conn, XCB_CURRENT_TIME);
				break;
			case XCB_CONFIGURE_NOTIFY: {
				xcb_configure_notify_event_t *e = (xcb_configure_notify_event_t *)ev;
				if (e->window != focuswin)
					focus(e->window, INACTIVE);
				focus(focuswin, ACTIVE);
				break;
			}
		}
		xcb_flush(conn);
		free(ev);
	}
	errx(1, "xcb connection broken");
}

int main(void)
{
	atexit(cleanup);
	if (xcb_connection_has_error((conn = xcb_connect(NULL, NULL))))
		errx(1, "error connecting to X");
	scr = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
	root = scr->root;
	checkotherwm();
	deploy();
	event_loop();
	return 1;
}
