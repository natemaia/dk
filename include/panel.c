/* ewmh panel support functions */

static void attachpanel(Panel *p);
static void initpanel(xcb_window_t win);
static Panel *wintopanel(xcb_window_t win);
static void freepanel(Panel *panel, int destroyed);
static void updatestruts(Panel *p, int apply);

void applypanelstrut(Panel *p)
{
	DBG("%s window area before: %d,%d @ %dx%d", p->mon->name, p->mon->winarea_x,
			p->mon->winarea_y, p->mon->winarea_w, p->mon->winarea_h);
	if (p->mon->x + p->strut_l > p->mon->winarea_x)
		p->mon->winarea_x = p->strut_l;
	if (p->mon->y + p->strut_t > p->mon->winarea_y)
		p->mon->winarea_y = p->strut_t;
	if (p->mon->w - (p->strut_r + p->strut_l) < p->mon->winarea_w)
		p->mon->winarea_w = p->mon->w - (p->strut_r + p->strut_l);
	if (p->mon->h - (p->strut_b + p->strut_t) < p->mon->winarea_h)
		p->mon->winarea_h = p->mon->h - (p->strut_b + p->strut_t);
	DBG("%s window area after: %d,%d @ %dx%d", p->mon->name, p->mon->winarea_x,
			p->mon->winarea_y, p->mon->winarea_w, p->mon->winarea_h);
}

void attachpanel(Panel *p)
{
	p->next = panels;
	panels = p;
}

void freepanel(Panel *p, int destroyed)
{
	Panel **pp = &panels;

	DBG("freeing panel: %d", p->win)
	while (*pp && *pp != p)
		pp = &(*pp)->next;
	*pp = p->next;
	if (!destroyed) {
		xcb_grab_server(con);
		setwinstate(p->win, XCB_ICCCM_WM_STATE_WITHDRAWN);
		xcb_aux_sync(con);
		xcb_ungrab_server(con);
	}
	updatestruts(p, 0);
	free(p);
	layoutws(NULL, 0);
}

void initpanel(xcb_window_t win)
{
	Panel *p;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t rc;
	int *s, x = 0, y = 0, w, h, bw;
	xcb_get_property_reply_t *r = NULL;

	DBG("initializing new panel from window: %d", win)
	p = ecalloc(1, sizeof(Panel));
	p->win = win;
	if (windowgeom(p->win, &x, &y, &w, &h, &bw))
		p->x = x, p->y = y, p->w = w, p->h = h;
	p->mon = ptrtomon(x, y);

	rc = xcb_get_property(con, 0, p->win, netatoms[StrutPartial], XCB_ATOM_CARDINAL, 0, 4);
	DBG("checking panel for _NET_WM_STRUT_PARTIAL or _NET_WM_STRUT")
	if (!(r = xcb_get_property_reply(con, rc, &e)) || r->type == XCB_NONE) {
		checkerror("unable to get _NET_WM_STRUT_PARTIAL from window", e);
		rc = xcb_get_property(con, 0, p->win, netatoms[Strut], XCB_ATOM_CARDINAL, 0, 4);
		if (!(r = xcb_get_property_reply(con, rc, &e)))
			checkerror("unable to get _NET_WM_STRUT or _NET_WM_STRUT_PARTIAL from window", e);
	}
	if (r) {
		if (r->value_len && (s = xcb_get_property_value(r))) {
			DBG("panel window has struts: %d, %d, %d, %d", s[0], s[1], s[2], s[3])
			p->strut_l = s[0], p->strut_r = s[1], p->strut_t = s[2], p->strut_b = s[3];
			updatestruts(p, 1);
		}
		free(r);
	}

	attachpanel(p);
	xcb_change_window_attributes(con, p->win, XCB_CW_EVENT_MASK,
			(uint []){XCB_EVENT_MASK_PROPERTY_CHANGE|XCB_EVENT_MASK_STRUCTURE_NOTIFY});
	xcb_map_window(con, p->win);
	layoutws(NULL, 0);
	DBG("new panel mapped -- monitor: %s -- geometry: %d,%d @ %dx%d",
			p->mon->name, p->x, p->y, p->w, p->h)
}

void updatestruts(Panel *p, int apply)
{
	Panel *n;
	Monitor *m;
	
	DBG("resetting struts for each monitor")
	FOR_EACH(m, monitors) {
		m->winarea_x = m->x, m->winarea_y = m->y, m->winarea_w = m->w, m->winarea_h = m->h;
	}
	if (!p)
		return;
	if (apply && !panels)
		applypanelstrut(p);
	DBG("applying each panel strut where needed")
	FOR_EACH(n, panels)
		if ((apply || n != p) && (n->strut_l || n->strut_r || n->strut_t || n->strut_b))
			applypanelstrut(p);
}

Panel *wintopanel(xcb_window_t win)
{
	Panel *p;

	if (win == root)
		return NULL;
	FOR_EACH(p, panels)
		if (p->win == win)
			break;
	return p;
}
