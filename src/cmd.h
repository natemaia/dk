#pragma once

#include "common.h"

void cmdborder(char **argv);
void cmdcycle(char **argv);
void cmdfakefull(char **argv);
void cmdfloat(char **argv);
void cmdfocus(char **argv);
void cmdfollow(Workspace *ws);
void cmdfull(char **argv);
void cmdgappx(char **argv);
void cmdkill(char **argv);
void cmdlayout(char **argv);
void cmdmon(char **argv);
void cmdmouse(char **argv);
void cmdmsplit(char **argv);
void cmdnmaster(char **argv);
void cmdnstack(char **argv);
void cmdpad(char **argv);
void cmdresize(char **argv);
void cmdrule(char **argv);
void cmdsend(Workspace *ws);
void cmdset(char **argv);
void cmdssplit(char **argv);
void cmdstick(char **argv);
void cmdswap(char **argv);
void cmdview(Workspace *ws);
void cmdwin(char **argv);
void cmdwm(char **argv);
void cmdws(char **argv);
void cmdwsdef(char **argv);

#include "strl.h"
#include "parse.h"
