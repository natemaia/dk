/* yet another X window manager
* see license file for copyright and license details
* vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>

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

char **parsecolor(char **argv, uint32_t *setting)
{
	char *end;
	uint32_t i, len;

	if (!argv || !*argv)
		return argv;
	len = strlen(*argv);
	if (**argv == '#' && len >= 7 && len <= 9)
		i = strtoul(*argv + 1, &end, 16);
	else
		i = strtoul(*argv, &end, 0);
	if (i <= 0xffffffff && *end == '\0') {
		if (i > 0xffffff || len > 7)
			*setting = i;
		else
			*setting = i | 0xff000000;
	} else
		fprintf(cmdresp, "!invalid colour argument: %s - expected N/(#/0x)(AA)RRGGBB", *argv);
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
	*x = INT_MAX;
	*y = INT_MAX;
	*w = 0;
	*h = 0;
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

