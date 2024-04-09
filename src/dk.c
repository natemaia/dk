/* dk window manager
 *
 * see license file for copyright and license details
 *
 * vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
 */

#define _XOPEN_SOURCE   700
#define _POSIX_C_SOURCE 200112L

#ifdef FUNCDEBUG
#define _GNU_SOURCE
#include <dlfcn.h>
#endif

#include <sys/un.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <regex.h>
#include <signal.h>
#include <fcntl.h>
#include <err.h>

#include <xcb/randr.h>
#include <xcb/xproto.h>
#include <xcb/xcb_util.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_cursor.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xcb_aux.h>
#include <xcb/res.h>

#include "dk.h"
#include "strl.h"
#include "util.h"
#include "parse.h"
#include "layout.h"
#include "event.h"
#include "cmd.h"
#include "config.h"
#include "status.h"

FILE *cmdresp;
char *argv0, sock[256];
uint32_t lockmask = 0;
int running, restart, needsrefresh, status_usingcmdresp, depth;
int scr_h, scr_w, sockfd, randrbase, cmdusemon, winchange, wschange, lytchange;

Desk *desks;
Rule *rules;
Panel *panels;
Status *stats;
Client *cmdc;
Monitor *monitors, *primary, *selmon, *lastmon;
Workspace *workspaces, *setws, *selws, *lastws;

Workspace scratch = {.nmaster = 0,
					 .nstack = 0,
					 .gappx = 0,
					 .smartgap = 0,
					 .num = -1,
					 .padl = 0,
					 .padr = 0,
					 .padt = 0,
					 .padb = 0,
					 .msplit = 0,
					 .ssplit = 0,
					 .layout = &layouts[0],
					 .name = "scratch",
					 .mon = NULL,
					 .next = NULL,
					 .sel = NULL,
					 .stack = NULL,
					 .clients = NULL};

xcb_screen_t *scr;
xcb_connection_t *con;
xcb_window_t root, wmcheck;
xcb_key_symbols_t *keysyms;
xcb_cursor_t cursor[CURS_LAST];
xcb_atom_t wmatom[WM_LAST], netatom[NET_LAST];

static uint32_t rootmask = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
						   XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_ENTER_WINDOW |
						   XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_STRUCTURE_NOTIFY |
						   XCB_EVENT_MASK_PROPERTY_CHANGE;
static uint32_t clientmask = XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_FOCUS_CHANGE |
							 XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY;
const char *ebadarg = "invalid argument for";
const char *enoargs = "command requires additional arguments but none were given";
const char *gravs[] = {
	[GRAV_NONE] = "none",     [GRAV_LEFT] = "left", [GRAV_RIGHT] = "right",
	[GRAV_CENTER] = "center", [GRAV_TOP] = "top",   [GRAV_BOTTOM] = "bottom",
};
const char *dirs[] = {
	[DIR_NEXT] = "next",     [DIR_PREV] = "prev",     [DIR_LAST] = "last",
	[DIR_NEXTNE] = "nextne", [DIR_PREVNE] = "prevne"
};
const char *utf8 = "UTF8-STRING";

const char *wmatoms[] = {
	[WM_DELETE] = "WM_DELETE_WINDOW", [WM_FOCUS] = "WM_TAKE_FOCUS", [WM_MOTIF] = "_MOTIF_WM_HINTS",
	[WM_PROTO] = "WM_PROTOCOLS",      [WM_STATE] = "WM_STATE",      [WM_UTF8STR] = "UTF8_STRING",
};
const char *netatoms[] = {
	[NET_ACTIVE] = "_NET_ACTIVE_WINDOW",
	[NET_CLIENTS] = "_NET_CLIENT_LIST",
	[NET_CLOSE] = "_NET_CLOSE_WINDOW",
	[NET_DESK_CUR] = "_NET_CURRENT_DESKTOP",
	[NET_DESK_GEOM] = "_NET_DESKTOP_GEOMETRY",
	[NET_DESK_NAMES] = "_NET_DESKTOP_NAMES",
	[NET_DESK_NUM] = "_NET_NUMBER_OF_DESKTOPS",
	[NET_DESK_VP] = "_NET_DESKTOP_VIEWPORT",
	[NET_DESK_WA] = "_NET_WORKAREA",
	[NET_STATE_ABOVE] = "_NET_WM_STATE_ABOVE",
	[NET_STATE_DEMANDATT] = "_NET_WM_STATE_DEMANDS_ATTENTION",
	[NET_STATE_FULL] = "_NET_WM_STATE_FULLSCREEN",
	[NET_SUPPORTED] = "_NET_SUPPORTED",
	[NET_TYPE_DESK] = "_NET_WM_WINDOW_TYPE_DESKTOP",
	[NET_TYPE_DIALOG] = "_NET_WM_WINDOW_TYPE_DIALOG",
	[NET_TYPE_DOCK] = "_NET_WM_WINDOW_TYPE_DOCK",
	[NET_TYPE_SPLASH] = "_NET_WM_WINDOW_TYPE_SPLASH",
	[NET_WM_CHECK] = "_NET_SUPPORTING_WM_CHECK",
	[NET_WM_DESK] = "_NET_WM_DESKTOP",
	[NET_WM_NAME] = "_NET_WM_NAME",
	[NET_WM_STATE] = "_NET_WM_STATE",
	[NET_WM_STRUTP] = "_NET_WM_STRUT_PARTIAL",
	[NET_WM_STRUT] = "_NET_WM_STRUT",
	[NET_WM_TYPE] = "_NET_WM_WINDOW_TYPE",
};

static void absorb(Client *p, Client *c);
static Client *absorbingclient(xcb_window_t win);
static void desorb(Client *c);
static int discreteproc(pid_t p, pid_t c);
static void freews(Workspace *ws);
static void initwm(void);
static pid_t parentproc(pid_t p);
static void relocatews(Workspace *ws, Monitor *old, int wasvis);
static int rulecmp(Client *c, Rule *r);
static int savestate(int restore);
static void sighandle(int sig);
static Client *termforwin(const Client *c);
static void updatenetclients(void);
static void updnetworkspaces(void);
static xcb_get_window_attributes_reply_t *winattr(xcb_window_t win);
static void winclass(xcb_window_t win, char *clss, char *inst, size_t len);
static xcb_get_geometry_reply_t *wingeom(xcb_window_t win);
static pid_t winpid(xcb_window_t win);
static int winprop(xcb_window_t win, xcb_atom_t prop, xcb_atom_t *ret);

int main(int argc, char *argv[])
{
	fd_set read_fds;
	xcb_generic_event_t *ev;
	xcb_generic_error_t *e;
	xcb_query_tree_reply_t *rt;
	static struct sockaddr_un addr;
	char *end, buf[PIPE_BUF], *host = NULL;
	int cmdfd, confd, nfds, dsp = 0, scrn = 0;

	/* setup basics */
	argv0 = argv[0];
	randrbase = -1;
	running = 1;
	needsrefresh = 1;
	depth = 0;
	sockfd = 0;
	restart = 0;
	cmdusemon = 0;
	winchange = 0;
	wschange = 0;
	lytchange = 0;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-s")) {
			if (i + 1 >= argc) {
				warnx("-s requires an additional argument");
			} else if (!(sockfd = strtol(argv[++i], &end, 0)) || *end != '\0') {
				warnx("invalid socket file descriptor: %s", argv[i]);
				sockfd = 0;
			}
		} else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "-h")) {
			return usage(argv[0], VERSION, 0, argv[i][1], "[-hv]");
		} else {
			return usage(argv[0], VERSION, 1, 'h', "[-hv]");
		}
	}

	/* setup the xcb connection and root window */
	if (xcb_connection_has_error((con = xcb_connect(NULL, NULL)))) {
		err(1, "error connecting to X");
	}
	atexit(freewm);
	if (!(scr = xcb_setup_roots_iterator(xcb_get_setup(con)).data)) {
		errx(1, "error getting default screen from X connection");
	}
	root = scr->root;
	scr_w = scr->width_in_pixels;
	scr_h = scr->height_in_pixels;
	iferr(1, "is another window manager running?",
		  xcb_request_check(
			  con, xcb_change_window_attributes_checked(con, root, XCB_CW_EVENT_MASK,
														(uint32_t[]){XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT})));
	initwm();

	/* setup the socket connection for commands and status */
	if (sockfd <= 0) {
		if (xcb_parse_display(NULL, &host, &dsp, &scrn)) {
			snprintf(sock, sizeof(sock), "/tmp/dk_%s_%i_%i.socket", host, dsp, scrn);
			free(host);
		} else {
			strlcpy(sock, "/tmp/dk.socket", sizeof(sock));
		}
		if (setenv("DKSOCK", sock, 1) < 0) {
			warn("unable to set DKSOCK environment variable");
		}
		addr.sun_family = AF_UNIX;
		strlcpy(addr.sun_path, sock, sizeof(addr.sun_path));
		check(sockfd = socket(AF_UNIX, SOCK_STREAM, 0), "unable to create socket");
		unlink(sock);
		check(bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)), "unable to bind socket");
		check(listen(sockfd, SOMAXCONN), "unable to listen on socket");
	}
	fcntl(sockfd, F_SETFD, FD_CLOEXEC | fcntl(sockfd, F_GETFD));

	/* setup signal handling */
	struct sigaction sa;
	int sigs[] = {SIGTERM, SIGINT, SIGHUP, SIGCHLD, SIGPIPE};
	sa.sa_handler = sighandle;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART;
	for (uint32_t i = 0; i < LEN(sigs); i++) {
		if (sigs[i] == SIGPIPE) {
			sa.sa_handler = SIG_IGN;
		}
		check(sigaction(sigs[i], &sa, NULL), "unable to setup signal handler");
	}
	while (waitpid(-1, NULL, WNOHANG) > 0)
		;

	/* apply user settings and rules */
	execcfg();

	/* initialize existing windows AFTER config is loaded (rules, etc.) */
	xcb_query_tree_cookie_t rc = xcb_query_tree(con, root);
	if (!(rt = xcb_query_tree_reply(con, rc, &e))) {
		iferr(1, "unable to query tree from root window", e);
	} else if (rt->children_len) {
		xcb_window_t *w = xcb_query_tree_children(rt);
		for (uint32_t i = 0; i < rt->children_len; i++) {
			if (!wintrans(w[i])) {
				manage(w[i], 1);
				w[i] = XCB_WINDOW_NONE;
			}
		}
		for (uint32_t i = 0; i < rt->children_len; i++) {
			if (w[i] != XCB_WINDOW_NONE) {
				manage(w[i], 1);
			}
		}
	}
	free(rt);

	/* restore state from restart and warp pointer to active window if needed */
	if (savestate(1) == -1 && monitors->next) {
		Monitor *m = primary;
		xcb_warp_pointer(con, root, root, 0, 0, 0, 0, m->x + (m->w / 2), m->y + (m->h / 2));
	}

	confd = xcb_get_file_descriptor(con);
	while (running) {
		xcb_flush(con);
		FD_ZERO(&read_fds);
		FD_SET(sockfd, &read_fds);
		FD_SET(confd, &read_fds);
		nfds = MAX(confd, sockfd) + 1;
		if (select(nfds, &read_fds, NULL, NULL, NULL) > 0) {
			/* socket commands */
			if (FD_ISSET(sockfd, &read_fds)) {
				cmdfd = accept(sockfd, NULL, 0);
				ssize_t n = recv(cmdfd, buf, sizeof(buf) - 1, 0);
				if (cmdfd > 0 && n > 0) {
					if (buf[n - 1] == '\n') {
						n--;
					}
					buf[n] = '\0';
					if (!(cmdresp = fdopen(cmdfd, "w"))) {
						warn("unable to open the socket as file: %s", sock);
						close(cmdfd);
					}
					parsecmd(buf);
				}
			}
			/* xcb events */
			if (FD_ISSET(confd, &read_fds)) {
				xcb_flush(con);
				while ((ev = xcb_poll_for_event(con))) {
					dispatch(ev);
					free(ev);
				}
			}
		}
		if (xcb_connection_has_error(con)) {
			DBG("main: X connection has error -- %s", "bailing")
			break;
		}
		if (needsrefresh) {
			refresh();
		}
		/* handle dead status' and print existing ones if needed */
		Status *s = stats, *next;
		while (s) {
			next = s->next;
			if (write(fileno(s->file), 0, 0) == -1) {
				freestatus(s);
			}
			s = next;
		}
		if (stats && (winchange || wschange || lytchange)) {
			printstatus(NULL, 1);
		}
	}
	return 0;
}

static void absorb(Client *p, Client *c)
{
	xcb_window_t w = p->win;

	if (STATE(c, NOABSORB) || STATE(c, TERMINAL) || FLOATING(c)) {
		DBG("absorb: c: 0x%08x %s -- is not eligible to absorb windows", c->win, c->title)
		return;
	}
	DBG("absorb: c: 0x%08x %s - absorbing - p: 0x%08x %s", c->win, c->clss, p->win, p->clss)
	detach(c, 0);
	detachstack(c);
	winunmap(w);
	p->absorbed = c;
	p->win = c->win;
	c->win = w;

	/* TODO: this is excessive, find a better way to update client title and class */
	clientname(p);
	clientname(c);
	p->pid = winpid(p->win);
	c->pid = winpid(c->win);
	winclass(p->win, p->clss, p->inst, sizeof(p->clss));
	winclass(c->win, c->clss, c->inst, sizeof(c->clss));

	updatenetclients();
	p->state |= STATE_NEEDSMAP;
	wschange = winchange = 1;
	refresh();
}

static Client *absorbingclient(xcb_window_t win)
{
	Client *c;
	Workspace *ws;

	for (ws = workspaces; ws; ws = ws->next) {
		for (c = ws->clients; c; c = c->next) {
			if (c->absorbed && c->absorbed->win == win) {
				return c;
			}
		}
	}
	for (c = scratch.clients; c; c = c->next) {
		if (c->absorbed && c->absorbed->win == win) {
			return c;
		}
	}
	return NULL;
}

static void applypanelstrut(Panel *p)
{
	if (p->mon->x + p->l > p->mon->wx) {
		p->mon->wx = p->mon->x + p->l;
	}
	if (p->mon->y + p->t > p->mon->wy) {
		p->mon->wy = p->mon->y + p->t;
	}
	if (p->mon->wx + p->mon->ww > (p->mon->x + p->mon->w) - (p->r + (p->mon->wy - p->mon->y))) {
		p->mon->ww = p->mon->w - p->r - (p->mon->wx - p->mon->x);
	}
	if (p->mon->wy + p->mon->wh > (p->mon->y + p->mon->h) - (p->b + (p->mon->wy - p->mon->y))) {
		p->mon->wh = p->mon->h - p->b - (p->mon->wy - p->mon->y);
	}
}

static void applyrule(Client *c, Rule *r, xcb_atom_t curws, int nofocus)
{
	int ws = curws, dofocus = 0, xgrav = GRAV_NONE, ygrav = GRAV_NONE;

	if (r) {
		c->cb = r->cb;
		dofocus = r->focus;
		c->state |= r->state;
		xgrav = r->xgrav;
		ygrav = r->ygrav;
		c->x = r->x != -1 ? r->x : c->x;
		c->y = r->y != -1 ? r->y : c->y;
		c->w = r->w != -1 ? r->w : c->w;
		c->h = r->h != -1 ? r->h : c->h;
		c->bw = r->bw != -1 && !STATE(c, NOBORDER) ? r->bw : c->bw;
		if (!c->trans && ws == (int)curws) {
			if ((cmdusemon = (r->mon != NULL))) {
				int num;
				Monitor *m;
				if ((num = strtol(r->mon, NULL, 0)) > 0 && (m = itomon(num))) {
					ws = m->ws->num;
				} else {
					for (m = monitors; m; m = m->next) {
						if (!strcmp(r->mon, m->name)) {
							ws = m->ws->num;
							break;
						}
					}
				}
			} else if (r->ws > 0 && r->ws <= globalcfg[GLB_NUM_WS].val) {
				ws = r->ws - 1;
			}
		}
	}

	if (ws + 1 > globalcfg[GLB_NUM_WS].val && ws <= 256) {
		updworkspaces(ws + 1);
	}
	setworkspace(c, itows(MIN(ws, globalcfg[GLB_NUM_WS].val)), nofocus);
	if (!dofocus && nofocus && !globalcfg[GLB_FOCUS_URGENT].val) {
		seturgent(c, 1);
		clientborder(c, 0);
	}
	if (dofocus && c->ws != selws) {
		cmdview(c->ws);
	}
	if (r && STATE(r, FULLSCREEN)) {
		c->state &= ~STATE_FULLSCREEN;
		setfullscreen(c, 1);
	} else if (xgrav != GRAV_NONE || ygrav != GRAV_NONE) {
		gravitate(c, xgrav, ygrav, 1);
	}
	cmdusemon = 0;
}

int applysizehints(Client *c, int *x, int *y, int *w, int *h, int bw, int usermotion, int mouse)
{
	Monitor *m = MON(c);
	int baseismin, min = globalcfg[GLB_MIN_XY].val;

	*w = MAX(1, *w);
	*h = MAX(1, *h);
	if (usermotion) {
		if (!mouse) {
			if (*w > c->w && c->inc_w > *w - c->w) {
				*w = c->w + c->inc_w;
			} else if (*w < c->w && c->inc_w > c->w - *w) {
				*w = c->w - c->inc_w;
			}
			if (*h > c->h && c->inc_h > *h - c->h) {
				*h = c->h + c->inc_h;
			} else if (*h < c->h && c->inc_h > c->h - *h) {
				*h = c->h - c->inc_h;
			}
			*h = MIN(*h, m->wh);
			*w = MIN(*w, m->ww);
		}
		*x = CLAMP(*x, (*w - min) * -1, scr_w - (min + bw));
		*y = CLAMP(*y, (*h - min) * -1, scr_h - (min + bw));
	} else {
		*w = CLAMP(*w, globalcfg[GLB_MIN_WH].val, m->ww - (2 * bw));
		*h = CLAMP(*h, globalcfg[GLB_MIN_WH].val, m->wh - (2 * bw));
		*x = CLAMP(*x, m->wx, m->wx + m->ww - (*w + bw));
		*y = CLAMP(*y, m->wy, m->wy + m->wh - (*h + bw));
	}

	if (FLOATING(c) || globalcfg[GLB_TILE_HINTS].val) {
		if (!c->hints) {
			sizehints(c, 0);
		}
		if (!(baseismin = (c->base_w == c->min_w && c->base_h == c->min_h))) {
			*w -= c->base_w, *h -= c->base_h;
		}
		if (c->min_aspect > 0 && c->max_aspect > 0) {
			if (c->max_aspect < (float)*w / *h) {
				*w = *h * c->max_aspect + 0.5;
			} else if (c->min_aspect < (float)*h / *w) {
				*h = *w * c->min_aspect + 0.5;
			}
		}
		if (baseismin) {
			*w -= c->base_w, *h -= c->base_h;
		}
		if (c->inc_w) {
			*w -= *w % c->inc_w;
		}
		if (c->inc_h) {
			*h -= *h % c->inc_h;
		}
		*w += c->base_w;
		*h += c->base_h;
		*w = MAX(*w, c->min_w);
		*h = MAX(*h, c->min_h);
		if (c->max_w) {
			*w = MIN(*w, c->max_w);
		}
		if (c->max_h) {
			*h = MIN(*h, c->max_h);
		}
	}
	return *x != c->x || *y != c->y || *w != c->w || *h != c->h || bw != c->bw || STATE(c, NEEDSMAP);
}

void attach(Client *c, int tohead)
{
	Client *tail = NULL;

	if (!tohead) {
		TAIL(tail, c->ws->clients);
	}
	if (tail) {
		ATTACH(c, tail->next);
	} else {
		ATTACH(c, c->ws->clients);
	}
}

void attachstack(Client *c)
{
	c->snext = c->ws->stack;
	c->ws->stack = c;
}

int assignws(Workspace *ws, Monitor *mon)
{
	int n;
	Workspace *ows;

	if (ws->mon == mon) {
		return 1;
	}
	DBG("assignws: ws: %d -> mon mon: %s", ws->num, mon->name)
	for (n = 0, ows = workspaces; ows; ows = ows->next) {
		if (ows->mon == ws->mon) {
			n++;
			if (ows != ws) {
				n++;
				break;
			}
		}
	}
	if (n > 1 && ows) {
		DBG("assignws: old mon: %s has available workspace: %d", ws->mon->name, ows->num)
		if (ws == ws->mon->ws) {
			ws->mon->ws = ows;
		}
		Monitor *old = ws->mon;
		ws->mon = mon;
		relocatews(ws, old, 1);
		needsrefresh = 1;
		wschange = 1;
	} else {
		respond(cmdresp, "!unable to assign last/only workspace on monitor");
		return 0;
	}
	return 1;
}

void changews(Workspace *ws, int swap, int warp)
{
	Monitor *m;
	Workspace *hidews = ws->mon->ws;

	if (!ws || ws == selws) {
		return;
	}
	DBG("changews: %d:%s -> %d:%s - swap: %d - warp: %d", selws->num + 1, selws->mon->name, ws->num + 1,
		ws->mon->name, swap, warp)
	int dowarp = !swap && warp && selws->mon != ws->mon;
	int vis = ws == ws->mon->ws;
	lastws = selws;
	lastmon = selmon;
	m = selws->mon;
	if (selws->sel) {
		unfocus(selws->sel, 1);
	}
	if (swap && m != ws->mon) {
		Monitor *old = ws->mon;
		selws->mon = ws->mon;
		if (ws->mon->ws == ws) {
			ws->mon->ws = selws;
		}
		ws->mon = m;
		m->ws = ws;
		updnetworkspaces();
		relocatews(ws, old, vis);
		if (lastws->mon->ws == lastws) {
			relocatews(lastws, selmon, 1);
		}
	}
	selws = ws;
	selmon = selws->mon;
	selmon->ws = selws;
	showhide(selws->stack);
	if (dowarp) {
		xcb_warp_pointer(con, root, root, 0, 0, 0, 0,
						 ws->sel ? ws->sel->x + (ws->sel->w / 2) : ws->mon->x + (ws->mon->w / 2),
						 ws->sel ? ws->sel->y + (ws->sel->h / 2) : ws->mon->y + (ws->mon->h / 2));
		showhide(hidews->stack);
	} else {
		showhide(lastws->stack);
	}
	PROP(REPLACE, root, netatom[NET_DESK_CUR], XCB_ATOM_CARDINAL, 32, 1, &ws->num);
	needsrefresh = wschange = 1;
}

void clientborder(Client *c, int focused)
{ /* modified from swm/wmutils */
	if (STATE(c, NOBORDER) || !c->bw) {
		return;
	}
	uint32_t b = c->bw;
	uint32_t o = border[BORD_O_WIDTH];
	uint32_t in = border[focused ? BORD_FOCUS : STATE(c, URGENT) ? BORD_URGENT : BORD_UNFOCUS];

	DBG("clientborder: %s - 0x%08x %s, width=%d, outer_width=%d", focused ? "focus" : "unfocus", c->win,
		c->title, b, o)

	if (b - o > 0) {
		uint32_t out = border[focused ? BORD_O_FOCUS : STATE(c, URGENT) ? BORD_O_URGENT : BORD_O_UNFOCUS];
		xcb_rectangle_t inner[] = {
			{c->w,         0,            b - o,        c->h + b - o},
			{c->w + b + o, 0,            b - o,        c->h + b - o},
			{0,            c->h,         c->w + b - o, b - o       },
			{0,            c->h + b + o, c->w + b - o, b - o       },
			{c->w + b + o, c->h + b + o, b,            b           }
        };
		xcb_rectangle_t outer[] = {
			{c->w + b - o, 0,            o,              c->h + (b * 2)},
			{c->w + b,     0,            o,              c->h + (b * 2)},
			{0,            c->h + b - o, c->w + (b * 2), o             },
			{0,            c->h + b,     c->w + (b * 2), o             },
			{1,            1,            1,              1             }
        };

		xcb_pixmap_t pmap = xcb_generate_id(con);
		xcb_gcontext_t gc = xcb_generate_id(con);
		xcb_create_pixmap(con, c->depth, pmap, c->win, W(c), H(c));
		xcb_create_gc(con, gc, pmap, XCB_GC_FOREGROUND, &in);
		xcb_poly_fill_rectangle(con, pmap, gc, LEN(inner), inner);
		xcb_change_gc(con, gc, XCB_GC_FOREGROUND, &out);
		xcb_poly_fill_rectangle(con, pmap, gc, LEN(outer), outer);
		xcb_change_window_attributes(con, c->win, XCB_CW_BORDER_PIXMAP, &pmap);
		xcb_free_pixmap(con, pmap);
		xcb_free_gc(con, gc);
	} else {
		xcb_change_window_attributes(con, c->win, XCB_CW_BORDER_PIXEL, &in);
	}
	xcb_flush(con);
}

void clienthints(Client *c)
{
	xcb_generic_error_t *e;
	xcb_icccm_wm_hints_t wmh;

	if (xcb_icccm_get_wm_hints_reply(con, xcb_icccm_get_wm_hints(con, c->win), &wmh, &e)) {
		if (c == selws->sel && wmh.flags & XCB_ICCCM_WM_HINT_X_URGENCY) {
			wmh.flags &= ~XCB_ICCCM_WM_HINT_X_URGENCY;
			xcb_icccm_set_wm_hints(con, c->win, &wmh);
		} else if (wmh.flags & XCB_ICCCM_WM_HINT_X_URGENCY) {
			c->state |= STATE_URGENT;
		}
		if ((wmh.flags & XCB_ICCCM_WM_HINT_INPUT) && !wmh.input) {
			c->state |= STATE_NOINPUT;
		}
	} else {
		iferr(0, "unable to get window wm hints reply", e);
	}
}

void clientmotif(void)
{
	Client *c;
	Workspace *ws;

	for (ws = workspaces; ws; ws = ws->next) {
		for (c = ws->clients; c; c = c->next) {
			if (c->has_motif) {
				if (globalcfg[GLB_OBEY_MOTIF].val) {
					c->state |= STATE_NOBORDER;
					c->bw = 0;
				} else {
					c->state &= ~STATE_NOBORDER;
					c->bw = border[BORD_WIDTH];
				}
				clientborder(c, c == selws->sel);
			}
		}
	}
	for (c = scratch.clients; c; c = c->next) {
		if (c->has_motif) {
			if (globalcfg[GLB_OBEY_MOTIF].val) {
				c->state |= STATE_NOBORDER;
				c->bw = 0;
			} else {
				c->state &= ~STATE_NOBORDER;
				c->bw = border[BORD_WIDTH];
			}
			clientborder(c, c == selws->sel);
		}
	}
}

int clientname(Client *c)
{
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t rc;
	xcb_icccm_get_text_property_reply_t r;

	rc = xcb_icccm_get_text_property(con, c->win, netatom[NET_WM_NAME]);
	if (!xcb_icccm_get_text_property_reply(con, rc, &r, &e)) {
		iferr(0, "unable to get NET_WM_NAME text property reply", e);
		rc = xcb_icccm_get_text_property(con, c->win, XCB_ATOM_WM_NAME);
		if (!xcb_icccm_get_text_property_reply(con, rc, &r, &e)) {
			iferr(0, "unable to get WM_NAME text property reply", e);
			strlcpy(c->title, "broken", sizeof(c->title));
			return 0;
		}
	}

	if (r.name && r.name[0] != '\0' && r.name_len > 0 &&
		(r.encoding == wmatom[WM_UTF8STR] || r.encoding == XCB_ATOM_STRING)) {
		strlcpy(c->title, r.name, MIN(sizeof(c->title), r.name_len + 1));
	} else {
		strlcpy(c->title, "broken", sizeof(c->title));
	}
	xcb_icccm_get_text_property_reply_wipe(&r);
	return 1;
}

void clientrule(Client *c, Rule *wr, int nofocus)
{
	Rule *r = wr;
	xcb_atom_t type = 0;
	xcb_atom_t curws = selws->num;

	if (c->trans) {
		curws = c->trans->ws->num;
	} else if (!winprop(c->win, netatom[NET_WM_DESK], &curws) || curws > 256) {
		curws = selws->num;
	}
	winprop(c->win, netatom[NET_WM_TYPE], &type);

	if (!r) {
		for (r = rules; r; r = r->next) {
			if (rulecmp(c, r) && (!r->type || r->type == type)) {
				applyrule(c, r, curws, nofocus);
				return;
			}
		}
	} else if (rulecmp(c, r) && (!r->type || r->type == type)) {
		applyrule(c, r, curws, nofocus);
		return;
	}
	applyrule(c, NULL, curws, nofocus);
}

void clientstate(Client *c)
{
	xcb_atom_t *state;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t rc;
	xcb_get_property_reply_t *r = NULL;

	rc = xcb_get_property(con, 0, c->win, netatom[NET_WM_STATE], XCB_ATOM_ANY, 0, 3);
	if ((r = xcb_get_property_reply(con, rc, &e))) {
		if (r->value_len && r->format == 32) {
			state = xcb_get_property_value(r);
			for (uint32_t i = 0; i < r->value_len; i++) {
				if (state[i] == netatom[NET_STATE_FULL]) {
					setfullscreen(c, 1);
				} else if (state[i] == netatom[NET_STATE_ABOVE]) {
					c->state |= STATE_ABOVE | STATE_FLOATING;
				}
			}
		}
	} else {
		iferr(0, "unable to get window property reply", e);
	}
	free(r);
}

void clienttype(Client *c)
{
	xcb_atom_t type;

	if ((winprop(c->win, netatom[NET_WM_TYPE], &type) &&
		 (type == netatom[NET_TYPE_DIALOG] || type == netatom[NET_TYPE_SPLASH])) ||
		c->trans || (c->trans = wintoclient(wintrans(c->win)))) {
		c->state |= STATE_FLOATING;
	}
}

Monitor *coordtomon(int x, int y)
{
	Monitor *m = NULL;

	for (m = monitors; m; m = m->next) {
		if (m->connected && x >= m->x && x < m->x + m->w && y >= m->y && y < m->y + m->h) {
			return m;
		}
	}
	return m;
}

void desorb(Client *c)
{
	DBG("desorb: 0x%08x %s -- c->win = 0x%08x %s", c->win, c->title, c->absorbed->win, c->absorbed->title)
	c->win = c->absorbed->win;
	free(c->absorbed);
	c->absorbed = NULL;
	setfullscreen(c, 0);
	clientname(c);
	c->pid = winpid(c->win);
	winclass(c->win, c->clss, c->inst, sizeof(c->clss));
	wschange = winchange = 1;
	c->state |= STATE_NEEDSMAP;
	refresh();
}

static int discreteproc(pid_t p, pid_t c)
{
	while (p != c && c != 0) {
		c = parentproc(c);
	}
	return c;
}

void detach(Client *c, int reattach)
{
	Client **cc = &c->ws->clients;

	DETACH(c, cc);
	if (reattach) {
		ATTACH(c, c->ws->clients);
	}
}

void detachstack(Client *c)
{
	Client **cc = &c->ws->stack;

	while (*cc && *cc != c) {
		cc = &(*cc)->snext;
	}
	*cc = c->snext;
	if (c == c->ws->sel) {
		c->ws->sel = c->ws->stack;
	}
}

static int savestate(int restore)
{
	Client c, *t;
	Workspace *ws;
	FILE *f = NULL;
	xcb_window_t win;
	const char *path = "/tmp/dk_state";

	if (!(f = fopen(path, restore ? "rb" : "wb"))) {
		perror("dk: fopen");
		return -1;
	}
	if (restore) {
		/* restore the active window */
		if (fread(&win, 1, sizeof(xcb_window_t), f) != sizeof(xcb_window_t)) {
			perror("dk: fread");
			return -1;
		}
		if ((t = wintoclient(win)) && VISIBLE(t)) {
			focus(t);
			if (monitors->next) {
				xcb_generic_error_t *e;
				xcb_query_pointer_reply_t *r = NULL;
				if ((r = xcb_query_pointer_reply(con, xcb_query_pointer(con, root), &e)) &&
					!INRECT(r->root_x, r->root_y, 2, 2, t->x, t->y, t->w, t->h)) {
					xcb_warp_pointer(con, root, root, 0, 0, 0, 0, t->x + (t->w / 2), t->y + (t->h / 2));
				}
				free(r);
			}
		}
		/* read a client worth of data and restore it until we're done */
		while (fread(&c, 1, sizeof(Client), f) == sizeof(Client)) {
			if (!(t = wintoclient(c.win)) || !(c.state & STATE_FLOATING)) {
				continue;
			}
			t->state |= STATE_FLOATING;
			if (VISIBLE(t)) {
				resizehint(t, c.x, c.y, c.w, c.h, c.bw, 0, 0);
				needsrefresh = 1;
			} else {
				t->x = c.x, t->y = c.y, t->w = c.w, t->h = c.h; t->bw = c.bw;
			}
		}
		for (ws = workspaces; ws; ws = ws->next) {
			ws->sel = !ws->sel && ws->stack ? ws->stack : NULL;
		}
	} else {
		/* dump client data as binary */
		win = selws->sel ? selws->sel->win : XCB_WINDOW_NONE;
		if (fwrite(&win, 1, sizeof(xcb_window_t), f) != sizeof(xcb_window_t)) {
			perror("dk: fwrite");
			return -1;
		}
		for (ws = workspaces; ws; ws = ws->next) {
			for (t = ws->clients; t; t = t->next) {
				if (fwrite(t, 1, sizeof(Client), f) != sizeof(Client)) {
					perror("dk: fwrite");
					return -1;
				}
			}
		}
		for (t = scratch.clients; t; t = t->next) {
			if (fwrite(t, 1, sizeof(Client), f) != sizeof(Client)) {
				perror("dk: fwrite");
				return -1;
			}
		}
	}
	fclose(f);
	return 0;
}

void execcfg(void)
{
	char *cfg, *s, path[PATH_MAX];

	if (!(cfg = getenv("DKRC"))) {
		if (!(s = getenv("HOME"))) {
			warn("getenv");
			return;
		}
		strlcpy(path, s, sizeof(path));
		strlcat(path, "/.config/dk/dkrc", sizeof(path));
		cfg = path;
	}

	if (!fork()) {
		if (con) {
			close(xcb_get_file_descriptor(con));
		}
		setsid();
		execle(cfg, cfg, (char *)NULL, environ);
		warn("unable to execute config file: %s", cfg);
		exit(0);
	}
}

void fillstruts(Panel *p)
{
	int *s;
	xcb_generic_error_t *err;
	xcb_get_property_cookie_t rc;
	xcb_get_property_reply_t *prop = NULL;

	rc = xcb_get_property(con, 0, p->win, netatom[NET_WM_STRUTP], XCB_ATOM_CARDINAL, 0, 4);
	if (!(prop = xcb_get_property_reply(con, rc, &err)) || prop->type == XCB_NONE) {
		rc = xcb_get_property(con, 0, p->win, netatom[NET_WM_STRUT], XCB_ATOM_CARDINAL, 0, 4);
		iferr(0, "unable to get _NET_WM_STRUT_PARTIAL reply from window", err);
		if (!(prop = xcb_get_property_reply(con, rc, &err))) {
			iferr(0, "unable to get _NET_WM_STRUT reply from window", err);
		}
	}
	if (prop && xcb_get_property_value_length(prop) >= 4 && (s = xcb_get_property_value(prop))) {
		p->l = s[0], p->r = s[1], p->t = s[2], p->b = s[3];
	}
	free(prop);
}

void focus(Client *c)
{
	if (!selws) {
		selws = workspaces;
	}
	if (!c) {
		c = selws ? selws->stack : NULL;
	}
	if (selws && selws->sel) {
		if (c && c != selws->sel && c->win == selws->sel->win) {
			DBG("focus: IGNORING -- %p 0x%08x %s", (void *)c, c->win, c->title);
			return;
		}
		unfocus(selws->sel, 0);
	}
	if (c) {
		DBG("focus: 0x%08x %s", c->win, c->title)
		if (STATE(c, URGENT)) {
			seturgent(c, 0);
		}
		detachstack(c);
		attachstack(c);
		clientborder(c, 1);
		setinputfocus(c);
		selws->sel = c;
		cmdc = c;
		if (selws->layout->func == mono) {
			/* resizehint won't move the window when nothing has changed
			 * so we force it on this window by incrementing the width */
			c->w++;
			mono(selws);
			ignore(XCB_ENTER_NOTIFY);
			xcb_aux_sync(con);
		}
	} else {
		unfocus(NULL, 1);
		selws->sel = NULL;
	}
	winchange = 1;
}

static void freemon(Monitor *m)
{
	Monitor **mm = &monitors;

	DETACH(m, mm);
	free(m);
}

void freerule(Rule *r)
{
	Rule **rr = &rules;

	DETACH(r, rr);
	if (r->clss) {
		regfree(&(r->clssreg));
		free(r->clss);
	}
	if (r->inst) {
		regfree(&(r->instreg));
		free(r->inst);
	}
	if (r->title) {
		regfree(&(r->titlereg));
		free(r->title);
	}
	free(r->mon);
	free(r);
}

void freestatus(Status *s)
{
	Status **ss = &stats;

	DETACH(s, ss);
	if (!restart) {
		fclose(s->file);
	}
	if (s->path) {
		free(s->path);
	}
	free(s);
}

void freewm(void)
{
	Client *c;
	Workspace *ws;

	if (restart) {
		savestate(0);
	}
	for (c = scratch.clients; c; c = c->next) {
		setworkspace(c, selws, 0);
	}
	while ((ws = workspaces)) {
		while (ws->clients) {
			TAIL(c, ws->clients);
			unmanage(c->win, 0);
		}
		freews(ws);
	}
	while (panels) unmanage(panels->win, 0);
	while (desks) unmanage(desks->win, 0);
	while (rules) freerule(rules);
	while (stats) freestatus(stats);
	while (monitors) freemon(monitors);

	xcb_key_symbols_free(keysyms);
	for (uint32_t i = 0; i < LEN(cursors); i++) {
		xcb_free_cursor(con, cursor[i]);
	}
	xcb_destroy_window(con, wmcheck);
	xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, XCB_INPUT_FOCUS_POINTER_ROOT, XCB_CURRENT_TIME);
	if (!restart) {
		xcb_delete_property(con, root, netatom[NET_ACTIVE]);
	}
	xcb_flush(con);
	xcb_disconnect(con);

	if (restart) {
		fcntl(sockfd, F_SETFD, ~FD_CLOEXEC & fcntl(sockfd, F_GETFD));
		char fdstr[64];
		if (!itoa(sockfd, fdstr)) {
			itoa(-1, fdstr);
		}
		char *const arg[] = {argv0, "-s", fdstr, NULL};
		execvp(arg[0], arg);
	}

	close(sockfd);
	unlink(sock);
}

static void freews(Workspace *ws)
{
	Workspace **wws = &workspaces;

	if (ws == selws) {
		selws = ws->next ? ws->next : workspaces;
		selmon = selws->mon;
		selmon->ws = selws;
	}
	DETACH(ws, wws);
	free(ws);
}

void grabbuttons(Client *c)
{
	xcb_ungrab_button(con, XCB_BUTTON_INDEX_ANY, c->win, XCB_BUTTON_MASK_ANY);
	xcb_grab_button(con, 0, c->win, XCB_EVENT_MASK_BUTTON_PRESS, XCB_GRAB_MODE_SYNC, XCB_GRAB_MODE_SYNC,
					XCB_NONE, XCB_NONE, XCB_BUTTON_INDEX_ANY, XCB_BUTTON_MASK_ANY);
	uint32_t mods[] = {0, XCB_MOD_MASK_LOCK, lockmask, lockmask | XCB_MOD_MASK_LOCK};
	for (uint32_t i = 0; i < LEN(mods); i++) {
		xcb_grab_button(con, 0, c->win, XCB_EVENT_MASK_BUTTON_PRESS, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_SYNC,
						XCB_NONE, XCB_NONE, mousemove, mousemod | mods[i]);
		xcb_grab_button(con, 0, c->win, XCB_EVENT_MASK_BUTTON_PRESS, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_SYNC,
						XCB_NONE, XCB_NONE, mouseresize, mousemod | mods[i]);
	}
}

void gravitate(Client *c, int xgrav, int ygrav, int matchgap)
{
	int x, y, gap;
	int monx, mony, monw, monh;

	if (!c || !c->ws || !FLOATING(c)) {
		return;
	}
	x = c->x, y = c->y;
	if (c->trans) {
		gap = 0;
		monx = c->trans->x, mony = c->trans->y;
		monw = c->trans->w, monh = c->trans->h;
	} else {
		gap = matchgap ? c->ws->gappx : 0;
		monx = MON(c)->wx, mony = MON(c)->wy;
		monw = MON(c)->ww, monh = MON(c)->wh;
	}
	switch (xgrav) {
		case GRAV_LEFT: x = monx + gap; break;
		case GRAV_RIGHT: x = monx + monw - W(c) - gap; break;
		case GRAV_CENTER: x = (monx + monw - W(c)) / 2; break;
	}
	switch (ygrav) {
		case GRAV_TOP: y = mony + gap; break;
		case GRAV_BOTTOM: y = mony + monh - H(c) - gap; break;
		case GRAV_CENTER: y = (mony + monh - H(c)) / 2; break;
	}
	if (c->x == x && c->y == y) {
		return;
	}
	DBG("gravitate: moving window: %d, %d -> %d, %d", c->x, c->y, x, y)
	c->x = x, c->y = y;
	if (VISIBLE(c)) {
		resizehint(c, x, y, c->w, c->h, c->bw, 0, 0);
	}
}

int iferr(int lvl, char *msg, xcb_generic_error_t *e)
{
	if (!e) {
		return 1;
	}
	warn("%s", msg);
	free(e);
	if (lvl) {
		exit(lvl);
	}
	return 0;
}

static void initatoms(xcb_atom_t *atoms, const char **names, int num)
{
	int i;
	xcb_generic_error_t *e;
	xcb_intern_atom_reply_t *r;
	xcb_intern_atom_cookie_t c[num];

	for (i = 0; i < num; ++i) {
		c[i] = xcb_intern_atom(con, 0, strlen(names[i]), names[i]);
	}
	for (i = 0; i < num; ++i) {
		if ((r = xcb_intern_atom_reply(con, c[i], &e))) {
			atoms[i] = r->atom;
			free(r);
		} else {
			iferr(0, "unable to initialize atom", e);
		}
	}
}

static void initclient(xcb_window_t win, xcb_get_geometry_reply_t *g)
{
	Client *c, *term = NULL;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t pc;
	xcb_get_property_reply_t *pr = NULL;

	c = ecalloc(1, sizeof(Client));
	c->win = win;
	c->depth = g->depth;
	c->x = c->old_x = g->x;
	c->y = c->old_y = g->y;
	c->w = c->old_w = g->width;
	c->h = c->old_h = g->height;
	c->bw = c->old_bw = border[BORD_WIDTH];
	c->pid = winpid(win);
	c->has_motif = 0;
	c->state = STATE_NEEDSMAP;
	c->old_state = STATE_NONE;
	c->trans = wintoclient(wintrans(win));
	winclass(win, c->clss, c->inst, sizeof(c->clss));
	clientname(c);
	DBG("initclient: 0x%08x - %s", c->win, c->title)

	pc = xcb_get_property(con, 0, c->win, wmatom[WM_MOTIF], wmatom[WM_MOTIF], 0, 5);
	if ((pr = xcb_get_property_reply(con, pc, &e)) && xcb_get_property_value_length(pr) >= 3) {
		if (((xcb_atom_t *)xcb_get_property_value(pr))[2] == 0) {
			c->has_motif = 1;
			if (globalcfg[GLB_OBEY_MOTIF].val) {
				c->bw = 0;
				c->state |= STATE_NOBORDER;
			}
		}
	} else {
		iferr(0, "unable to get window motif hints reply", e);
	}
	free(pr);

	/* apply rules and set the client's workspace, when config focus_open=false
	 * the new client is attached to the end of the stack, otherwise the head.
	 * later in refresh(), focus(NULL) is called to focus the correct client */
	DBG("initclient: rule setting: 0x%08x - %s", c->win, c->title)
	clientrule(c, NULL, !globalcfg[GLB_FOCUS_OPEN].val);
	clientstate(c); /* state MUST be after rule to ensure c->ws is set */
	clienttype(c);
	clienthints(c);
	sizehints(c, 1);
	grabbuttons(c);
	if (!c->trans) {
		term = termforwin(c);
	}

	if (!FULLSCREEN(c) && (FLOATING(c) || STATE(c, FIXED))) {
		DBG("initclient: floating or fixed: 0x%08x - %s", c->win, c->title)
		if (!STATE(c, FIXED)) {
			c->w = CLAMP(c->w, globalcfg[GLB_MIN_WH].val, MON(c)->ww);
			c->h = CLAMP(c->h, globalcfg[GLB_MIN_WH].val, MON(c)->wh);
		}
		if (c->trans) {
			c->x = c->trans->x + ((W(c->trans) - W(c)) / 2);
			c->y = c->trans->y + ((H(c->trans) - H(c)) / 2);
		}
		c->x = CLAMP(c->x, MON(c)->x, MON(c)->x + MON(c)->w - W(c));
		c->y = CLAMP(c->y, MON(c)->y, MON(c)->y + MON(c)->h - H(c));
		if (c->x == MON(c)->x && c->y == MON(c)->y) {
			quadrant(c, &c->x, &c->y, &c->w, &c->h);
		}
	}
	if (c->cb) {
		c->cb->func(c, 0);
	}
	xcb_change_window_attributes(con, win, XCB_CW_EVENT_MASK, &clientmask);
	if (term && term->win != win && term->ws == c->ws) {
		absorb(term, c);
	} else if (STATE(c, SCRATCH) && !STATE(c, FULLSCREEN)) {
		cmdc = c;
		char *arg = "push";
		DBG("initclient: calling cmdscratch(%s) on client: 0x%08x %s", arg, c->win, c->title)
		cmdscratch(&arg);
	}
	wschange = c->ws->clients->next ? wschange : 1;
}

static void initdesk(xcb_window_t win, xcb_get_geometry_reply_t *g)
{
	Desk *d;
	uint32_t deskmask = XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY;

	d = ecalloc(1, sizeof(Desk));
	d->win = win;
	if (!(d->mon = coordtomon(g->x, g->y))) {
		d->mon = selws->mon;
	}
	d->state |= STATE_NEEDSMAP;
	winclass(win, d->clss, d->inst, sizeof(d->clss));
	DBG("initdesk: 0x%08x - %s", d->win, d->clss)
	ATTACH(d, desks);
	MOVERESIZE(win, d->mon->x, d->mon->y, d->mon->w, d->mon->h, g->border_width);
	setstackmode(d->win, XCB_STACK_MODE_BELOW);
	xcb_change_window_attributes(con, d->win, XCB_CW_EVENT_MASK, &deskmask);
	needsrefresh = 1;
}

static void initmon(int num, char *name, xcb_randr_output_t id, int x, int y, int w, int h)
{
	Monitor *m, *tail;

	m = ecalloc(1, sizeof(Monitor));
	m->id = id;
	m->num = num;
	m->connected = 1;
	m->x = m->wx = x;
	m->y = m->wy = y;
	m->w = m->ww = w;
	m->h = m->wh = h;
	strlcpy(m->name, name, sizeof(m->name));
	TAIL(tail, monitors);
	if (tail) {
		tail->next = m;
	} else {
		monitors = m;
	}
}

static void initpanel(xcb_window_t win, xcb_get_geometry_reply_t *g)
{
	Panel *p;
	uint32_t panelmask = XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY;

	p = ecalloc(1, sizeof(Panel));
	p->win = win;
	p->x = g->x;
	p->y = g->y;
	p->w = g->width;
	p->h = g->height;
	p->state |= STATE_NEEDSMAP;
	if (!(p->mon = coordtomon(g->x, g->y))) {
		p->mon = selws->mon;
	}
	winclass(win, p->clss, p->inst, sizeof(p->clss));
	ATTACH(p, panels);
	fillstruts(p);
	updstruts();
	xcb_change_window_attributes(con, p->win, XCB_CW_EVENT_MASK, &panelmask);
	needsrefresh = 1;
}

Rule *initrule(Rule *wr)
{
	int i;
	Rule *r;
	size_t len;
	char buf[NAME_MAX];

#define CPYSTR(dst, src)                                                                                     \
	dst = ecalloc(1, (len = strlen(src) + 1));                                                               \
	strlcpy(dst, src, len)
#define INITREG(str, reg)                                                                                    \
	if ((i = regcomp(reg, str, REG_NOSUB | REG_EXTENDED | REG_ICASE))) {                                     \
		regerror(i, reg, buf, sizeof(buf));                                                                  \
		respond(cmdresp, "!invalid regex %s: %s", str, buf);                                                 \
		goto error;                                                                                          \
	}
#define FREEREG(str, wstr, reg)                                                                              \
	if (wstr) {                                                                                              \
		regfree(reg);                                                                                        \
		free(str);                                                                                           \
	}

	r = ecalloc(1, sizeof(Rule));
	memcpy(r, wr, sizeof(Rule));
	if (wr->mon) {
		CPYSTR(r->mon, wr->mon);
	}
	if (wr->title) {
		CPYSTR(r->title, wr->title);
		INITREG(r->title, &(r->titlereg))
	}
	if (wr->clss) {
		CPYSTR(r->clss, wr->clss);
		INITREG(r->clss, &(r->clssreg))
	}
	if (wr->inst) {
		CPYSTR(r->inst, wr->inst);
		INITREG(r->inst, &(r->instreg))
	}
	ATTACH(r, rules);
	return r;

error:
	FREEREG(r->title, wr->title, &(r->titlereg))
	FREEREG(r->clss, wr->clss, &(r->clssreg))
	FREEREG(r->inst, wr->inst, &(r->instreg))
	if (wr->mon) {
		free(r->mon);
	}
	free(r);
	return NULL;

#undef INITREG
#undef FREEREG
#undef CPYSTR
}

Status *initstatus(Status *tmp)
{
	Status *s, *tail;
	s = ecalloc(1, sizeof(Status));
	if (tmp->path) {
		size_t len = strlen(tmp->path) + 1;
		s->path = ecalloc(1, len);
		strlcpy(s->path, tmp->path, len);
	}
	s->num = tmp->num;
	s->file = tmp->file;
	s->type = tmp->type;
	switch (s->type) {
		case STAT_WS: wschange = 1; break;
		case STAT_WIN: winchange = 1; break;
		case STAT_LYT: lytchange = 1; break;
		default: wschange = 1; winchange = 1; lytchange = 1; break;
	}
	TAIL(tail, stats);
	if (tail) {
		tail->next = s;
	} else {
		stats = s;
	}
	return s;
}

static void initwm(void)
{
	int cws;
	xcb_atom_t r;
	Workspace *ws;
	uint32_t i;
	xcb_cursor_context_t *ctx;
	const xcb_query_extension_reply_t *ext;

	check(xcb_cursor_context_new(con, scr, &ctx), "unable to create cursor context");
	for (i = 0; i < LEN(cursors); i++) {
		cursor[i] = xcb_cursor_load_cursor(ctx, cursors[i]);
	}
	xcb_cursor_context_free(ctx);

	initatoms(wmatom, wmatoms, LEN(wmatoms));
	initatoms(netatom, netatoms, LEN(netatoms));

	if ((ext = xcb_get_extension_data(con, &xcb_randr_id)) && ext->present) {
		randrbase = ext->first_event;
		xcb_randr_select_input(con, root, XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE);
		xcb_flush(con);
		updrandr(1);
	} else {
		warnx("unable to get randr extension data");
	}
	if (randrbase < 0 || !nextmon(monitors)) {
		initmon(0, "default", 0, 0, 0, scr_w, scr_h);
	}

	cws = winprop(root, netatom[NET_DESK_CUR], &r) && r < 100 ? r : 0;
	updworkspaces(MAX(cws + 1, globalcfg[GLB_NUM_WS].val));
	selws = workspaces;
	selmon = selws->mon;
	changews((ws = itows(cws)) ? ws : workspaces, globalcfg[GLB_WS_STATIC].val, 1);

	wmcheck = xcb_generate_id(con);
	xcb_create_window(con, XCB_COPY_FROM_PARENT, wmcheck, root, -1, -1, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_ONLY,
					  scr->root_visual, 0, NULL);
	PROP(REPLACE, wmcheck, netatom[NET_WM_CHECK], XCB_ATOM_WINDOW, 32, 1, &wmcheck);
	PROP(REPLACE, wmcheck, netatom[NET_WM_NAME], wmatom[WM_UTF8STR], 8, 2, "dk");
	PROP(REPLACE, root, netatom[NET_WM_CHECK], XCB_ATOM_WINDOW, 32, 1, &wmcheck);
	PROP(REPLACE, root, netatom[NET_SUPPORTED], XCB_ATOM_ATOM, 32, LEN(netatom), netatom);
	xcb_delete_property(con, root, netatom[NET_CLIENTS]);

	uint32_t rm = monitors->next ? (rootmask | XCB_EVENT_MASK_POINTER_MOTION) : rootmask;
	uint32_t val[] = {rm, cursor[CURS_NORMAL]};
	iferr(1, "unable to change root window event mask or cursor",
		  xcb_request_check(
			  con, xcb_change_window_attributes_checked(con, root, XCB_CW_EVENT_MASK | XCB_CW_CURSOR, &val)));
	if (!(keysyms = xcb_key_symbols_alloc(con))) {
		err(1, "unable to get keysyms from X connection");
	}
	numlockmask();
}

static Workspace *initws(int num)
{
	Workspace *ws, *tail;

	ws = ecalloc(1, sizeof(Workspace));
	ws->num = num;
	itoa(num + 1, ws->name);
	ws->gappx = MAX(0, wsdef.gappx);
	ws->layout = wsdef.layout;
	ws->nmaster = MAX(0, wsdef.nmaster);
	ws->nstack = MAX(0, wsdef.nstack);
	ws->msplit = CLAMP(wsdef.msplit, 0.05, 0.95);
	ws->ssplit = CLAMP(wsdef.ssplit, 0.05, 0.95);
	ws->padl = MAX(0, wsdef.padl);
	ws->padr = MAX(0, wsdef.padr);
	ws->padt = MAX(0, wsdef.padt);
	ws->padb = MAX(0, wsdef.padb);
	TAIL(tail, workspaces);
	if (tail) {
		tail->next = ws;
	} else {
		workspaces = ws;
	}
	return ws;
}

Monitor *itomon(int num)
{
	Monitor *mon = monitors;

	while (mon && mon->num != num) {
		mon = mon->next;
	}
	return mon;
}

Workspace *itows(int num)
{
	Workspace *ws = workspaces;

	while (ws && ws->num != num) {
		ws = ws->next;
	}
	return ws;
}

void manage(xcb_window_t win, int scan)
{
	xcb_get_geometry_reply_t *g = NULL;
	xcb_get_window_attributes_reply_t *wa = NULL;
	xcb_atom_t type, state;

	if (wintoclient(win) || wintopanel(win) || wintodesk(win)) {
		return;
	}
	if (!(wa = winattr(win)) || !(g = wingeom(win))) {
		goto end;
	}
	DBG("manage: 0x%08x - %d,%d @ %dx%d", win, g->x, g->y, g->width, g->height)
	if (winprop(win, netatom[NET_WM_TYPE], &type)) {
		DBG("manage: 0x%08x has NET_WM_TYPE", win);
		if (type == netatom[NET_TYPE_DOCK]) {
			initpanel(win, g);
		} else if (type == netatom[NET_TYPE_DESK]) {
			initdesk(win, g);
		} else if (!wa->override_redirect) {
			goto client;
		}
		/* never reached for normal windows, only panels, desktops, and
		 * override_redirect windows */
		setwinstate(win, XCB_ICCCM_WM_STATE_NORMAL);
	} else if (!wa->override_redirect) {
client:
		if (scan && !(wa->map_state == XCB_MAP_STATE_VIEWABLE ||
					  (winprop(win, wmatom[WM_STATE], &state) && state == XCB_ICCCM_WM_STATE_ICONIC))) {
			goto end;
		}
		initclient(win, g);
		PROP(APPEND, root, netatom[NET_CLIENTS], XCB_ATOM_WINDOW, 32, 1, &win);
	}
	refresh();
end:
	free(wa);
	free(g);
}

void movestack(int direction)
{
	Client *c = cmdc, *t;

	if (!nexttiled(c->ws->clients->next)) {
		return;
	}
	if (direction > 0) {
		while (direction) {
			detach(c, (t = nexttiled(c->next)) ? 0 : 1);
			if (t) {
				ATTACH(c, t->next);
			}
			direction--;
		}
	} else {
		int i;
		while (direction) {
			if (c == nexttiled(c->ws->clients)) {
				detach(c, 0);
				attach(c, 0);
			} else {
				for (t = nexttiled(c->ws->clients); t && nexttiled(t->next) && nexttiled(t->next) != c;
					 t = nexttiled(t->next))
					;
				detach(c, (i = (t == nexttiled(c->ws->clients)) ? 1 : 0));
				if (!i) {
					c->next = t;
					PREV(t, c->next, c->ws->clients);
					t->next = c;
				}
			}
			direction++;
		}
	}
	needsrefresh = 1;
}

Monitor *nextmon(Monitor *m)
{
	while (m && !m->connected) {
		m = m->next;
	}
	return m;
}

Client *nexttiled(Client *c)
{
	while (c && FLOATING(c)) {
		c = c->next;
	}
	return c;
}

void numlockmask(void)
{
	xcb_generic_error_t *e;
	xcb_get_modifier_mapping_reply_t *m = NULL;

	if ((m = xcb_get_modifier_mapping_reply(con, xcb_get_modifier_mapping(con), &e))) {
		xcb_keycode_t *k, *t = NULL;
		if ((t = xcb_key_symbols_get_keycode(keysyms, 0xff7f)) &&
			(k = xcb_get_modifier_mapping_keycodes(m))) {
			for (uint32_t i = 0; i < 8; i++) {
				for (uint32_t j = 0; j < m->keycodes_per_modifier; j++) {
					if (k[i * m->keycodes_per_modifier + j] == *t) {
						lockmask = (1 << i);
					}
				}
			}
		}
		free(t);
	} else {
		iferr(0, "unable to get modifier mapping for numlock", e);
	}
	free(m);
}

static Monitor *outputtomon(xcb_randr_output_t id)
{
	Monitor *m = NULL;

	for (m = monitors; m; m = m->next) {
		if (m->id == id) {
			return m;
		}
	}
	return m;
}

static pid_t parentproc(pid_t p)
{
	FILE *f;
	char buf[256];
	unsigned int v = 0;

	snprintf(buf, sizeof(buf) - 1, "/proc/%u/stat", p);
	if (!(f = fopen(buf, "r"))) {
		perror("dk: fopen");
		return 0;
	}
	if (fscanf(f, "%*u %*s %*c %u", &v) <= 0) {
		perror("dk: fscanf");
	}
	fclose(f);
	return v;
}

void popfloat(Client *c)
{
	int x, y, w, h;

	c->state |= STATE_FLOATING;
	x = c->x, y = c->y;
	w = CLAMP(c->w, MON(c)->ww / 8, MON(c)->ww / 3);
	h = CLAMP(c->h, MON(c)->wh / 8, MON(c)->wh / 3);
	quadrant(c, &x, &y, &w, &h);
	resizehint(c, x, y, w, h, c->bw, 0, 0);
	setstackmode(c->win, XCB_STACK_MODE_ABOVE);
}

void quadrant(Client *c, int *x, int *y, const int *w, const int *h)
{
	Monitor *m = MON(c);
	static int index = 0;
	static Workspace *ws = NULL;
	uint32_t i = 0;
	int tw = m->ww / 3, th = m->wh / 3;
	int q[][3] = {
		{1, m->wx + tw,       m->wy + th      },
		{1, m->wx + (tw * 2), m->wy + th      },
		{1, m->wx,            m->wy + th      },
		{1, m->wx + tw,       m->wy           },
		{1, m->wx + (tw * 2), m->wy           },
		{1, m->wx,            m->wy           },
		{1, m->wx + tw,       m->wy + (2 * th)},
		{1, m->wx + (tw * 2), m->wy + (2 * th)},
		{1, m->wx,            m->wy + (2 * th)}
    };

	if (ws != c->ws) {
		ws = c->ws;
		index = 0;
	}
	i = index;
	index = (index + 1) % LEN(q);
	*x = q[i][1] + (((*w - tw) * -1) / 2);
	*y = q[i][2] + (((*h - th) * -1) / 2);
	*x = CLAMP(*x, m->wx, m->wx + m->ww - (*w + (2 * c->bw)));
	*y = CLAMP(*y, m->wy, m->wy + m->wh - (*h + (2 * c->bw)));
}

void refresh(void)
{
	Desk *d;
	Panel *p;
	Client *c;
	Monitor *m;
	int x, y, w, h;

	for (m = monitors; m; m = m->next) {
		DBG("refresh: workspace: %d, monitor: %s layout: %s", m->ws->num + 1, m->name, m->ws->layout->name)
		if (m->ws->layout->func && m->ws->layout->func(m->ws) < 0) {
			m->ws->layout->func(m->ws);
		}
		for (c = m->ws->clients; c; c = c->next) {
			if (FULLSCREEN(c)) {
				resize(c, m->x, m->y, m->w, m->h, 0);
			} else if (FLOATING(c)) {
				resizehint(c, (x = c->x), (y = c->y), (w = c->w), (h = c->h), c->bw, 0, 0);
			}
			/* map last to avoid resizing while visible */
			winmap(c->win, &c->state);
		}
		for (p = panels; p; p = p->next) {
			if (p->mon == m) {
				setstackmode(p->win, XCB_STACK_MODE_BELOW);
			}
		}
		for (c = m->ws->stack; c; c = c->snext) {
			if (!FLOATING(c)) {
				setstackmode(c->win, XCB_STACK_MODE_BELOW);
			} else if (STATE(c, ABOVE)) {
				setstackmode(c->win, XCB_STACK_MODE_ABOVE);
			}
		}
		for (d = desks; d; d = d->next) {
			if (d->mon == m) {
				setstackmode(d->win, XCB_STACK_MODE_BELOW);
			}
		}
	}
	for (p = panels; p; p = p->next) {
		winmap(p->win, &p->state);
	}
	for (d = desks; d; d = d->next) {
		winmap(d->win, &d->state);
	}

	focus(NULL);
	if (selws->sel && FLOATING(selws->sel)) {
		setstackmode(selws->sel->win, XCB_STACK_MODE_ABOVE);
	}
	ignore(XCB_ENTER_NOTIFY);
	xcb_aux_sync(con);
	needsrefresh = 0;
}

void relocate(Client *c, Monitor *mon, Monitor *old)
{
	if (!FLOATING(c) || INRECT(c->x, c->y, c->w, c->h, mon->x, mon->y, mon->w, mon->h)) {
		return;
	}
	if (STATE(c, STICKY)) {
		Client *sel = lastws->sel == c ? c : selws->sel;
		setworkspace(c, old->ws, 0);
		focus(sel);
		return;
	}

	DBG("relocate: window: 0x%08x %s -- from %s to %s -- x: %d - y: %d - w: %d - h: %d", c->win, c->title,
		old->name, mon->name, c->x, c->y, c->w, c->h)

	if (STATE(c, FULLSCREEN) && c->w == old->w && c->h == old->h) {
		DBG("relocate: fullscreen window: 0x%08x %s -- x: %d -> %d - y: %d -> %d - w: "
			"%d -> %d - h: %d -> %d",
			c->win, c->title, c->x, mon->x, c->y, mon->y, c->w, mon->w, c->h, mon->h)
		c->x = mon->x, c->y = mon->y, c->w = mon->w, c->h = mon->h;
		return;
	}

	int corner = c->x == old->x && c->y == old->y;
	double xscale = mon->w > old->w ? (double)mon->w / (double)old->w : (double)old->w / (double)mon->w;
	double yscale = mon->h > old->h ? (double)mon->h / (double)old->h : (double)old->h / (double)mon->h;
	int nx = mon->w > old->w ? (c->x - old->x) * xscale : (c->x - old->x) / xscale;
	int ny = mon->h > old->h ? (c->y - old->y) * yscale : (c->y - old->y) / yscale;

	DBG("relocate: nx: %d - ny: %d xscale: %f - yscale: %f - x: %d -> %d - y: %d -> %d", nx, ny, xscale,
		yscale, c->x, mon->x + nx, c->y, mon->y + ny)
	c->w = mon->w > old->w ? c->w * xscale : c->w / xscale;
	c->h = mon->h > old->h ? c->h * yscale : c->h / yscale;
	c->x = mon->x + nx;
	c->y = mon->y + ny;
	applysizehints(c, &c->x, &c->y, &c->w, &c->h, c->bw, 0, 0);
	if (c->x > 0 && c->x < mon->x) {
		c->x = mon->x;
	}
	if (c->y > 0 && c->y < mon->y) {
		c->y = mon->y;
	}
	if (!corner && c->x == mon->x && c->y == mon->y) {
		gravitate(c, GRAV_CENTER, GRAV_CENTER, 1);
	}
	DBG("relocate: finale size/location of window: 0x%08x %s -- x: %d - y: %d - w: %d - h: %d", c->win,
		c->title, c->x, c->y, c->w, c->h)
}

static void relocatews(Workspace *ws, Monitor *old, int wasvis)
{
	Client *c;
	Monitor *mon;

	if (!(mon = ws->mon) || mon == old || ws != ws->mon->ws || !wasvis) {
		return;
	}
	DBG("relocatews: %d:%s -> %d:%s", old->ws->num + 1, old->name, mon->ws->num + 1, mon->name)
	for (c = ws->clients; c; c = c->next) {
		relocate(c, mon, old);
	}
}

void resize(Client *c, int x, int y, int w, int h, int bw)
{
	if (FLOATING(c) && !FULLSCREEN(c)) {
		c->old_x = c->x, c->old_y = c->y, c->old_w = c->w, c->old_h = c->h;
	}
	c->x = x, c->y = y, c->w = w, c->h = h;
	MOVERESIZE(c->win, x, y, w, h, bw);
	clientborder(c, c == selws->sel);
	sendconfigure(c);
	xcb_flush(con);
}

void resizehint(Client *c, int x, int y, int w, int h, int bw, int usermotion, int mouse)
{
	if (applysizehints(c, &x, &y, &w, &h, bw, usermotion, mouse)) {
		resize(c, x, y, w, h, bw);
	}
}

static int rulecmp(Client *c, Rule *r)
{
	return !((r->clss && regexec(&(r->clssreg), c->clss, 0, NULL, 0)) ||
			 (r->inst && regexec(&(r->instreg), c->inst, 0, NULL, 0)) ||
			 (r->title && regexec(&(r->titlereg), c->title, 0, NULL, 0)));
}

void sendconfigure(Client *c)
{
	xcb_configure_notify_event_t e = {
		.event = c->win,
		.window = c->win,
		.response_type = XCB_CONFIGURE_NOTIFY,
		.x = c->x,
		.y = c->y,
		.width = c->w,
		.height = c->h,
		.border_width = c->bw,
		.above_sibling = XCB_NONE,
		.override_redirect = 0,
	};
	xcb_send_event(con, 0, c->win, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (char *)&e);
}

int sendwmproto(Client *c, xcb_atom_t proto)
{
	int exists = 0;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t rpc;
	xcb_icccm_get_wm_protocols_reply_t p;

	rpc = xcb_icccm_get_wm_protocols(con, c->win, wmatom[WM_PROTO]);
	if (xcb_icccm_get_wm_protocols_reply(con, rpc, &p, &e)) {
		int n = p.atoms_len;
		while (!exists && n--) {
			exists = p.atoms[n] == proto;
		}
		xcb_icccm_get_wm_protocols_reply_wipe(&p);
	} else {
		iferr(0, "unable to get requested wm protocol", e);
	}
	if (exists) {
		xcb_client_message_event_t e = {.response_type = XCB_CLIENT_MESSAGE,
										.window = c->win,
										.type = wmatom[WM_PROTO],
										.format = 32,
										.data.data32[0] = proto,
										.data.data32[1] = XCB_TIME_CURRENT_TIME};
		iferr(0, "unable to send client message event",
			  xcb_request_check(con,
								xcb_send_event_checked(con, 0, c->win, XCB_EVENT_MASK_NO_EVENT, (char *)&e)));
		xcb_flush(con);
	}
	return exists;
}

void setfullscreen(Client *c, int fullscreen)
{
	Monitor *m;

	if (!c->ws || !(m = MON(c))) {
		m = selws->mon;
	}
	if (fullscreen && !STATE(c, FULLSCREEN)) {
		setnetstate(c->win, STATE_FULLSCREEN);
		c->old_state = c->state;
		c->old_x = c->x, c->old_y = c->y, c->old_w = c->w, c->old_h = c->h;
		c->state |= STATE_FULLSCREEN | STATE_FLOATING;
		if (c->bw || (c->state == STATE_NOBORDER))
			c->old_bw = c->bw;
		c->bw = 0;
		if (VISIBLE(c)) {
			resize(c, m->x, m->y, m->w, m->h, 0);
			setstackmode(c->win, XCB_STACK_MODE_ABOVE);
			refresh();
		}
	} else if (!fullscreen && STATE(c, FULLSCREEN)) {
		setnetstate(c->win, 0);
		c->state = c->old_state;
		c->bw = c->old_bw;
		if (VISIBLE(c)) {
			resizehint(c, c->old_x, c->old_y, c->old_w, c->old_h, c->bw, 0, 0);
			refresh();
		} else {
			c->x = c->old_x, c->y = c->old_y, c->w = c->old_w, c->h = c->old_h;
		}
	}
	xcb_flush(con);
}

void setinputfocus(Client *c)
{
	if (!STATE(c, NOINPUT)) {
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, c->win, XCB_CURRENT_TIME);
		PROP(REPLACE, root, netatom[NET_ACTIVE], XCB_ATOM_WINDOW, 32, 1, &c->win);
	}
	sendwmproto(c, wmatom[WM_FOCUS]);
}

void setnetwsnames(void)
{
	char *names;
	Workspace *ws;
	size_t len = 1;

	for (ws = workspaces; ws; ws = ws->next) {
		len += strlen(ws->name) + 1;
	}
	names = ecalloc(1, len);
	len = 0;
	for (ws = workspaces; ws; ws = ws->next) {
		for (uint32_t i = 0; (names[len++] = ws->name[i]); i++)
			;
	}
	PROP(REPLACE, root, netatom[NET_DESK_NAMES], wmatom[WM_UTF8STR], 8, --len, names);
	free(names);
}

void setstackmode(xcb_window_t win, uint32_t mode)
{
	xcb_configure_window(con, win, XCB_CONFIG_WINDOW_STACK_MODE, &mode);
}

void setnetstate(xcb_window_t win, uint32_t state)
{
	if (state & STATE_FULLSCREEN) {
		PROP(REPLACE, win, netatom[NET_WM_STATE], XCB_ATOM_ATOM, 32, 1, &netatom[NET_STATE_FULL]);
	} else {
		PROP(REPLACE, win, netatom[NET_WM_STATE], XCB_ATOM_ATOM, 32, 0, (const void *)0);
	}
}

void seturgent(Client *c, int urg)
{
	xcb_generic_error_t *e;
	xcb_icccm_wm_hints_t wmh;
	xcb_get_property_cookie_t pc;

	pc = xcb_icccm_get_wm_hints(con, c->win);
	if (urg && c != selws->sel) {
		c->state |= STATE_URGENT;
	} else if (!urg) {
		c->state &= ~STATE_URGENT;
	}
	if (xcb_icccm_get_wm_hints_reply(con, pc, &wmh, &e)) {
		wmh.flags =
			urg ? (wmh.flags | XCB_ICCCM_WM_HINT_X_URGENCY) : (wmh.flags & ~XCB_ICCCM_WM_HINT_X_URGENCY);
		xcb_icccm_set_wm_hints(con, c->win, &wmh);
	} else {
		iferr(0, "unable to get wm window hints", e);
	}
}

void setwinstate(xcb_window_t win, uint32_t state)
{
	uint32_t data[] = {state, XCB_ATOM_NONE};
	PROP(REPLACE, win, wmatom[WM_STATE], wmatom[WM_STATE], 32, 2, (const void *)data);
}

void setworkspace(Client *c, Workspace *ws, int stacktail)
{
	Client *tail = NULL;

	if (ws == c->ws) {
		return;
	}
	DBG("setworkspace: 0x%08x %s -> %d", c->win, c->title, ws->num + 1)
	if (c->ws) {
		detach(c, 0);
		detachstack(c);
	}
	c->ws = ws;
	PROP(REPLACE, c->win, netatom[NET_WM_DESK], XCB_ATOM_CARDINAL, 32, 1, &c->ws->num);
	attach(c, globalcfg[GLB_TILE_TOHEAD].val);
	if (stacktail) {
		for (tail = ws->stack; tail && tail->snext; tail = tail->snext)
			;
	}
	if (tail) {
		tail->snext = c;
		c->snext = NULL;
	} else {
		attachstack(c);
	}
	wschange = c->ws->clients->next ? wschange : 1;
}

void showhide(Client *c)
{
	if (!c) {
		return;
	}
	if (VISIBLE(c)) {
		Monitor *m = MON(c);
		DBG("showhide: ws: %d - showing window: 0x%08x %s", c->ws->num + 1, c->win, c->title)
		setwinstate(c->win, XCB_ICCCM_WM_STATE_NORMAL);
		if (FULLSCREEN(c)) {
			MOVERESIZE(c->win, m->x, m->y, m->w, m->h, 0);
		} else if (FLOATING(c)) {
			resize(c, c->x, c->y, c->w, c->h, c->bw);
		} else if (c == c->ws->sel || c->ws->layout->func != mono) {
			MOVE(c->win, c->x, c->y);
		}
		showhide(c->snext);
	} else {
		showhide(c->snext);
		if (!STATE(c, STICKY)) {
			DBG("showhide: ws: %d - hiding window: 0x%08x %s", c->ws->num + 1, c->win, c->title)
			setwinstate(c->win, XCB_ICCCM_WM_STATE_ICONIC);
			MOVE(c->win, W(c) * -2, c->y);
		} else if (c->ws != selws && MON(c) == selws->mon) {
			DBG("showhide: ws: %d -- not hiding sticky window: 0x%08x %s", c->ws->num + 1, c->win, c->title)
			Client *sel = lastws->sel == c ? c : selws->sel;
			setworkspace(c, selws, 0);
			focus(sel);
		}
	}
}

void sighandle(int sig)
{
	if (sig == SIGCHLD) {
		signal(sig, sighandle);
		while (waitpid(-1, 0, WNOHANG) > 0)
			;
	} else if (sig == SIGINT || sig == SIGTERM || sig == SIGHUP) {
		running = 0;
	}
}

void sizehints(Client *c, int uss)
{
	xcb_size_hints_t s;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t pc;

	DBG("sizehints: client: 0x%08x %s", c->win, c->title)
	pc = xcb_icccm_get_wm_normal_hints(con, c->win);
	c->inc_w = c->inc_h = 0;
	c->max_aspect = c->min_aspect = 0.0;
	c->min_w = c->min_h = c->max_w = c->max_h = c->base_w = c->base_h = 0;
	if (xcb_icccm_get_wm_normal_hints_reply(con, pc, &s, &e)) {
		if (uss && s.flags & XCB_ICCCM_SIZE_HINT_US_SIZE) {
			c->w = s.width, c->h = s.height;
		}
		if (uss && s.flags & XCB_ICCCM_SIZE_HINT_US_POSITION) {
			c->x = s.x, c->y = s.y;
		}
		if (s.flags & XCB_ICCCM_SIZE_HINT_P_ASPECT) {
			c->min_aspect = (float)s.min_aspect_den / s.min_aspect_num;
			c->max_aspect = (float)s.max_aspect_num / s.max_aspect_den;
		}
		if (s.flags & XCB_ICCCM_SIZE_HINT_P_MAX_SIZE) {
			c->max_w = s.max_width, c->max_h = s.max_height;
		}
		if (s.flags & XCB_ICCCM_SIZE_HINT_P_RESIZE_INC) {
			c->inc_w = s.width_inc, c->inc_h = s.height_inc;
		}
		if (s.flags & XCB_ICCCM_SIZE_HINT_BASE_SIZE) {
			c->base_w = s.base_width, c->base_h = s.base_height;
		} else if (s.flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) {
			c->base_w = s.min_width, c->base_h = s.min_height;
		}
		if (s.flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) {
			c->min_w = s.min_width, c->min_h = s.min_height;
		} else if (s.flags & XCB_ICCCM_SIZE_HINT_BASE_SIZE) {
			c->min_w = s.base_width, c->min_h = s.base_height;
		}
	} else {
		iferr(0, "unable to get wm normal hints", e);
	}
	DBG("sizehints: client: 0x%08x %s -- max_w = %d, max_h = %d, min_w = %d, min_h = %d", c->win, c->title,
		c->max_w, c->max_h, c->min_w, c->min_h)
	if (c->max_w && c->max_w == c->min_w && c->max_h && c->max_h == c->min_h) {
		DBG("sizehints: FIXED client: 0x%08x %s", c->win, c->title)
		c->state |= (STATE_FIXED | STATE_FLOATING);
	}
	c->hints = 1;
}

static Client *termforwin(const Client *w)
{
	Client *c;
	Workspace *ws;

	if (!w->pid || STATE(w, TERMINAL)) {
		return NULL;
	}
	for (ws = workspaces; ws; ws = ws->next) {
		for (c = ws->clients; c; c = c->next) {
			if (STATE(c, TERMINAL) && !c->absorbed && c->pid && discreteproc(c->pid, w->pid)) {
				return c;
			}
		}
	}
	for (c = scratch.clients; c; c = c->next) {
		if (STATE(c, TERMINAL) && !c->absorbed && c->pid && discreteproc(c->pid, w->pid)) {
			return c;
		}
	}
	return NULL;
}

int tilecount(Workspace *ws)
{
	int i;
	Client *c;

	for (i = 0, c = nexttiled(ws->clients); c; c = nexttiled(c->next), i++)
		;
	return i;
}

void unfocus(Client *c, int focusroot)
{
	if (c) {
		DBG("unfocus: 0x%08x %s", c->win, c->title)
		clientborder(c, 0);
	}
	if (focusroot) {
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
		xcb_delete_property(con, root, netatom[NET_ACTIVE]);
	}
}

void unmanage(xcb_window_t win, int destroyed)
{
	Desk *d;
	Panel *p;
	Client *c, *s = NULL;
	void *ptr;

	/* window we're handling is actually absorbed */
	if ((c = absorbingclient(win)) && win != c->absorbed->win) {
		DBG("unmanage: 0x%08x is absorbed by 0x%08x", win, c->absorbed->win)
		win = c->absorbed->win;
	}

	if ((ptr = c = wintoclient(win))) {
		DBG("unmanage: client: 0x%08x %s", c->win, c->title)
		if (c->absorbed) {
			desorb(c);
			return;
		}
		if ((s = absorbingclient(c->win))) {
			free(s->absorbed);
			s->absorbed = NULL;
			refresh();
			return;
		}
		if (c->cb && running) {
			c->cb->func(c, 1);
		}
		wschange = c->ws->clients->next ? wschange : 1;
		detach(c, 0);
		detachstack(c);
	} else if ((ptr = p = wintopanel(win))) {
		DBG("unmanage: panel: 0x%08x %s", p->win, p->clss)
		Panel **pp = &panels;
		DETACH(p, pp);
		updstruts();
	} else if ((ptr = d = wintodesk(win))) {
		DBG("unmanage: desktop: 0x%08x %s", d->win, d->clss)
		Desk **dd = &desks;
		DETACH(d, dd);
	}

	if (!destroyed) {
		DBG("unmanage: 0x%08x was NOT destroyed", win)
		xcb_grab_server(con);
		if (c) {
			xcb_configure_window(con, c->win, XCB_CONFIG_WINDOW_BORDER_WIDTH, &c->old_bw);
			xcb_ungrab_button(con, XCB_BUTTON_INDEX_ANY, c->win, XCB_MOD_MASK_ANY);
			if (running) {
				xcb_delete_property(con, c->win, netatom[NET_WM_STATE]);
				xcb_delete_property(con, c->win, netatom[NET_WM_DESK]);
			}
		}
		setwinstate(win, XCB_ICCCM_WM_STATE_WITHDRAWN);
		xcb_aux_sync(con);
		xcb_ungrab_server(con);
	} else {
		DBG("unmanage: 0x%08x was destroyed", win)
		xcb_flush(con);
	}

	if (ptr) {
		free(ptr);
		updatenetclients();
		if (!s) {
			refresh();
		}
	}
}

static void updatenetclients(void)
{
	Panel *p;
	Desk * d;
	Client *c;
	Workspace *ws;

	xcb_delete_property(con, root, netatom[NET_CLIENTS]);
	for (ws = workspaces; ws; ws = ws->next) {
		for (c = ws->clients; c; c = c->next) {
			PROP(APPEND, root, netatom[NET_CLIENTS], XCB_ATOM_WINDOW, 32, 1, &c->win);
		}
	}
	for (c = scratch.clients; c; c = c->next) {
		PROP(APPEND, root, netatom[NET_CLIENTS], XCB_ATOM_WINDOW, 32, 1, &c->win);
	}
	for (p = panels; p; p = p->next) {
		PROP(APPEND, root, netatom[NET_CLIENTS], XCB_ATOM_WINDOW, 32, 1, &p->win);
	}
	for (d = desks; d; d = d->next) {
		PROP(APPEND, root, netatom[NET_CLIENTS], XCB_ATOM_WINDOW, 32, 1, &d->win);
	}
}

static void updnetworkspaces(void)
{
	int v[4];
	Workspace *ws;

	xcb_delete_property(con, root, netatom[NET_DESK_VP]);
	xcb_delete_property(con, root, netatom[NET_DESK_WA]);
	v[0] = scr_w, v[1] = scr_h;
	PROP(REPLACE, root, netatom[NET_DESK_GEOM], XCB_ATOM_CARDINAL, 32, 2, &v);
	PROP(REPLACE, root, netatom[NET_DESK_NUM], XCB_ATOM_CARDINAL, 32, 1, &globalcfg[GLB_NUM_WS]);
	for (ws = workspaces; ws; ws = ws->next) {
		if (!ws->mon) {
			ws->mon = primary;
		}
		v[0] = ws->mon->x, v[1] = ws->mon->y;
		PROP(APPEND, root, netatom[NET_DESK_VP], XCB_ATOM_CARDINAL, 32, 2, &v);
		v[0] = ws->mon->wx, v[1] = ws->mon->wy, v[2] = ws->mon->ww, v[3] = ws->mon->wh;
		PROP(APPEND, root, netatom[NET_DESK_WA], XCB_ATOM_CARDINAL, 32, 4, &v);
	}
}

static int updoutputs(xcb_randr_output_t *outs, int nouts, xcb_timestamp_t t)
{
	Desk *d;
	Monitor *m;
	uint32_t n;
	char name[64];
	int i, nmons, changed = 0;
	xcb_generic_error_t *e;
	xcb_randr_get_crtc_info_cookie_t ck;
	xcb_randr_get_crtc_info_reply_t *crtc;
	xcb_randr_get_output_info_reply_t *o;
	xcb_randr_get_output_info_cookie_t oc[nouts];
	xcb_randr_get_output_primary_cookie_t pc;
	xcb_randr_get_output_primary_reply_t *po;

	DBG("updoutputs: checking %d outputs for changes", nouts)
	for (i = 0; i < nouts; i++) {
		oc[i] = xcb_randr_get_output_info(con, outs[i], t);
	}
	for (i = 0, nmons = 0; i < nouts; i++) {
		if (!(o = xcb_randr_get_output_info_reply(con, oc[i], &e)) || o->crtc == XCB_NONE) {
			iferr(0, "unable to get output info or output has no crtc", e);
		} else if (o->connection == XCB_RANDR_CONNECTION_CONNECTED) {
			ck = xcb_randr_get_crtc_info(con, o->crtc, t);
			crtc = xcb_randr_get_crtc_info_reply(con, ck, &e);
			if (!crtc || !xcb_randr_get_crtc_info_outputs_length(crtc)) {
				iferr(0, "unable to get crtc info reply", e);
				goto next;
			}
			n = xcb_randr_get_output_info_name_length(o) + 1;
			strlcpy(name, (char *)xcb_randr_get_output_info_name(o), MIN(sizeof(name), n));
			for (m = monitors; m; m = m->next) {
				if (outs[i] != m->id && crtc->x >= m->x && crtc->y >= m->y && crtc->x < m->x + m->w &&
					crtc->y < m->y + m->h) {
					DBG("updoutput: %s is a clone of %s", name, m->name)
					goto next;
				}
			}
			if ((m = outputtomon(outs[i]))) {
				if (!m->connected || crtc->x != m->x || crtc->y != m->y || crtc->width != m->w ||
					crtc->height != m->h) {
					changed = 1;
				}
				m->x = m->wx = crtc->x;
				m->y = m->wy = crtc->y;
				m->w = m->ww = crtc->width;
				m->h = m->wh = crtc->height;
				m->connected = (m->w != 0 && m->h != 0 && crtc->mode != XCB_NONE);
			} else {
				initmon(nmons++, name, outs[i], crtc->x, crtc->y, crtc->width, crtc->height);
				changed = 1;
			}
			DBG("updoutputs: %s - %d,%d @ %dx%d - changed: %d", name, crtc->x, crtc->y, crtc->width,
				crtc->height, changed)
next:
			free(crtc);
		} else if (o->connection == XCB_RANDR_CONNECTION_DISCONNECTED && (m = outputtomon(outs[i])) &&
				   m->connected) {
			changed = 1;
			m->num = -1;
			m->connected = 0;
		}
		free(o);
	}

	if (changed) {
		pc = xcb_randr_get_output_primary(con, root);
		if (!(po = xcb_randr_get_output_primary_reply(con, pc, NULL)) ||
			!(primary = outputtomon(po->output))) {
			primary = nextmon(monitors);
		}
		free(po);

		for (d = desks; d; d = d->next) {
			if (!d->mon->connected) {
				int bw = 0;
				xcb_get_geometry_reply_t *g = NULL;
				if ((g = wingeom(d->win))) {
					bw = g->border_width;
				}
				d->mon = primary;
				MOVERESIZE(d->win, d->mon->x, d->mon->y, d->mon->w, d->mon->h, bw);
				setstackmode(d->win, XCB_STACK_MODE_BELOW);
			}
		}
	}
	return changed;
}

int updrandr(int init)
{
	int changed = 0;
	xcb_generic_error_t *e;
	xcb_randr_get_screen_resources_reply_t *r;
	xcb_randr_get_screen_resources_cookie_t rc;

	rc = xcb_randr_get_screen_resources(con, root);
	if ((r = xcb_randr_get_screen_resources_reply(con, rc, &e))) {
		int n;
		if ((n = xcb_randr_get_screen_resources_outputs_length(r)) <= 0) {
			warnx("no monitors available");
		} else {
			changed = updoutputs(xcb_randr_get_screen_resources_outputs(r), n, r->config_timestamp);
		}
		if (!init) {
			uint32_t rm = monitors->next ? (rootmask | XCB_EVENT_MASK_POINTER_MOTION) : rootmask;
			xcb_change_window_attributes(con, root, XCB_CW_EVENT_MASK, &rm);
		}
		free(r);
	} else {
		iferr(0, "unable to get screen resources", e);
	}
	return changed;
}

void updstruts(void)
{
	Panel *p;
	Monitor *m;

	for (m = monitors; m; m = m->next) {
		m->wx = m->x, m->wy = m->y, m->ww = m->w, m->wh = m->h;
	}
	for (p = panels; p; p = p->next) {
		if (p->l || p->r || p->t || p->b) {
			/* adjust the struts if they don't match up with the panel size and location */
			if (p->l && p->l > p->w && p->x == p->mon->x) {
				p->l = p->w;
			}
			if (p->r && p->r > p->w && p->x + p->w == p->mon->x + p->mon->w) {
				p->r = p->w;
			}
			if (p->t && p->t > p->h && p->y == p->mon->y) {
				p->t = p->h;
			}
			if (p->b && p->b > p->h && p->y + p->h == p->mon->y + p->mon->h) {
				p->b = p->h;
			}
			applypanelstrut(p);
		}
	}
	updnetworkspaces();
	needsrefresh = 1;
}

void updworkspaces(int needed)
{
	int n;
	Monitor *m;
	Workspace *ws;

	for (n = 0, m = nextmon(monitors); m; m = nextmon(m->next), n++)
		;
	if (n < 1 || n > 256 || needed > 256) {
		warnx(n < 1 ? "no connected monitors" : "allocating too many workspaces: max 256");
		return;
	}
	while (n > globalcfg[GLB_NUM_WS].val || needed > globalcfg[GLB_NUM_WS].val) {
		initws(globalcfg[GLB_NUM_WS].val);
		globalcfg[GLB_NUM_WS].val++;
	}

	m = nextmon(monitors);
	for (ws = workspaces; ws; ws = ws->next) {
		m->ws = m->ws ? m->ws : ws;
		ws->mon = m;
		DBG("updworkspaces: %d:%s -> %s - visible: %d", ws->num, ws->name, m->name, ws == m->ws)
		if (!(m = nextmon(m->next))) {
			m = nextmon(monitors);
		}
	}
	updstruts();
	setnetwsnames();
	wschange = 1;
	needsrefresh = 1;
}

static xcb_get_window_attributes_reply_t *winattr(xcb_window_t win)
{
	xcb_generic_error_t *e;
	xcb_get_window_attributes_reply_t *wa = NULL;

	if (win == XCB_WINDOW_NONE) {
		return wa;
	}
	if (!(wa = xcb_get_window_attributes_reply(con, xcb_get_window_attributes(con, win), &e))) {
		iferr(0, "unable to get window geometry reply", e);
	}
	return wa;
}

static void winclass(xcb_window_t win, char *clss, char *inst, size_t len)
{
	/* it is assumed that class and inst are allocated and the same size */
	xcb_generic_error_t *e;
	xcb_icccm_get_wm_class_reply_t p;

	if (!xcb_icccm_get_wm_class_reply(con, xcb_icccm_get_wm_class(con, win), &p, &e)) {
		iferr(0, "unable to get window class", e);
		strlcpy(clss, "broken", len);
		strlcpy(inst, "broken", len);
	} else {
		strlcpy(clss, strlen(p.class_name) ? p.class_name : "broken", len);
		strlcpy(inst, strlen(p.instance_name) ? p.instance_name : "broken", len);
		xcb_icccm_get_wm_class_reply_wipe(&p);
	}
}

static xcb_get_geometry_reply_t *wingeom(xcb_window_t win)
{
	xcb_generic_error_t *e;
	xcb_get_geometry_reply_t *g = NULL;

	if (win == XCB_WINDOW_NONE) {
		return g;
	}
	if (!(g = xcb_get_geometry_reply(con, xcb_get_geometry(con, win), &e))) {
		iferr(0, "unable to get window geometry reply", e);
	}
	return g;
}

void winmap(xcb_window_t win, uint32_t *state)
{
	if ((*state & STATE_NEEDSMAP)) {
		DBG("winmap: 0x%08x", win)
		setwinstate(win, XCB_ICCCM_WM_STATE_NORMAL);
		xcb_map_window(con, win);
		*state &= ~STATE_NEEDSMAP;
		xcb_aux_sync(con);
	}
}

pid_t winpid(xcb_window_t win)
{
	pid_t result = 0;
	xcb_generic_error_t *e = NULL;
	xcb_res_query_client_ids_reply_t *r;
	xcb_res_client_id_value_iterator_t i;
	xcb_res_client_id_spec_t spec = {
		.client = win,
		.mask = XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID,
	};

	if (!(r = xcb_res_query_client_ids_reply(con, xcb_res_query_client_ids(con, 1, &spec), &e))) {
		iferr(0, "unable to get client ids", e);
		return 0;
	}
	for (i = xcb_res_query_client_ids_ids_iterator(r); i.rem; xcb_res_client_id_value_next(&i)) {
		if (i.data->spec.mask & XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID) {
			uint32_t *t = xcb_res_client_id_value_value(i.data);
			result = *t;
			break;
		}
	}
	free(r);
	return result == -1 ? 0 : result;
}

static int winprop(xcb_window_t win, xcb_atom_t prop, xcb_atom_t *ret)
{
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t c;
	xcb_get_property_reply_t *r = NULL;

	c = xcb_get_property(con, 0, win, prop, XCB_ATOM_ANY, 0, 1);
	if ((r = xcb_get_property_reply(con, c, &e)) && r->value_len) {
		*ret = *(xcb_atom_t *)xcb_get_property_value(r);
		free(r);
		return 1;
	} else {
		iferr(0, "unable to get window property reply", e);
	}
	free(r);
	return 0;
}

Client *wintoclient(xcb_window_t win)
{
	Client *c;
	Workspace *ws;

	if (win != XCB_WINDOW_NONE && win != root) {
		for (ws = workspaces; ws; ws = ws->next) {
			for (c = ws->clients; c; c = c->next) {
				if (c->win == win) {
					return c;
				}
			}
		}
		for (c = scratch.clients; c; c = c->next) {
			if (c->win == win) {
				return c;
			}
		}
	}
	return NULL;
}

Desk *wintodesk(xcb_window_t win)
{
	if (win != XCB_WINDOW_NONE && win != root) {
		for (Desk *d = desks; d; d = d->next) {
			if (d->win == win) {
				return d;
			}
		}
	}
	return NULL;
}

Panel *wintopanel(xcb_window_t win)
{
	if (win != XCB_WINDOW_NONE && win != root) {
		for (Panel *p = panels; p; p = p->next) {
			if (p->win == win) {
				return p;
			}
		}
	}
	return NULL;
}

xcb_window_t wintrans(xcb_window_t win)
{
	xcb_window_t w;
	xcb_generic_error_t *e;

	if (!xcb_icccm_get_wm_transient_for_reply(con, xcb_icccm_get_wm_transient_for(con, win), &w, &e)) {
		iferr(0, "unable to get wm transient for hint", e);
		return XCB_WINDOW_NONE;
	}
	return w;
}

void winunmap(xcb_window_t win)
{
	DBG("winunmap: 0x%08x", win)
	xcb_get_window_attributes_reply_t *ra = winattr(root), *ca = winattr(win);
	uint32_t rm = (ra->your_event_mask & ~XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY);
	uint32_t cm = (ca->your_event_mask & ~XCB_EVENT_MASK_STRUCTURE_NOTIFY);

	xcb_grab_server(con);
	xcb_change_window_attributes(con, root, XCB_CW_EVENT_MASK, &rm);
	xcb_change_window_attributes(con, win, XCB_CW_EVENT_MASK, &cm);
	xcb_unmap_window(con, win);
	setwinstate(win, XCB_ICCCM_WM_STATE_WITHDRAWN);
	xcb_change_window_attributes(con, root, XCB_CW_EVENT_MASK, &ra->your_event_mask);
	xcb_change_window_attributes(con, win, XCB_CW_EVENT_MASK, &ca->your_event_mask);
	xcb_aux_sync(con);
	xcb_ungrab_server(con);
}

#ifdef FUNCDEBUG
void __cyg_profile_func_enter(void *fn, void *caller)
{
	int i;
	Dl_info info;

	fprintf(stderr, "dk:");
	for (i = 0; i < depth; i += 2) {
		fprintf(stderr, " |");
	}
	if (dladdr(fn, &info)) {
		fprintf(stderr, " --> %s (%p)", info.dli_sname ? info.dli_sname : "unknown", fn);
	} else {
		fprintf(stderr, " --> (%p)", fn);
	}
	if (dladdr(caller, &info)) {
		fprintf(stderr, " :: %s (%p)\n", info.dli_sname ? info.dli_sname : "unknown", caller);
	} else {
		fprintf(stderr, " :: (%p)\n", caller);
	}
	depth += 2;
}

void __cyg_profile_func_exit(void *fn, void *caller)
{
	int i;
	Dl_info info;

	depth -= 2;
	fprintf(stderr, "dk:");
	for (i = depth; i > 0; i -= 2) {
		fprintf(stderr, " |");
	}
	if (dladdr(fn, &info)) {
		fprintf(stderr, " <-- %s (%p)", info.dli_sname ? info.dli_sname : "unknown", fn);
	} else {
		fprintf(stderr, " <-- (%p)", fn);
	}
	if (dladdr(caller, &info)) {
		fprintf(stderr, " :: %s (%p)\n", info.dli_sname ? info.dli_sname : "unknown", caller);
	} else {
		fprintf(stderr, " :: (%p)\n", caller);
	}
}
#endif
