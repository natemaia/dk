/* dk window manager
 *
 * see license file for copyright and license details
 * vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
 */

#pragma once

#define UNLIKELY(x) __builtin_expect(!!(x), 0)

void check(int i, char *msg);
void *ecalloc(size_t elems, size_t elemsize);
void *erealloc(void *p, size_t size);
char *itoa(int n, char *s);
void respond(FILE *f, const char *fmt, ...);
void sighandle(int sig);
int usage(char *prog, char *ver, int e, char flag, char *flagstr);
