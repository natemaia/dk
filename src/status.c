/* dk window manager
 *
 * see license file for copyright and license details
 * vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
 */

#include <stdio.h>

#include "dk.h"
#include "status.h"

#define STOB(c, state) STATE(c, state) ? "true" : "false"

static void _client(Client *c, FILE *f);
static void _clients(FILE *f);
static void _desks(FILE *f);
static void _global(FILE *f);
static void _monitor(Monitor *m, FILE *f);
static void _monitors(FILE *f);
static void _panels(FILE *f);
static void _rules(FILE *f);
static void _workspace(Workspace *ws, FILE *f);
static void _workspaces(FILE *f);

static void _client(Client *c, FILE *f)
{
	fprintf(f, "\"id\":\"0x%08x\",", c->win);
	fprintf(f, "\"title\":\"%s\",", c->title);
	fprintf(f, "\"class\":\"%s\",", c->clss);
	fprintf(f, "\"instance\":\"%s\",", c->inst);
	fprintf(f, "\"workspace\":%d,", c->ws->num + !STATE(c, SCRATCH));
	fprintf(f, "\"focused\":%s,", c == selws->sel ? "true" : "false");
	fprintf(f, "\"x\":%d,", c->x);
	fprintf(f, "\"y\":%d,", c->y);
	fprintf(f, "\"w\":%d,", c->w);
	fprintf(f, "\"h\":%d,", c->h);
	fprintf(f, "\"bw\":%d,", c->bw);
	fprintf(f, "\"hoff\":%d,", c->hoff);
	fprintf(f, "\"float\":%s,", STOB(c, FLOATING));
	fprintf(f, "\"full\":%s,", STOB(c, FULLSCREEN));
	fprintf(f, "\"fakefull\":%s,", STOB(c, FAKEFULL));
	fprintf(f, "\"sticky\":%s,", STOB(c, STICKY));
	fprintf(f, "\"urgent\":%s,", STOB(c, URGENT));
	fprintf(f, "\"above\":%s,", STOB(c, ABOVE));
	fprintf(f, "\"hidden\":%s,", STOB(c, HIDDEN));
	fprintf(f, "\"scratch\":%s,", STOB(c, SCRATCH));
	fprintf(f, "\"callback\":\"%s\",", c->cb ? c->cb->name : "");
	fprintf(f, "\"trans_id\":\"0x%08x\"", c->trans ? c->trans->win : 0);
	fflush(f);
}

static void _clients(FILE *f)
{
	Client *c;
	Workspace *ws;

	fprintf(f, "\"clients\":[");
	for (ws = workspaces; ws; ws = ws->next) {
		for (c = ws->clients; c; c = c->next) {
			fprintf(f, "{");
			_client(c, f);
			fprintf(f, "}%s", c->next ? "," : "");
		}
		if (ws->next && ws->next->clients) {
			fprintf(f, ",");
		}
	}
	if (scratch.clients) {
		fprintf(f, ",");
		for (c = scratch.clients; c; c = c->next) {
			fprintf(f, "{");
			_client(c, f);
			fprintf(f, "}%s", c->next ? "," : "");
		}
	}
	fprintf(f, "]");
}

static void _desks(FILE *f)
{
	Desk *d;

	fprintf(f, "\"desks\":[");
	for (d = desks; d; d = d->next) {
		fprintf(f, "{");
		fprintf(f, "\"id\":\"0x%08x\",", d->win);
		fprintf(f, "\"class\":\"%s\",", d->clss);
		fprintf(f, "\"instance\":\"%s\",", d->inst);
		fprintf(f, "\"monitor\":\"%s\"", d->mon->name);
		fprintf(f, "}%s", d->next ? "," : "");
	}
	fprintf(f, "]");
}

static void _global(FILE *f)
{
	fprintf(f, "\"global\":{");
	for (uint32_t i = 0; i < LEN(globalcfg); i++) {
		if (globalcfg[i].type == TYPE_BOOL) {
			fprintf(f, "\"%s\":%s,", globalcfg[i].str, globalcfg[i].val ? "true" : "false");
		} else {
			fprintf(f, "\"%s\":%d,", globalcfg[i].str, globalcfg[i].val);
		}
	}
	fprintf(f, "\"layouts\":[");
	for (Layout *l = layouts; l && l->name; ) {
		fprintf(f, "\"%s\"", l->name);
		l++;
		if (l && l->name) {
			fprintf(f, ",");
		}
	}
	fprintf(f, "],");
	fprintf(f, "\"callbacks\":[");
	for (Callback *cb = callbacks; cb && cb->name; ) {
		fprintf(f, "\"%s\"", cb->name);
		cb++;
		if (cb && cb->name) {
			fprintf(f, ",");
		}
	}
	fprintf(f, "],");
	fprintf(f, "\"border\":{");
	fprintf(f, "\"width\":%u,", border[BORD_WIDTH]);
	fprintf(f, "\"outer_width\":%u,", border[BORD_O_WIDTH]);
	fprintf(f, "\"focus\":\"0x%08x\",", border[BORD_FOCUS]);
	fprintf(f, "\"urgent\":\"0x%08x\",", border[BORD_URGENT]);
	fprintf(f, "\"unfocus\":\"0x%08x\",", border[BORD_UNFOCUS]);
	fprintf(f, "\"outer_focus\":\"0x%08x\",", border[BORD_O_FOCUS]);
	fprintf(f, "\"outer_urgent\":\"0x%08x\",", border[BORD_O_URGENT]);
	fprintf(f, "\"outer_unfocus\":\"0x%08x\"", border[BORD_O_UNFOCUS]);
	fprintf(f, "},");
	fprintf(f, "\"focused\":{");
	_monitor(selmon, f);
	fprintf(f, "}}");
}

static void _monitor(Monitor *m, FILE *f)
{
	fprintf(f, "\"name\":\"%s\",", m->name);
	fprintf(f, "\"number\":%d,", m->num + 1);
	fprintf(f, "\"focused\":%s,", m->ws == selws ? "true" : "false");
	fprintf(f, "\"x\":%d,", m->x);
	fprintf(f, "\"y\":%d,", m->y);
	fprintf(f, "\"w\":%d,", m->w);
	fprintf(f, "\"h\":%d,", m->h);
	fprintf(f, "\"wx\":%d,", m->wx);
	fprintf(f, "\"wy\":%d,", m->wy);
	fprintf(f, "\"ww\":%d,", m->ww);
	fprintf(f, "\"wh\":%d,", m->wh);
	fprintf(f, "\"workspace\":{");
	_workspace(m->ws, f);
	fprintf(f, "}");
}

static void _monitors(FILE *f)
{
	Monitor *m;

	fprintf(f, "\"monitors\":[");
	for (m = monitors; m; m = m->next) {
		if (m->connected) {
			fprintf(f, "{");
			_monitor(m, f);
			fprintf(f, "}%s", m->next ? "," : "");
		}
	}
	fprintf(f, "]");
}

static void _panels(FILE *f)
{
	Panel *p;

	fprintf(f, "\"panels\":[");
	for (p = panels; p; p = p->next) {
		fprintf(f, "{");
		fprintf(f, "\"id\":\"0x%08x\",", p->win);
		fprintf(f, "\"class\":\"%s\",", p->clss);
		fprintf(f, "\"instance\":\"%s\",", p->inst);
		fprintf(f, "\"x\":%d,", p->x);
		fprintf(f, "\"y\":%d,", p->y);
		fprintf(f, "\"w\":%d,", p->w);
		fprintf(f, "\"h\":%d,", p->h);
		fprintf(f, "\"l\":%d,", p->l);
		fprintf(f, "\"r\":%d,", p->r);
		fprintf(f, "\"t\":%d,", p->t);
		fprintf(f, "\"b\":%d,", p->b);
		fprintf(f, "\"monitor\":{");
		_monitor(p->mon, f);
		fprintf(f, "}}%s", p->next ? "," : "");
	}
	fprintf(f, "]");
}

static void _rules(FILE *f)
{
	Rule *r;

	fprintf(f, "\"rules\":[");
	for (r = rules; r; r = r->next) {
		fprintf(f, "{");
		fprintf(f, "\"title\":\"%s\",", r->title ? r->title : "");
		fprintf(f, "\"class\":\"%s\",", r->clss ? r->clss : "");
		fprintf(f, "\"instance\":\"%s\",", r->inst ? r->inst : "");
		fprintf(f, "\"workspace\":%d,", r->ws);
		fprintf(f, "\"monitor\":\"%s\",", r->mon ? r->mon : "");
		fprintf(f, "\"x\":%d,", r->x);
		fprintf(f, "\"y\":%d,", r->y);
		fprintf(f, "\"w\":%d,", r->w);
		fprintf(f, "\"h\":%d,", r->h);
		fprintf(f, "\"float\":%s,", STOB(r, FLOATING));
		fprintf(f, "\"full\":%s,", STOB(r, FULLSCREEN));
		fprintf(f, "\"fakefull\":%s,", STOB(r, FAKEFULL));
		fprintf(f, "\"sticky\":%s,", STOB(r, STICKY));
		fprintf(f, "\"focus\":%s,", STOB(r, HIDDEN));
		fprintf(f, "\"ignore_cfg\":%s,", STOB(r, URGENT));
		fprintf(f, "\"ignore_msg\":%s,", STOB(r, ABOVE));
		fprintf(f, "\"callback\":\"%s\",", r->cb ? r->cb->name : "");
		fprintf(f, "\"xgrav\":\"%s\",", r->xgrav != GRAV_NONE ? gravs[r->xgrav] : "");
		fprintf(f, "\"ygrav\":\"%s\"", r->ygrav != GRAV_NONE ? gravs[r->ygrav] : "");
		fprintf(f, "}%s", r->next ? "," : "");
	}
	fprintf(f, "]");
}

static void _workspace(Workspace *ws, FILE *f)
{
	Client *c;

	fprintf(f, "\"name\":\"%s\",", ws->name);
	fprintf(f, "\"number\":%d,", ws->num + 1);
	fprintf(f, "\"focused\":%s,", ws == selws ? "true" : "false");
	fprintf(f, "\"monitor\":\"%s\",", ws->mon->name);
	fprintf(f, "\"layout\":\"%s\",", ws->layout->name);
	fprintf(f, "\"master\":%d,", ws->nmaster);
	fprintf(f, "\"stack\":%d,", ws->nstack);
	fprintf(f, "\"msplit\":%0.2f,", ws->msplit);
	fprintf(f, "\"ssplit\":%0.2f,", ws->ssplit);
	fprintf(f, "\"gap\":%d,", ws->gappx);
	fprintf(f, "\"smart_gap\":%s,", (ws->smartgap && tilecount(ws) == 1) ? "true" : "false");
	fprintf(f, "\"pad_l\":%d,", ws->padl);
	fprintf(f, "\"pad_r\":%d,", ws->padr);
	fprintf(f, "\"pad_t\":%d,", ws->padt);
	fprintf(f, "\"pad_b\":%d,", ws->padb);
	fprintf(f, "\"clients\":[");
	for (c = ws->clients; c; c = c->next) {
		fprintf(f, "{");
		_client(c, f);
		fprintf(f, "}%s", c->next ? "," : "");
	}
	fprintf(f, "],");
	fprintf(f, "\"focus_stack\":[");
	for (c = ws->stack; c; c = c->snext) {
		fprintf(f, "{");
		_client(c, f);
		fprintf(f, "}%s", c->snext ? "," : "");
	}
	fprintf(f, "]");
}

static void _workspaces(FILE *f)
{
	Workspace *ws;

	fprintf(f, "\"workspaces\":[");
	for (ws = workspaces; ws; ws = ws->next) {
		fprintf(f, "{");
		_workspace(ws, f);
		fprintf(f, "}%s", ws->next ? "," : "");
	}
	fprintf(f, "]");
}

void printstatus(Status *s, int freeable)
{
	Status *next;
	Workspace *ws;
	int single = 1;

	if (!s) {
		s = stats;
		single = 0;
	}
	while (s) {
		next = s->next;
		switch (s->type) {
			case STAT_WIN:
				if (winchange) {
					winchange = 0;
					fprintf(s->file, "%s", selws->sel ? selws->sel->title : "");
				}
				break;
			case STAT_LYT:
				if (lytchange) {
					lytchange = 0;
					fprintf(s->file, "%s", selws->layout->name);
				}
				break;
			case STAT_WS:
				if (wschange) {
					wschange = 0;
					for (ws = workspaces; ws; ws = ws->next) {
						char fmt[5] = "i%s:";
						fmt[0] = (ws == selws) ? (ws->clients ? 'A' : 'I') : (ws->clients ? 'a' : 'i');
						if (!ws->next) {
							fmt[3] = '\0';
						}
						fprintf(s->file, fmt, ws->name);
					}
				}
				break;
			case STAT_BAR:
				fprintf(s->file, "W");
				for (ws = workspaces; ws; ws = ws->next) {
					char fmt[5] = "i%s:";
					fmt[0] = (ws == selws) ? (ws->clients ? 'A' : 'I') : (ws->clients ? 'a' : 'i');
					if (!ws->next) {
						fmt[3] = '\0';
					}
					fprintf(s->file, fmt, ws->name);
				}
				fprintf(s->file, "\nL%s\nA%s", selws->layout->name,
					selws->sel && !STATE(selws->sel, HIDDEN) ? selws->sel->title : "");
				winchange = lytchange = wschange = 0;
				break;
			case STAT_JSON:
				fprintf(s->file, "{");
				_global(s->file);
				fprintf(s->file, ",");
				fflush(s->file);
				_workspaces(s->file);
				fprintf(s->file, ",");
				fflush(s->file);
				_monitors(s->file);
				fprintf(s->file, ",");
				fflush(s->file);
				_clients(s->file);
				fprintf(s->file, ",");
				fflush(s->file);
				_rules(s->file);
				fprintf(s->file, ",");
				fflush(s->file);
				_panels(s->file);
				fprintf(s->file, ",");
				fflush(s->file);
				_desks(s->file);
				fprintf(s->file, "}\n");
				winchange = lytchange = wschange = 0;
				break;
			case STAT_FULL:
				fprintf(s->file, "# global - key: value ...\n");
				for (uint32_t i = 0; i < LEN(globalcfg); i++) {
					fprintf(s->file, "%s: %d\n", globalcfg[i].str, globalcfg[i].val);
				}
				fprintf(s->file, "window: 0x%08x\n", selws->sel ? selws->sel->win : 0);
				fprintf(s->file, "layouts:");
				for (Layout *l = layouts; l && l->name; l++) {
					fprintf(s->file, " %s", l->name);
				}
				fprintf(s->file, "\ncallbacks:");
				for (Callback *cb = callbacks; cb && cb->name; cb++) {
					fprintf(s->file, " %s", cb->name);
				}
				fprintf(s->file,
					"\n\n# width outer_width focus urgent unfocus outer_focus "
					"outer_urgent outer_unfocus\n"
					"border: %u %u 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x",
					border[BORD_WIDTH], border[BORD_O_WIDTH], border[BORD_FOCUS], border[BORD_URGENT],
					border[BORD_UNFOCUS], border[BORD_O_FOCUS], border[BORD_O_URGENT],
					border[BORD_O_UNFOCUS]);
				Workspace *ws;
				fprintf(s->file, "\n\n# number:name:layout ...\nworkspaces:");
				for (ws = workspaces; ws; ws = ws->next) {
					fprintf(s->file, " %s%d:%s:%s", ws == selws ? "*" : "", ws->num + 1, ws->name,
						ws->layout->name);
				}
				fprintf(s->file, "\n\t# number:name window master stack "
					"msplit ssplit gappx smart_gap padl padr padt padb");
				for (ws = workspaces; ws; ws = ws->next) {
					fprintf(s->file, "\n\t%d:%s 0x%08x %d %d %0.2f %0.2f %d %d %d %d %d %d", ws->num + 1,
						ws->name, ws->sel ? ws->sel->win : 0, ws->nmaster, ws->nstack, ws->msplit,
						ws->ssplit, ws->gappx, ws->smartgap && tilecount(ws) == 1, ws->padl, ws->padr,
						ws->padt, ws->padb);
				}
				Monitor *m;
				fprintf(s->file, "\n\n# number:name:workspace ...\nmonitors:");
				for (m = monitors; m; m = m->next) {
					if (m->connected) {
						fprintf(s->file, " %s%d:%s:%d", m->ws == selws ? "*" : "", m->num + 1, m->name,
							m->ws->num + 1);
					}
				}
				fprintf(s->file, "\n\t# number:name window x y width height wx wy wwidth wheight");
				for (m = monitors; m; m = m->next) {
					if (m->connected) {
						fprintf(s->file, "\n\t%d:%s 0x%08x %d %d %d %d %d %d %d %d", m->num + 1, m->name,
							m->ws->sel ? m->ws->sel->win : 0, m->x, m->y, m->w, m->h, m->wx, m->wy, m->ww, m->wh);
					}
				}
				Client *c;
				fprintf(s->file, "\n\n# id:workspace ...\nwindows:");
				for (ws = workspaces; ws; ws = ws->next) {
					for (c = ws->clients; c; c = c->next) {
						fprintf(s->file, " %s0x%08x:%d", c == selws->sel ? "*" : "", c->win, c->ws->num + 1);
					}
				}
				for (c = scratch.clients; c; c = c->next) {
					fprintf(s->file, " 0x%08x:%d", c->win, c->ws->num);
				}
				fprintf(s->file, "\n\t# id title class instance ws x y width height bw hoff "
					"float full fakefull fixed stick urgent above hidden scratch callback trans_id");
				for (ws = workspaces; ws; ws = ws->next) {
					for (c = ws->clients; c; c = c->next) {
						DBG("printstatus: client 0x%08x %s", c->win, c->title)
						fprintf(s->file,
							"\n\t0x%08x \"%s\" \"%s\" \"%s\" %d %d %d %d %d %d %d "
							"%d %d %d %d %d %d %d %d %d %s 0x%08x",
							c->win, c->title, c->clss, c->inst, c->ws->num + 1, c->x, c->y, c->w, c->h, c->bw, c->hoff,
							STATE(c, FLOATING) != 0, STATE(c, FULLSCREEN) != 0, STATE(c, FAKEFULL) != 0,
							STATE(c, FIXED) != 0, STATE(c, STICKY) != 0, STATE(c, URGENT) != 0, STATE(c, ABOVE) != 0,
							STATE(c, HIDDEN) != 0, STATE(c, SCRATCH) != 0, c->cb ? c->cb->name : "none",
							c->trans ? c->trans->win : 0);
					}
				}
				for (c = scratch.clients; c; c = c->next) {
					DBG("printstatus: client 0x%08x %s", c->win, c->title)
					fprintf(s->file,
						"\n\t0x%08x \"%s\" \"%s\" \"%s\" %d %d %d %d %d %d %d "
						"%d %d %d %d %d %d %d %d %d %s 0x%08x",
						c->win, c->title, c->clss, c->inst, c->ws->num, c->x, c->y, c->w, c->h, c->bw, c->hoff,
						STATE(c, FLOATING) != 0, STATE(c, FULLSCREEN) != 0, STATE(c, FAKEFULL) != 0,
						STATE(c, FIXED) != 0, STATE(c, STICKY) != 0, STATE(c, URGENT) != 0, STATE(c, ABOVE) != 0,
						STATE(c, HIDDEN) != 0, STATE(c, SCRATCH) != 0, c->cb ? c->cb->name : "none",
						c->trans ? c->trans->win : 0);
				}
				if (rules) {
					Rule *r;
					fprintf(s->file, "\n\n# title class instance workspace monitor "
									 "float full fakefull stick ignore_cfg ignore_msg "
									 "focus callback x y width height xgrav ygrav");
					for (r = rules; r; r = r->next) {
						fprintf(s->file,
							"\nrule: \"%s\" \"%s\" \"%s\" %d %s %d %d %d %d %d %d "
							"%d %s %d %d %d %d %s %s",
							r->title, r->clss, r->inst, r->ws, r->mon, STATE(r, FLOATING) != 0,
							STATE(r, FULLSCREEN) != 0, STATE(r, FAKEFULL) != 0, STATE(r, STICKY) != 0,
							STATE(r, IGNORECFG) != 0, STATE(r, IGNOREMSG) != 0, r->focus,
							r->cb ? r->cb->name : "", r->x, r->y, r->w, r->h, gravs[r->xgrav],
							gravs[r->ygrav]);
					}
				}
				if (panels) {
					Panel *p;
					fprintf(s->file, "\n\n# id:monitor ...\npanels:");
					for (p = panels; p; p = p->next) {
						fprintf(s->file, " 0x%08x:%s", p->win, p->mon->name);
					}
					fprintf(s->file, "\n\t# id class instance monitor x y width height left right top bottom");
					for (p = panels; p; p = p->next) {
						fprintf(s->file, "\n\t0x%08x \"%s\" \"%s\" %s %d %d %d %d %d %d %d %d", p->win,
							p->clss, p->inst, p->mon->name, p->x, p->y, p->w, p->h, p->l, p->r, p->t, p->b);
					}
				}
				if (desks) {
					Desk *d;
					fprintf(s->file, "\n\n# id:monitor ...\ndesks:");
					for (d = desks; d; d = d->next) {
						fprintf(s->file, " 0x%08x:%s", d->win, d->mon->name);
					}
					fprintf(s->file, "\n\t# id class instance monitor");
					for (d = desks; d; d = d->next) {
						fprintf(s->file, "\n\t0x%08x \"%s\" \"%s\" %s", d->win, d->clss, d->inst, d->mon->name);
					}
				}
				winchange = lytchange = wschange = 0;
				break;
		}
		fflush(s->file);
		/* one-shot status prints have no allocations so aren't free-able */
		if (freeable && !(s->num -= s->num > 0 ? 1 : 0)) {
			freestatus(s);
		}
		if (single) {
			break;
		}
		s = next;
	}
}
