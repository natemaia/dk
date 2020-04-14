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
		fprintf(cmdresp, "!invalid boolean argument, expected true/1 or false/0: %s", *argv);
	return argv;
}

char **parsecolor(char **argv, int *setting)
{
	int i;
	char *end;

	if (!argv || !*argv)
		return argv;
	if (**argv == '#' && strlen(*argv) == 7)
		i = strtol(*argv + 1, &end, 16);
	else
		i = strtol(*argv, &end, 0);
	if (i >= 0x000000 && i <= 0xffffff && *end == '\0')
		*setting = i;
	else
		fprintf(cmdresp,
				"!invalid colour argument, expected (#/0x)(NUM/HEX): %s", *argv);
	return argv;
}

char **parsefloat(char **argv, float *setting, int *rel)
{
	float f;
	char *end;

	if (!argv || !*argv)
		return argv;
	if ((f = strtof(*argv, &end)) != 0.0
			&& ((f >= 0.1 && f <= 0.9) || (f <= -0.1 && f >= -0.9))
			&& *end == '\0')
	{
		if (rel) /* check if it's a relative number (has a sign) */
			*rel = **argv == '-' || **argv == '+';
		*setting = f;
	} else
		fprintf(cmdresp, "!invalid or incomplete float argument: %s", *argv);
	return argv;
}

char **parseint(char **argv, int *setting, int *rel, int allowzero)
{
	int i;
	char *end;

	if (!argv || !*argv)
		return argv;
	if (((i = strtol(*argv, &end, 0)) || (allowzero && !strcmp("0", *argv)))
			&& *end == '\0')
	{
		if (rel) /* check if it's a relative number (has a sign) */
			*rel = **argv == '-' || **argv == '+';
		*setting = i;
	} else
		fprintf(cmdresp, "!invalid integer argument, expected (-/+)NUM: %s", *argv);
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
	else
		fprintf(cmdresp, "!invalid integer argument, expected (-/+)NUM, >=%d and <=%d: %s",
				min, max, *argv);
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

