/* dk window manager
 *
 * see license file for copyright and license details
 * vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
 */

#include <sys/un.h>
#include <sys/socket.h>

#include <err.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "strl.h"
#include "util.h"

#ifndef VERSION
#define VERSION "2.1"
#endif

#ifndef INDENT
#define INDENT 2
#endif

static int json_pretty(int argc, char *argv[])
{
	size_t len = 0;
	FILE *f = stdin;
	char p, n, *c, *line = NULL;
	int lvl = 1, inkey = 0, instr = 0, first = 1;

	if (argc && *argv && !(f = fopen(*argv, "r"))) {
		perror("open");
		return 1;
	}

	while (getline(&line, &len, f) != -1 && (c = line)) {
		if (first) {
			first = 0;
			printf("%c\n", *c);
			p = *c++;
		}
		while (*c) {
			if (instr && (*c != '"' || *(c - 1) == '\\')) {
				printf("%c", *c++);
				continue;
			}
			n = *(c + 1);
			switch (*c) {
				case ',':
					printf("%c\n", *c);
					p = *c;
					break;
				case ':':
					printf("%c ", *c);
					inkey = 0;
					p = *c;
					break;
				case '"':
					if (instr) {
						instr = 0;
						printf("%c%s", *c, (n == '}' || n == ']') ? "\n" : "");
					} else if (!instr && !inkey && (p == ',' || p == '{' || p == '[')) {
						inkey = 1;
						printf("%*s%c", lvl * INDENT, lvl ? " " : "", *c);
					} else if (!instr && !inkey && p == ':') {
						instr = 1;
						printf("%c", *c);
					} else {
						printf("%c", *c);
					}
					p = *c;
					break;
				case '{':
				case '[':
					if (n == (*c) + 2) {
						printf("%c%c%s", *c, (*c) + 2, *(c + 2) == ',' || *(c + 2) != (*c) + 2 ? "" : "\n");
						c++;
						p = *c;
					} else if (p == ':') {
						printf("%c\n", *c);
						lvl++;
						p = *c;
					} else {
						printf("%*s%c%s", lvl * INDENT, lvl ? " " : "", *c, n != (*c) + 2 ? "\n" : "");
						lvl++;
						p = *c;
					}
					break;
				case '}':
				case ']':
					lvl--;
					printf("%*s%c%s", lvl * INDENT, lvl ? " " : "", *c, (n != ',' && (n == '}' || n == ']')) ? "\n" : "");
					p = *c;
					break;
				default:
					printf("%c", *c);
					break;
			}
			c++;
		}
		fflush(stdout);
	}
	free(line);
	fclose(f);
	return 0;
}

int main(int argc, char *argv[])
{
	if (argc == 1) {
		return usage(argv[0], VERSION, 1, 'h', "[-hv] <COMMAND>");
	} else if (!strcmp(argv[1], "-v") || !strcmp(argv[1], "-h")) {
		return usage(argv[0], VERSION, 0, argv[1][1], "[-hv] <COMMAND>");
	} else if (!strcmp(argv[1], "-p")) {
		return json_pretty(argc - 2, argv + 2);
	}

	size_t j = 0, n = 0;
	int i, fd, ret = 0, offs = 1;
	char *sock, *equal = NULL, *space = NULL, buf[BUFSIZ], resp[BUFSIZ];
	struct sockaddr_un addr;
	struct pollfd fds[] = {
		{-1,            POLLIN,  0},
		{STDOUT_FILENO, POLLHUP, 0},
	};

	if (!(sock = getenv("DKSOCK"))) {
		err(1, "unable to get socket path from environment");
	}
	addr.sun_family = AF_UNIX;
	check((fd = socket(AF_UNIX, SOCK_STREAM, 0)), "unable to create socket");
	fds[0].fd = fd;
	strlcpy(addr.sun_path, sock, sizeof(addr.sun_path));
	if (addr.sun_path[0] == '\0') {
		err(1, "unable to write socket path: %s", sock);
	}
	check(connect(fd, (struct sockaddr *)&addr, sizeof(addr)), "unable to connect socket");

	for (i = 1, j = 0, offs = 1; n + 1 < sizeof(buf) && i < argc; i++, j = 0, offs = 1) {
		if ((space = strchr(argv[i], ' ')) || (space = strchr(argv[i], '\t'))) {
			if (!(equal = strchr(argv[i], '=')) || space < equal) {
				buf[n++] = '"';
			}
			offs++;
		}
		while (n + offs + 1 < sizeof(buf) && argv[i][j]) {
			buf[n++] = argv[i][j++];
			if (equal && space > equal && buf[n - 1] == '=') {
				buf[n++] = '"';
				equal = NULL;
			}
		}
		if (offs > 1) {
			buf[n++] = '"';
		}
		buf[n++] = ' ';
	}
	buf[n - 1] = '\0';

	check(send(fd, buf, n, 0), "unable to send command");

	ssize_t s;
	while (poll(fds, 2, -1) > 0) {
		if (fds[0].revents & POLLIN) {
			if ((s = recv(fd, resp, sizeof(resp) - 1, 0)) > 0) {
				resp[s] = '\0';
				if ((ret = *resp == '!')) {
					fprintf(stderr, "%s: error: %s\n", argv[0], resp + 1);
					fflush(stderr);
				} else {
					fprintf(stdout, "%s\n", resp);
					fflush(stdout);
				}
			} else {
				break;
			}
		}
		if (fds[1].revents & (POLLERR | POLLHUP)) {
			break;
		}
	}
	close(fd);
	return ret;
}
