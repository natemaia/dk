#pragma once

#include <sys/types.h>

#include <err.h>
#include <stdlib.h>

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
