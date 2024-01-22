/* dk window manager
 *
 * see license file for copyright and license details
 * vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
 */

#include <sys/wait.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>
#include <err.h>

#include "util.h"

void check(int i, char *msg)
{
	if (i < 0) {
		err(1, "%s", msg);
	}
}

void *ecalloc(size_t elems, size_t elemsize)
{
	void *p;

	if (UNLIKELY((p = calloc(elems, elemsize)) == NULL)) {
		err(1, "unable to allocate space");
	}
	return p;
}

void *erealloc(void *p, size_t size)
{
	void *np;

	if (UNLIKELY((np = realloc(p, size)) == NULL)) {
		err(1, "unable to reallocate space");
	}
	return np;
}

char *itoa(int n, char *s)
{
	int j, i = 0, sign = n;

	if (sign < 0) {
		n = -n;
	}
	do {
		s[i++] = n % 10 + '0';
	} while ((n /= 10) > 0);
	if (sign < 0) {
		s[i++] = '-';
	}
	s[i] = '\0';
	for (j = i - 1, i = 0; i < j; i++, j--) {
		char c = s[i];
		s[i] = s[j];
		s[j] = c;
	}
	return s;
}

void respond(FILE *f, const char *fmt, ...)
{
	va_list ap;

	if (!f) {
		return;
	}
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
}

int usage(char *prog, char *ver, int e, char flag, char *flagstr)
{
	switch (flag) {
		case 'h': fprintf(stderr, "usage: %s %s\n", prog, flagstr); break;
		case 'v': fprintf(stderr, "%s %s\n", prog, ver); break;
	}
	return e;
}
