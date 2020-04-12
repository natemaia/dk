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

char **parsecolor(char **argv, int *setting)
{
	int i;
	char *end;

	if (!argv || !*argv)
		return argv;
	if ((i = (**argv == '#' && strlen(*argv) == 7) ? strtol(*argv + 1, &end, 16)
				: strtol(*argv, &end, 0)) >= 0x000000 && i <= 0xffffff && *end == '\0')
	{
		*setting = i;
	} else
		fprintf(cmdresp, "!invalid or incomplete colour argument: %s", *argv);
	return argv;
}

char **parseint(char **argv, int *setting, int *rel, int allowzero)
{
	int i;
	char *end;

	if (!argv || !*argv)
		return argv;
	if (((i = strtol(*argv, &end, 0)) || (allowzero && !strcmp("0", *argv))) && *end == '\0') {
		if (rel) /* check if it's a relative number (has a sign) */
			*rel = **argv == '-' || **argv == '+';
		*setting = i;
	} else
		fprintf(cmdresp, "!invalid or incomplete integer argument: %s", *argv);
	return argv;
}

char **parsefloat(char **argv, float *setting, int *rel)
{
	float f;
	char *end;

	if (!argv || !*argv)
		return argv;
	if ((f = strtof(*argv, &end)) != 0.0 && ((f >= 0.1 && f <= 0.9) || (f <= -0.1 && f >= -0.9))
			&& *end == '\0')
	{
		if (rel) /* check if it's a relative number (has a sign) */
			*rel = **argv == '-' || **argv == '+';
		*setting = f;
	} else
		fprintf(cmdresp, "!invalid or incomplete float argument: %s", *argv);
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

