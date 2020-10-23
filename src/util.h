#pragma once

#include <sys/wait.h>

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <err.h>

void check(int i, char *msg);
void *ecalloc(size_t elems, size_t size);
char *itoa(int n, char *s);
void sighandle(int sig);
int usage(char *prog, char *ver, int e, char flag, char *flagstr);
