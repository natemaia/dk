/* xcb randr support functions */

#ifdef RANDR


static Monitor *randrclone(xcb_randr_output_t id, int x, int y, int w, int h);
static Monitor *outputtomon(xcb_randr_output_t id);
static int updateoutputs(xcb_randr_output_t *outputs, int len, xcb_timestamp_t timestamp);
static int updaterandr(void);
static int initrandr(void);

int updateoutputs(xcb_randr_output_t *outs, int len, xcb_timestamp_t timestamp)
{
	uint n;
	Monitor *m;
    char name[64];
	int i, changed = 0;
    xcb_randr_get_output_info_reply_t *o;
    xcb_randr_get_crtc_info_reply_t *crtc;
    xcb_randr_get_output_info_cookie_t oc[len];

	DBG("%d outputs, requesting info for each")
	for (i = 0; i < len; i++)
		oc[i] = xcb_randr_get_output_info(con, outs[i], timestamp);
	for (i = 0; i < len; i++) {
		if (!(o = xcb_randr_get_output_info_reply(con, oc[i], NULL))) {
			DBG("unable to get monitor info for output: %d", outs[i])
			continue;
		}

		if (o->crtc != XCB_NONE) {
			if (!(crtc = xcb_randr_get_crtc_info_reply(con, xcb_randr_get_crtc_info(con, o->crtc, timestamp), NULL))) {
				DBG("crtc info for randr output %d was NULL -- returning error", outs[i])
				free(o);
				return -1;
			}

			n = xcb_randr_get_output_info_name_length(o) + 1;
			strlcpy(name, (const char *)xcb_randr_get_output_info_name(o), MIN(sizeof(name), n));
			DBG("crtc: %s -- location: %d,%d -- size: %dx%d -- status: %d", name, crtc->x, crtc->y, crtc->width, crtc->height, crtc->status)

			if ((m = randrclone(outs[i], crtc->x, crtc->y, crtc->width, crtc->height))) {
				DBG("monitor %s, id %d is a clone of %s, id %d, skipping", name, outs[i], m->name, m->id)
			} else if ((m = outputtomon(outs[i]))) {
				DBG("previously initialized monitor: %s -- location and size: %d,%d @ %dx%d", m->name, m->x, m->y, m->w, m->h)
				changed = crtc->x != m->x || crtc->y != m->y || crtc->width != m->w || crtc->height != m->h;
				if (crtc->x != m->x)      m->x = m->winarea_x = crtc->x;
				if (crtc->y != m->y)      m->y = m->winarea_y = crtc->y;
				if (crtc->width != m->w)  m->w = m->winarea_w = crtc->width;
				if (crtc->height != m->h) m->h = m->winarea_h = crtc->height;
				DBG("size and location for monitor: %s -- %d,%d @ %dx%d -- %s", m->name, m->x, m->y, m->w, m->h, changed ? "updated" : "unchanged")
			} else {
				FOR_TAIL(m, monitors);
				if (m)
					m->next = initmon(name, outs[i], crtc->x, crtc->y, crtc->width, crtc->height);
				else
					monitors = initmon(name, outs[i], crtc->x, crtc->y, crtc->width, crtc->height);
				changed = 1;
			}
			free(crtc);
		} else if ((m = outputtomon(outs[i]))) {
			DBG("previously initialized monitor is now inactive: %s -- freeing", m->name)
			freemon(m);
			changed = 1;
		}
        free(o);
    }
	return changed;
}

int initrandr(void)
{
	int extbase;

	DBG("checking randr extension support")
	const xcb_query_extension_reply_t *ext = xcb_get_extension_data(con, &xcb_randr_id);

	if (!ext->present)
		return -1;
	DBG("randr extension is supported, initializing")
	updaterandr();
	extbase = ext->first_event;

	xcb_randr_select_input(con, root, XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE
			|XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE|XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE
			|XCB_RANDR_NOTIFY_MASK_OUTPUT_PROPERTY);

	DBG("RANDR extension active and monitor(s) initialized -- extension base: %d", extbase)
	return extbase;
}

int updaterandr(void)
{
	int len, changed;
    xcb_timestamp_t timestamp;
	xcb_randr_output_t *outputs;
    xcb_randr_get_screen_resources_current_reply_t *r;
    xcb_randr_get_screen_resources_current_cookie_t rc;

	DBG("querying current randr outputs")
	rc = xcb_randr_get_screen_resources_current(con, root);
	if (!(r = xcb_randr_get_screen_resources_current_reply(con, rc, NULL))) {
		warnx("unable to get screen resources");
		return -1;
	}

	timestamp = r->config_timestamp;
    len = xcb_randr_get_screen_resources_current_outputs_length(r);
    outputs = xcb_randr_get_screen_resources_current_outputs(r);
	changed = updateoutputs(outputs, len, timestamp);
	free(r);
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

Monitor *outputtomon(xcb_randr_output_t id)
{
	Monitor *m;

	FOR_EACH(m, monitors)
		if (m->id == id)
			return m;
	return m;
}

#endif
