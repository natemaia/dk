/* dk window manager
 *
 * see license file for copyright and license details
 * vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
 */

#pragma once

int parsebool(char *arg);
Client *parseclient(char *arg, int *ebadwin);
void parsecmd(char *buf);
int parsecolour(char *arg, unsigned int *result);
int parsecoord(char *arg, char type, int *i, int *rel, int *grav);
float parsefloat(char *arg, int *rel);
int parseint(char *arg, int *rel, int allowzero);
int parseintclamp(char *arg, int *rel, int min, int max);
int parseopt(char *arg, const char **optarr, int len_optarr);
Workspace *parsewsormon(char *arg, int mon);
