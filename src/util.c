#include "util.h"

void check(int i, char *msg)
{
	if (i < 0)
		err(1, "%s", msg);
}

void *ecalloc(size_t elems, size_t size)
{
	void *p;

	if (!(p = calloc(elems, size)))
		err(1, "unable to allocate space");
	return p;
}

char *itoa(int n, char *s)
{
	int j, i = 0, sign = n;

	if (sign < 0) n = -n;
	do { s[i++] = n % 10 + '0'; } while ((n /= 10) > 0);
	if (sign < 0) s[i++] = '-';
	s[i] = '\0';
	for (j = i - 1, i = 0; i < j; i++, j--) {
		char c = s[i];
		s[i] = s[j];
		s[j] = c;
	}
	return s;
}

void sighandle(int sig)
{
	switch (sig) {
	case SIGINT: /* FALLTHROUGH */
	case SIGTERM: /* FALLTHROUGH */
	case SIGHUP:
		exit(1);
		break;
	case SIGCHLD:
		signal(sig, sighandle);
		while (waitpid(-1, NULL, WNOHANG) > 0)
			;
		break;
	}
}

int usage(char *prog, char *ver, int e, char flag, char *flagstr)
{
	switch (flag) {
	case 'h': fprintf(stderr, "usage: %s %s\n", prog, flagstr); break;
	case 'v': fprintf(stderr, "%s %s\n", prog, ver); break;
	}
	return e;
}
