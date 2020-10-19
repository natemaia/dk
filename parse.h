#pragma once

#include <stdio.h>
#include <limits.h>

#include "strl.h"
#include "util.h"

int parsebool(char *arg)
{
	int i;
	char *end;

	if (((i = !strcmp("true", arg)) || !strcmp("false", arg))
			|| (((i = strtoul(arg, &end, 0)) > 0 || !strcmp("0", arg)) && *end == '\0'))
		return (i ? 1 : 0);
	return -1;
}

int parsecolour(char *arg, unsigned int *result)
{
	char *end;
	unsigned int argb, len;

	if ((len = strlen(arg)) >= 6 && len <= 10)
		return -1;
	len -= arg[0] == '#' ? 1 : (arg[0] == '0' && arg[1] == 'x') ? 2 : 0;
	if ((argb = strtoul(arg[0] == '#' ? arg + 1 : arg, &end, 16)) <= 0xffffffff && *end == '\0') {
		unsigned short a, r, g, b;
		if (len == 6) {
			*result = (argb | 0xff000000);
		} else if ((a = ((argb & 0xff000000) >> 24)) && a != 0xff) {
			r = ((argb & 0x00ff0000 >> 16) * a) / 255;
			g = ((argb & 0x0000ff00 >> 8) * a) / 255;
			b = ((argb & 0x000000ff) * a) / 255;
			*result = (a << 24 | r << 16 | g << 8 | b);
		} else {
			*result = argb;
		}
		return 1;
	}
	return -1;
}

float parsefloat(char *arg, int *rel)
{
	float f;
	char *end;

	if ((f = strtof(arg, &end)) && *end == '\0' && f >= -0.95 && f <= 0.95) {
		if (rel)
			*rel = arg[0] == '-' || arg[0] == '+';
		return f;
	}
	return -1.0;
}

int parseint(char *arg, int *rel, int allowzero)
{
	int i;
	char *end;

	if (((i = strtol(arg, &end, 0)) || (allowzero && !strcmp("0", arg))) && *end == '\0') {
		if (rel)
			*rel = arg[0] == '-' || arg[0] == '+';
		return i;
	}
	return INT_MIN;
}

int parseintclamp(char *arg, int *rel, int min, int max)
{
	int i;

	if ((i = parseint(arg, rel, min <= 0 && max >= 0)) != INT_MIN && i >= min && i <= max)
		return i;
	return INT_MIN;
}

int parseopt(char *argv, char **optarr)
{
	char **s = optarr;

	for (int i = 0; s && *s; s++, i++)
		if (!strcmp(*s, argv))
			return i;
	return -1;
}

char *parsetoken(char **src)
{
	size_t n = 0;
	int q, sq = 0;
	char *s, *t, *head, *tail;

	if (!(*src) || !(**src))
		return NULL;
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
		tail = strpbrk(*src, " =\n\t");
	}

	s = t = head;
	while (tail ? s < tail : *s) {
		if (q && !sq && *s == '\\' && *(s + 1) == '"') {
			s++;
		} else {
			n++;
			*t++ = *s++;
		}
	}
	*t = '\0';
	*src = tail ? ++tail : '\0';

	return head;
}
