/*
 * (C)opyright MMIV-MMVI Anselm R. Garbe <garbeam at gmail dot com>
 * See LICENSE file for license details.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "wm.h"

static Bool
is_empty(View *v)
{
	Area *a;
	for(a=v->area; a; a=a->next)
		if(a->frame)
			return False;
	return True;
}

Frame *
clientframe_of_view(View *v, Client *c)
{              
	Frame *f;
	for(f=c->frame; f; f=f->cnext)
		if(f->area->view == v)
			break;
	return f;
} 

static void
assign_sel_view(View *v)
{
	if(screen->sel != v) {
		if(screen->sel)
			write_event("UnfocusTag %s\n",screen->sel->name);
		screen->sel = v;
		write_event("FocusTag %s\n", screen->sel->name);
	}
}

Client *
sel_client_of_view(View *v) {
	return v->sel && v->sel->sel ? v->sel->sel->client : nil;
}

View *
get_view(const char *name)
{
	View *v;
	int cmp;
	for(v = view; v; v=v->next)
		if((cmp=strcmp(name, v->name)) >= 0)
			break;
	if(!v || cmp != 0)
		v = create_view(name);
	return v;
}

View *
create_view(const char *name)
{
	static unsigned short id = 1;
	View **i, *v = cext_emallocz(sizeof(View));

	v->id = id++;
	cext_strlcpy(v->name, name, sizeof(v->name));
	create_area(v, nil, 0);
	create_area(v, v->area, 0);
	v->area->floating = True;

	for(i=&view; *i; i=&(*i)->next)
		if(strcmp((*i)->name, name) < 0) break;
	v->next = *i;
	*i = v;

	write_event("CreateTag %s\n", v->name);
	if(!screen->sel)
		assign_sel_view(v);

	return v;
}

void
destroy_view(View *v)
{
	Area *a;
	View **i;

	while((a = v->area)) {
		v->area = a->next;
		destroy_area(a);
	};

	for(i=&view; *i; i=&(*i)->next)
		if(*i == v) break;
	*i = v->next;

	if(screen->sel == v)
		for(screen->sel=view; screen->sel && screen->sel->next; screen->sel=screen->sel->next)
			if(screen->sel->next == *i) break;

	write_event("DestroyTag %s\n", v->name);
	free(v);
}

static void
update_frame_selectors(View *v)
{
	Area *a;
	Frame *f;
	for(a=v->area; a; a=a->next)
		for(f=a->frame; f; f=f->anext)
			f->client->sel = f;
}

void
focus_view(WMScreen *s, View *v)
{
	Frame *f;
	Client *c;

	XGrabServer(blz.dpy);
	assign_sel_view(v);

	update_frame_selectors(v);

	/* gives all(!) clients proper geometry (for use of different tags) */
	for(c=client; c; c=c->next)
		if((f = c->sel)) {
			if(f->view == v) {
				resize_client(c, &f->rect, False);
				//XMoveWindow(blz.dpy, c->framewin, f->rect.x, f->rect.y);
			}else
				XMoveWindow(blz.dpy, c->framewin, 2 * s->rect.width + f->rect.x,
						f->rect.y);
		}

	if((c = sel_client()))
		focus_client(c, True);

	draw_frames();
	XSync(blz.dpy, False);
	XUngrabServer(blz.dpy);
	flush_masked_events(EnterWindowMask);
}

void
select_view(const char *arg)
{
	char buf[256];
	cext_strlcpy(buf, arg, sizeof(buf));
	cext_trim(buf, " \t+");
	if(!strlen(buf))
		return;
	assign_sel_view(get_view(arg));
	update_views(); /* performs focus_view */
}

void
attach_to_view(View *v, Frame *f)
{
	Area *a;
	Client *c = f->client;

	c->revert = nil;

	if(c->trans || c->floating || c->fixedsize
		|| (c->rect.width == screen->rect.width && c->rect.height == screen->rect.height))
		a = v->area;
	else if(starting && v->sel->floating)
		a = v->area->next;
	else
		a = v->sel;
	attach_to_area(a, f, False);
	v->sel = a;
}

void
restack_view(View *v)
{
	Area *a;
	Frame *f;
	Client *c;
	unsigned int n=0, i=0;
	static Window *wins = nil;
	static unsigned int winssz = 0;

	for(c=client; c; c=c->next, i++);
	if(i > winssz) {
		winssz = 2 * i;
		wins = cext_erealloc(wins, sizeof(Window) * winssz);
	}

	for(a=v->area; a; a=a->next) {
		if(a->frame) {
			wins[n++] = a->sel->client->framewin;
			for(f=a->frame; f; f=f->anext)
				if(f != a->sel) n++;
			i=n;
			for(f=a->frame; f; f=f->anext) {
				Client *c = f->client;
				update_client_grab(c, (v->sel == a) && (a->sel == f));
				if(f != a->sel)
					wins[--i] = c->framewin;
			}
		}
	}

	if(n)
		XRestackWindows(blz.dpy, wins, n);
}

void
scale_view(View *v, float w)
{
	unsigned int xoff, i=0;
	Area *a;
	float scale, dx = 0;
	int wdiff = 0;

	if(!v->area->next)
		return;

	for(a=v->area->next; a; a=a->next, i++)
		dx += a->rect.width;
	scale = w / dx;
	xoff = 0;
	for(a=v->area->next; a; a=a->next) {
		a->rect.width *= scale;
		if(!a->next)
			a->rect.width = w - xoff;
		xoff += a->rect.width;
	}

	/* MIN_COLWIDTH can only be respected when there is enough space; the caller should guarantee this */
	if(i * MIN_COLWIDTH > w)
		return;
	xoff = 0;
	for(a=v->area->next; a; a=a->next, i--) {
		if(a->rect.width < MIN_COLWIDTH)
			a->rect.width = MIN_COLWIDTH;
		else if((wdiff = xoff + a->rect.width - w + i * MIN_COLWIDTH) > 0)
			a->rect.width -= wdiff;
		if(!a->next)
			a->rect.width = w - xoff;
		xoff += a->rect.width;
	}
}

void
arrange_view(View *v)
{
	unsigned int xoff = 0;
	Area *a;

	if(!v->area->next)
		return;

	scale_view(v, screen->rect.width);
	for(a=v->area->next; a; a=a->next) {
		a->rect.x = xoff;
		a->rect.y = 0;
		a->rect.height = screen->rect.height - screen->brect.height;
		xoff += a->rect.width;
		arrange_column(a, False);
	}
}

XRectangle *
rects_of_view(View *v, unsigned int *num)
{
	XRectangle *result;
	Frame *f;

	*num = 2;
	for(f=v->area->frame; f; f=f->anext, (*num)++);

	result = cext_emallocz(*num * sizeof(XRectangle));
	for(f=v->area->frame; f; f=f->anext)
		*result++ = f->rect;
	*result++ = screen->rect;
	*result++ = screen->brect;

	return result - *num;
}

/* XXX: This will need cleanup */
unsigned char *
view_index(View *v) {
	unsigned int a_i, buf_i, n;
	int len;
	Frame *f;
	Area *a;

	len = BUFFER_SIZE;
	buf_i = 0;
	for((a = v->area), (a_i = 0); a && len > 0; (a=a->next), (a_i++)) {
		if(a->floating)
			n = snprintf(&buffer[buf_i], len, "# ~ %d %d\n",
					a->rect.width, a->rect.height);
		else
			n = snprintf(&buffer[buf_i], len, "# %d %d %d\n",
					a_i, a->rect.x, a->rect.width);
		buf_i += n;
		len -= n;
		for(f=a->frame; f && len > 0; f=f->anext) {
			XRectangle *r = &f->rect;
			if(a->floating)
				n = snprintf(&buffer[buf_i], len, "~ %d %d %d %d %d %s\n",
						idx_of_client(f->client),
						r->x, r->y, r->width, r->height,
						f->client->props);
			else
				n = snprintf(&buffer[buf_i], len, "%d %d %d %d %s\n",
						a_i, idx_of_client(f->client), r->y,
						r->height, f->client->props);
			if(len - n < 0)
				return (unsigned char *)buffer;
			buf_i += n;
			len -= n;
		}
	}
	return (unsigned char *)buffer;
}

Client *
client_of_message(View *v, char *message, unsigned int *next)
{              
	unsigned int i;
	Client *c;

	if(!strncmp(message, "sel ", 4)) {
		*next = 4;
		return sel_client_of_view(v);
	}
	if((1 != sscanf(message, "%d %n", &i, next)))
		return nil;
	for(c=client; i && c; c=c->next, i--);
	return c;
}

Area *
area_of_message(View *v, char *message, unsigned int *next) {
	unsigned int i;
	Area *a;

	if(!strncmp(message, "sel ", 4)) {
		*next = 4;
		return v->sel;
	}
	if(!strncmp(message, "~ ", 2))
		return v->area;
	if(1 != sscanf(message, "%d %n", &i, next) || i == 0)
		return nil;
	for(a=v->area; i && a; a=a->next, i--);
	return a;
}

char *
message_view(View *v, char *message) {
	unsigned int n, i;
	Client *c;
	Frame *f;
	Area *a;
	static char Ebadvalue[] = "bad value";

	if(!strncmp(message, "send ", 5)) {
		message += 5;
		if(!(c = client_of_message(v, message, &n)))
			return Ebadvalue;
		if(!(f = clientframe_of_view(v, c)))
			return Ebadvalue;
		return send_client(f, &message[n]);
	}
	if(!strncmp(message, "select ", 7)) {
		message += 7;
		return select_area(v->sel, message);
	}
	if(!strncmp(message, "colmode ", 8)) {
		message += 8;
		if(!(a = area_of_message(v, message, &n)) || a == v->area)
			return Ebadvalue;
		if((i = column_mode_of_str(&message[n])) == -1)
			return Ebadvalue;
		a->mode = i;
		arrange_column(a, True);
		restack_view(v);
		if(v == screen->sel)
			focus_view(screen, v);
		draw_frames();
		return nil;
	}
	return Ebadvalue;
}

void
update_views()
{
	View *n, *v;
	View *old = screen->sel;

	for(v=view; v; v=v->next)
		update_frame_selectors(v);

	if(old && !strncmp(old->name, "nil", 4))
		old = nil;

	for((v=view) && (n=v->next); v; (v=n) && (n=v->next))
		if((v != old) && is_empty(v))
			destroy_view(v);

	if(old)
		focus_view(screen, old);
	else if(screen->sel)
		focus_view(screen, screen->sel);
}

unsigned int
newcolw_of_view(View *v)
{
	Rule *r;
	Area *a;
	unsigned int i, n;
	regmatch_t tmpregm;

	for(r=def.colrules.rule; r; r=r->next) {
		if(!regexec(&r->regex, v->name, 1, &tmpregm, 0)) {
			char buf[256];
			char *toks[16];
			cext_strlcpy(buf, r->value, sizeof(buf));
			n = cext_tokenize(toks, 16, buf, '+');
			for(a=v->area, i=0; a; a=a->next, i++);
			if(n && n >= i) {
				if(sscanf(toks[i - 1], "%u", &n) == 1)
					return (screen->rect.width * n) / 100;
			}
			break;
		}
	}
	return 0;
}
