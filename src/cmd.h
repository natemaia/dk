/* dk window manager
 *
 * see license file for copyright and license details
 * vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
 */

#pragma once

int adjustisetting(int i, int rel, int *val, int other, int border);
int adjustwsormon(char **argv);
int cmdborder(char **argv);
int cmdcycle(__attribute__((unused)) char **argv);
int cmdexit(__attribute__((unused)) char **argv);
int cmdexit(__attribute__((unused)) char **argv);
int cmdfakefull(__attribute__((unused)) char **argv);
int cmdfloat(char **argv);
int cmdfocus(char **argv);
int cmdfollow(Workspace *ws);
int cmdfull(__attribute__((unused)) char **argv);
int cmdgappx(char **argv);
int cmdkill(__attribute__((unused)) char **argv);
int cmdlayout(char **argv);
int cmdmon(char **argv);
int cmdmors(char **argv);
int cmdmouse(char **argv);
int cmdmvstack(char **argv);
int cmdpad(char **argv);
int cmdresize(char **argv);
int cmdrestart(__attribute__((unused)) char **argv);
int cmdrule(char **argv);
int cmdsend(Workspace *ws);
int cmdset(char **argv);
int cmdsplit(char **argv);
int cmdstatus(char **argv);
int cmdstick(__attribute__((unused)) char **argv);
int cmdswap(char **argv);
int cmdview(Workspace *ws);
int cmdwin(char **argv);
int cmdws(char **argv);
int cmdws_(char **argv);
