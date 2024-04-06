/* dk window manager
 *
 * see license file for copyright and license details
 * vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
 */

#include <stdio.h>
#include <ctype.h>

#include "dk.h"
#include "status.h"

#define STOB(v, s) STATE(v, s) ? "true" : "false"

static void _client(Client *c, FILE *f);
static void _clients(FILE *f);
static void _desks(FILE *f);
static void _global(FILE *f);
static void _monitor(Monitor *m, FILE *f);
static void _monitors(FILE *f);
static void _panels(FILE *f);
static void _rules(FILE *f);
static char *_title(Client *c);
static void _workspaces(FILE *f);
static void _workspace_win(Workspace *ws, FILE *f);
static void _workspace(Workspace *ws, FILE *f);

static void _client(Client *c, FILE *f)
{
	fprintf(f, "\"id\":\"0x%08x\",", c->win);
	fprintf(f, "\"pid\":%d,", c->pid);
	fprintf(f, "\"title\":\"%s\",", _title(c));
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
	fprintf(f, "\"fixed\":%s,", STOB(c, FIXED));
	fprintf(f, "\"sticky\":%s,", STOB(c, STICKY));
	fprintf(f, "\"urgent\":%s,", STOB(c, URGENT));
	fprintf(f, "\"above\":%s,", STOB(c, ABOVE));
	fprintf(f, "\"hidden\":%s,", STOB(c, HIDDEN));
	fprintf(f, "\"scratch\":%s,", STOB(c, SCRATCH));
	fprintf(f, "\"no_absorb\":%s,", STOB(c, NOABSORB));
	fprintf(f, "\"callback\":\"%s\",", c->cb ? c->cb->name : "");
	fprintf(f, "\"transient\":{");
	if (c->trans) {
		_client(c->trans, f);
	}
	fprintf(f, "},");
	fprintf(f, "\"absorbed\":{");
	if (c->absorbed) {
		_client(c->absorbed, f);
	}
	fprintf(f, "}");
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
		fprintf(f, "\"workspace\":%d,", r->ws + !STATE(r, SCRATCH));
		fprintf(f, "\"monitor\":\"%s\",", r->mon ? r->mon : "");
		fprintf(f, "\"x\":%d,", r->x);
		fprintf(f, "\"y\":%d,", r->y);
		fprintf(f, "\"w\":%d,", r->w);
		fprintf(f, "\"h\":%d,", r->h);
		fprintf(f, "\"float\":%s,", STOB(r, FLOATING));
		fprintf(f, "\"full\":%s,", STOB(r, FULLSCREEN));
		fprintf(f, "\"fakefull\":%s,", STOB(r, FAKEFULL));
		fprintf(f, "\"sticky\":%s,", STOB(r, STICKY));
		fprintf(f, "\"scratch\":%s,", STOB(r, SCRATCH));
		fprintf(f, "\"focus\":%s,", r->focus ? "true" : "false");
		fprintf(f, "\"ignore_cfg\":%s,", STOB(r, IGNORECFG));
		fprintf(f, "\"ignore_msg\":%s,", STOB(r, IGNOREMSG));
		fprintf(f, "\"no_absorb\":%s,", STOB(r, NOABSORB));
		fprintf(f, "\"callback\":\"%s\",", r->cb ? r->cb->name : "");
		fprintf(f, "\"xgrav\":\"%s\",", r->xgrav != GRAV_NONE ? gravs[r->xgrav] : "");
		fprintf(f, "\"ygrav\":\"%s\"", r->ygrav != GRAV_NONE ? gravs[r->ygrav] : "");
		fprintf(f, "}%s", r->next ? "," : "");
	}
	fprintf(f, "]");
}

static char *_title(Client *c)
{
	int idx = 0;
	static char title[512];

	for (uint32_t i = 0; i < sizeof(title) && c->title[i]; i++) {
		if (!iscntrl(c->title[i])) {
			if (c->title[i] == '"')	{
				title[idx++] = '\\';
			}
			title[idx++] = c->title[i];
		}
	}
	title[idx] = '\0';

	return title;
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

static void _workspace_win(Workspace *ws, FILE *f)
{
	if (ws->sel && !STATE(ws->sel, HIDDEN)) {
		fprintf(f, "\"title\":\"%s\",", _title(ws->sel));
		fprintf(f, "\"id\":\"0x%08x\"", ws->sel->win);
	} else {
		fprintf(f, "\"title\":\"\",");
		fprintf(f, "\"id\":\"\"");
	}
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
					fprintf(s->file, "{\"focused\":\"%s\"}", selws->sel ? _title(selws->sel) : "");
				}
				break;
			case STAT_LYT:
				if (lytchange) {
					lytchange = 0;
					fprintf(s->file, "{\"layout\":\"%s\"}", selws->layout->name);
				}
				break;
			case STAT_WS:
				if (!wschange) {
					break;
				}
				wschange = 0;
				/* FALL THROUGH */
			case STAT_BAR:
				if (s->type == STAT_BAR) {
					winchange = lytchange = wschange = 0;
				}
				fprintf(s->file, "{\"workspaces\":[");
				for (ws = workspaces; ws; ws = ws->next) {
					fprintf(s->file, "{");
					fprintf(s->file, "\"name\":\"%s\",", ws->name);
					fprintf(s->file, "\"number\":%d,", ws->num + 1);
					fprintf(s->file, "\"focused\":%s,", ws == selws ? "true" : "false");
					fprintf(s->file, "\"active\":%s,", ws->clients ? "true" : "false");
					fprintf(s->file, "\"monitor\":\"%s\",", ws->mon->name);
					fprintf(s->file, "\"layout\":\"%s\",", ws->layout->name);
					_workspace_win(ws, s->file);
					fprintf(s->file, "}%s", ws->next ? "," : "");
				}
				fprintf(s->file, "]}");
				break;
			case STAT_FULL:
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
				fprintf(s->file, "}");
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
