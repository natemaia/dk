/* dk - /dəˈkā/ window manager
 *
 * see license file for copyright and license details
 * vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
 */

#pragma once

#include "common.h"

int cmdborder(char **argv);
int cmdcycle(char **argv);
int cmdfakefull(char **argv);
int cmdfloat(char **argv);
int cmdfocus(char **argv);
int cmdfollow(Workspace *ws);
int cmdfull(char **argv);
int cmdgappx(char **argv);
int cmdkill(char **argv);
int cmdlayout(char **argv);
int cmdmon(char **argv);
int cmdmouse(char **argv);
int cmdsplit(char **argv);
int cmdmors(char **argv);
int cmdpad(char **argv);
int cmdresize(char **argv);
int cmdrule(char **argv);
int cmdsend(Workspace *ws);
int cmdset(char **argv);
int cmdstick(char **argv);
int cmdswap(char **argv);
int cmdview(Workspace *ws);
int cmdwin(char **argv);
int cmdwm(char **argv);
int cmdws(char **argv);
int cmdwsdef(char **argv);

#include "strl.h"
#include "parse.h"
