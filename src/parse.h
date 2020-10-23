#pragma once

#include "common.h"

int parsebool(char *arg);
float parsefloat(char *arg, int *rel);
int parseint(char *arg, int *rel, int allowzero);
int parseintclamp(char *arg, int *rel, int min, int max);
int parsecolour(char *arg, unsigned int *result);
int parseopt(char *arg, char **optarr);
int parsegeom(char *arg, char type, int *i, int *rel, int *grav);
Client *parseclient(char *arg, int *ebadwin);
Workspace *parsewsormon(char *arg, int mon);
void parsecmd(char *buf);
char *parsetoken(char **src);

#include "strl.h"
#include "util.h"
#include "cmd.h"

/* if you need to include config.h do it last */
#include "config.h"
