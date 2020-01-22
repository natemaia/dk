/* xcb randr support functions */

#ifdef RANDR

static Monitor *randrclone(xcb_randr_output_t id, int x, int y, int w, int h);
static Monitor *randrtomon(xcb_randr_output_t id);
static int initmons(xcb_randr_output_t *outputs, int len);
static int initrandr(void);

int initmons(xcb_randr_output_t *outs, int len)
{
	uint n;
    char name[256];
	int i, changed = 0;
	Monitor *m, *e, *clone;
    xcb_randr_get_output_info_reply_t *o;
    xcb_randr_get_crtc_info_reply_t *crtc;
    xcb_randr_get_output_info_cookie_t oc[len];

	DBG("%d outputs, requesting info for each")
	for (i = 0; i < len; i++)
		oc[i] = xcb_randr_get_output_info(con, outs[i], XCB_CURRENT_TIME);
	for (i = 0; i < len; i++) {
		if (!(o = xcb_randr_get_output_info_reply(con, oc[i], NULL))) {
			DBG("unable to get monitor info for output: %d", outs[i])
			continue;
		}

		FOR_TAIL(m, monitors);

		if (o->crtc != XCB_NONE && (crtc = xcb_randr_get_crtc_info_reply(con,
						xcb_randr_get_crtc_info(con, o->crtc, XCB_CURRENT_TIME), NULL))) {
			n = xcb_randr_get_output_info_name_length(o) + 1;
			strlcpy(name, (const char *)xcb_randr_get_output_info_name(o), MIN(sizeof(name), n));

			DBG("crtc: %s -- location: %d,%d -- size: %dx%d -- status: %d",
					name, crtc->x, crtc->y, crtc->width, crtc->height, crtc->status)
			if ((clone = randrclone(outs[i], crtc->x, crtc->y, crtc->width, crtc->height))) {
				DBG("monitor %s, id %d is a clone of %s, id %d, skipping",
						name, outs[i], clone->name, clone->id)
			} else if ((e = randrtomon(outs[i]))) {
				DBG("previously initialized monitor: %s -- location and size: %d,%d @ %dx%d",
						e->name, e->x, e->y, e->w, e->h)
				changed = crtc->x != e->x || crtc->y != e->y || crtc->width != e->w || crtc->height != e->h;
				if (crtc->x != e->x)
					e->x = e->winarea_x = crtc->x;
				if (crtc->y != e->y)
					e->y = e->winarea_y = crtc->y;
				if (crtc->width != e->w)
					e->w = e->winarea_w = crtc->width;
                if (crtc->height != e->h)
					e->h = e->winarea_h = crtc->height;
				if (m)
					m->next = e;
				else
					monitors = e;
				if (changed) {
					DBG("new size and location for monitor: %s -- %d,%d @ %dx%d", e->name, e->x, e->y, e->w, e->h)
				} else {
					DBG("size and location of monitor: %s -- unchanged", e->name)
				}
			} else {
				if (m)
					m->next = initmon(name, outs[i], crtc->x, crtc->y, crtc->width, crtc->height);
				else
					monitors = initmon(name, outs[i], crtc->x, crtc->y, crtc->width, crtc->height);
				changed = 1;
            }
            free(crtc);
		} else if (o->crtc == XCB_NONE && (e = randrtomon(outs[i]))) {
			DBG("previously initialized monitor is now inactive: %s -- freeing", e->name)
			freemon(e);
			changed = 1;
		}
        free(o);
    }
	if (!monitors) {
		DBG("RANDR extension is active but no monitors were initialized, using entire screen")
		return -1;
	}
	return changed;
}

int initrandr(void)
{
	int changed;
    const xcb_query_extension_reply_t *ext;
    xcb_randr_get_screen_resources_current_reply_t *r;
    xcb_randr_get_screen_resources_current_cookie_t rc;

	ext = xcb_get_extension_data(con, &xcb_randr_id);
	rc = xcb_randr_get_screen_resources_current(con, root);
	if (!ext || !ext->present) {
		warnx("RANDR extension is not available, using entire screen");
		return (randr = -1);
	} else if (!(r = xcb_randr_get_screen_resources_current_reply(con, rc, NULL))) {
		warnx("unable to get screen resources, using entire screen");
		return (randr = -1);
	}
	changed = initmons(xcb_randr_get_screen_resources_current_outputs(r),
			xcb_randr_get_screen_resources_current_outputs_length(r));
	free(r);
	if (changed < 0)
		return (randr = changed);
	randr = ext->first_event;
	xcb_randr_select_input(con, root, XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE
			|XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE|XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE
			|XCB_RANDR_NOTIFY_MASK_OUTPUT_PROPERTY);
	DBG("RANDR extension active and monitor(s) initialized -- extension base: %d", randr)
	return changed;
}

Monitor *randrclone(xcb_randr_output_t id, int x, int y, int w, int h)
{
	Monitor *m;

	FOR_EACH(m, monitors)
		if (id != m->id && m->x == x && m->y == y && m->w == w && m->h == h)
			return m;
	return m;
}

Monitor *randrtomon(xcb_randr_output_t id)
{
	Monitor *m;

	FOR_EACH(m, monitors)
		if (m->id == id)
			return m;
	return m;
}

#endif
