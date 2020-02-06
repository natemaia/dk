/* debug printing functions
 * see license file for copyright and license details
 * vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
 */

#ifdef DEBUG

#include <xkbcommon/xkbcommon.h>

#undef DBGBIND
#define DBGBIND(event, mod, sym) printbind(event, mod, sym);

#undef DBG
#define DBG(fmt, ...) print("yaxwm:%s:%d - " fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__);

static char *masktomods(uint mask, char *out, int outsize);
static size_t strlcat(char *dst, const char *src, size_t size);
static void print(const char *fmt, ...);
static void printbind(xcb_generic_event_t *e, uint modmask, xcb_keysym_t keysym);

char *masktomods(uint mask, char *out, int outsize)
{ /* convert mask to modifier names in out, eg. "Shift, Mod4\0" */
	const char **mod, *mods[] = {
		"Shift", "Lock", "Ctrl", "Mod1", "Mod2", "Mod3", "Mod4",
		"Mod5", "Button1", "Button2", "Button3", "Button4", "Button5"
	};

	*out = '\0';
	for (mod = mods; mask; mask >>= 1, ++mod)
		if (mask & 1) {
			if (*out) {
				strlcat(out, ", ", outsize);
				strlcat(out, *mod, outsize);
			} else
				strlcpy(out, *mod, outsize);
		}
	return out;
}

void print(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
}

size_t strlcat(char *dst, const char *src, size_t size)
{
	size_t n = size, dlen;
	const char *odst = dst;
	const char *osrc = src;

	while (n-- != 0 && *dst != '\0')
		dst++;
	dlen = dst - odst;
	n = size - dlen;

	if (n-- == 0)
		return dlen + strlen(src);
	while (*src != '\0') {
		if (n != 0) {
			*dst++ = *src;
			n--;
		}
		src++;
	}
	*dst = '\0';

	return dlen + (src - osrc);
}

void printbind(xcb_generic_event_t *e, uint modmask, xcb_keysym_t keysym)
{
	char mod[64], key[64];

	masktomods(modmask, mod, sizeof(mod));
	xkb_keysym_get_name(keysym, key, sizeof(key));
	print("yaxwm:eventhandle: %s event - key: %s - mod: %s", xcb_event_get_label(e->response_type), key, mod);
}

#endif
