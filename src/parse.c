/* yet another X window manager
* see license file for copyright and license details
* vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

extern FILE *cmdresp;
extern const char *enoargs;

char **parsebool(char **argv, int *setting)
{
	int i;
	char *end;

	if (!argv || !*argv)
		return argv;
	if (((i = !strcmp("true", *argv)) || !strcmp("false", *argv))
			|| (((i = strtoul(*argv, &end, 0)) > 0 || !strcmp("0", *argv)) && *end == '\0'))
		*setting = i ? 1 : 0;
	else
		fprintf(cmdresp, "!invalid boolean argument: %s - expected true, false, 1, 0", *argv);
	return argv;
}

char **parsecolor(char **argv, unsigned int *setting)
{
	char *end;
	unsigned short a, r, g, b;
	unsigned int argb, len;

	if (!argv || !*argv)
		return argv;
	if ((len = strlen(*argv)) >= 6 && len <= 10) {
		if (**argv == '#' && len >= 7 && len <= 9)
			len--;
		else if (**argv == '0' && *(*argv + 1) == 'x')
			len -= 2;
		argb = strtoul(**argv == '#' ? *argv + 1 : *argv, &end, 16);
		if (argb <= 0xffffffff && *end == '\0') {
			if (len == 6)
				*setting = (argb | 0xff000000);
			else if ((a = ((argb & 0xff000000) >> 24)) && a != 0xff) {
				r = (((argb & 0xff0000) >> 16) * a) / 255;
				g = (((argb & 0xff00) >> 8) * a) / 255;
				b = (((argb & 0xff) >> 0) * a) / 255;
				*setting = (a << 24 | r << 16 | g << 8 | b << 0);
			} else
				*setting = argb;
			return argv;
		}
	}
	fprintf(cmdresp, "!invalid colour argument: %s - expected (#/0x)(AA)RRGGBB", *argv);
	return argv;
}

char **parsefloat(char **argv, float *setting, int *rel)
{
	float f;
	char *end;

	if (!argv || !*argv)
		return argv;
	if ((f = strtof(*argv, &end)) != 0.0 && *end == '\0') {
		if (f < -0.95 || f > 0.95)
			fprintf(cmdresp, "!float argument is out of range: %s - min: -0.95, max: 0.95", *argv);
		else {
			if (rel) /* check if it's a relative number (has a sign) */
				*rel = **argv == '-' || **argv == '+';
			*setting = f;
		}

	} else
		fprintf(cmdresp, "!invalid or incomplete float argument: %s - expected (-/+)0.N", *argv);
	return argv;
}

char **parseint(char **argv, int *setting, int *rel, int allowzero)
{
	int i;
	char *end;

	if (rel)
		*rel = 0;
	if (!argv || !*argv)
		return argv;
	if (((i = strtol(*argv, &end, 0)) || (allowzero && !strcmp("0", *argv))) && *end == '\0') {
		if (i && rel) /* check if it's a relative number (non-zero, has a sign) */
			*rel = **argv == '-' || **argv == '+';
		*setting = i;
	} else
		fprintf(cmdresp, "!invalid integer argument: %s - expected (-/+)N", *argv);
	return argv;
}

char **parseintclamp(char **argv, int *setting, int *rel, int min, int max)
{
	int i = min - 1;

	if (!argv || !*argv)
		return argv;
	parseint(argv, &i, rel, 1);
	if (i >= min && i <= max)
		*setting = i;
	else if (i != min - 1)
		fprintf(cmdresp, "!integer argument is out of range: %s - min: %d, max: %d",
				*argv, min, max);
	return argv;
}

char **parsegeom(char **argv, int *x, int *y, int *w, int *h,
		int *relx, int *rely, int *relw, int *relh)
{
	*relx = 0;
	*rely = 0;
	*relw = 0;
	*relh = 0;

	while (*argv) {
		if (!strcmp("x", *argv))
			argv = parseint(argv + 1, x, relx, 1);
		else if (!strcmp("y", *argv))
			argv = parseint(argv + 1, y, rely, 1);
		else if (!strcmp("w", *argv))
			argv = parseint(argv + 1, w, relw, 0);
		else if (!strcmp("h", *argv))
			argv = parseint(argv + 1, h, relh, 0);
		else {
			fprintf(cmdresp, "!invalid argument for window resize command: %s", *argv);
			break;
		}
		if (*argv)
			argv++;
	}
	return argv;
}

int parseopt(char **argv, char **opts, int *argi)
{
	char **s = opts;
	int i, ret = -1;

	if (!argv || !*argv)
		return ret;
	if (argi)
		*argi = INT_MAX;
	for (s = opts, i = 0; ret < 0 && s && *s; s++, i++)
		if (!strcmp(*s, *argv)) {
			ret = i;
			argv++;
			break;
		}
	if (argi && argv && *argv && ((i = strtol(*argv, NULL, 0)) || !strcmp(*argv, "0")))
		*argi = i;
	return ret;
}

int parsetoken(char **src, char *dst, size_t size)
{
	size_t n = 0;
    int q, sq = 0;
    char *s, *head, *tail;

    while (**src && (**src == ' ' || **src == '\t' || **src == '='))
		(*src)++;

    if ((q = **src == '"' || (sq = **src == '\''))) {
        head = *src + 1;
        if (!(tail = strchr(head, sq ? '\'' : '"')))
			return 0;
		if (!sq)
			while (*(tail - 1) == '\\')
				tail = strchr(tail + 1, '"');
    } else {
        head = *src;
        tail = strpbrk(*src, " =\t\n");
    }

    s = head;
    while (n + 1 < size && tail ? s < tail : *s)
        if (q && !sq && *s == '\\' && *(s + 1) == '"')
            s++;
        else {
            n++;
            *dst++ = *s++;
        }
    *dst = '\0';
	*src = tail ? ++tail : '\0';

    return n || q;
}
