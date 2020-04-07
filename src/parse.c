/* yet another X window manager
* see license file for copyright and license details
* vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

extern FILE *cmdresp;
extern const char enoargs[];

char **parsecolor(char **argv, char *match, int *setting, int doincr)
{
	char *end;
	int i, matched = 0;

	if (!argv || !*argv)
		return argv;
	if (!match || (matched = !strcmp(match, *argv))) {
		if (matched)
			argv++;
		if (!*argv) {
			fprintf(cmdresp, "!%s colour %s", match ? match : "", enoargs);
		} else if ((i = (**argv == '#' && strlen(*argv) == 7) ? strtol(*argv + 1, &end, 16)
					: strtol(*argv, &end, 0)) >= 0x000000 && i <= 0xffffff && *end == '\0')
		{
			*setting = i;
			if (doincr)
				argv++;
		} else {
			if (matched && !doincr) /* incremented argv but couldn't find an argument */
				argv--;
			fprintf(cmdresp, "!invalid or incomplete colour argument for %s colour", match ? match : "");
		}
	}
	return argv;
}

char **parseint(char **argv, char *match, int *setting, int *rel, int zero, int doincr)
{
	char *end;
	int i, matched = 0;

	if (!argv || !*argv)
		return argv;
	if (!match || (matched = !strcmp(match, *argv))) {
		if (matched)
			argv++;
		if (!*argv) {
			fprintf(cmdresp, "!%s %s", match ? match : "", enoargs);
		} else if (((i = strtol(*argv, &end, 0)) || (zero && !strcmp("0", *argv))) && *end == '\0') {
			if (rel) /* check if it's a relative number (has a sign) */
				*rel = **argv == '-' || **argv == '+';
			*setting = i;
			if (doincr)
				argv++;
		} else {
			if (matched && !doincr) /* incremented argv but couldn't find an argument */
				argv--;
			fprintf(cmdresp, "!invalid or incomplete integer argument for %s setting", match ? match : "");
		}
	}
	return argv;
}

char **parsefloat(char **argv, char *match, float *setting, int *rel, int doincr)
{
	float f;
	char *end;
	int matched = 0;

	if (!argv || !*argv)
		return argv;
	if (!match || (matched = !strcmp(match, *argv))) {
		if (matched)
			argv++;
		if (!*argv) {
			fprintf(cmdresp, "!%s %s", match ? match : "", enoargs);
		} else if ((f = strtof(*argv, &end)) > 0.1 && f < 0.9 && *end == '\0') {
			if (rel) /* check if it's a relative number (has a sign) */
				*rel = **argv == '-' || **argv == '+';
			*setting = f;
			if (doincr)
				argv++;
		} else {
			if (matched && !doincr) /* incremented argv but couldn't find an argument */
				argv--;
			fprintf(cmdresp, "!invalid or incomplete float argument for %s setting", match ? match : "");
		}
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

