/* dk - /dəˈkā/ window manager
 *
 * see license file for copyright and license details
 * vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
 */

#include <sys/un.h>
#include <sys/socket.h>

#include <poll.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>

#include "strl.h"
#include "util.h"

#ifndef VERSION
#define VERSION "1.0"
#endif

int main(int argc, char *argv[])
{
	ssize_t s;
	size_t j = 0, n = 0;
	int i, fd, ret = 0, offs = 1;
	char *sock, *equal = NULL, *space = NULL, buf[BUFSIZ], resp[BUFSIZ];
	struct sockaddr_un addr;
	struct pollfd fds[] = {
		{ -1,            POLLIN,  0 },
		{ STDOUT_FILENO, POLLHUP, 0 },
	};

	if (!strcmp(argv[1], "-v") || !strcmp(argv[1], "-h"))
		return usage(argv[0], VERSION, 0, argv[1][1], "[-hv] <COMMAND>");
	else if (argc == 1)
		return usage(argv[0], VERSION, 1, 'h', "[-hv] <COMMAND>");

#ifdef __OpenBSD__
	if (pledge("stdio rpath wpath cpath tmppath flock unix", NULL) == -1)
		err(1, "pledge");
#endif

	if (!(sock = getenv("DKSOCK")))
		err(1, "unable to get socket path from environment");
	addr.sun_family = AF_UNIX;
	check((fd = socket(AF_UNIX, SOCK_STREAM, 0)), "unable to create socket");
	fds[0].fd = fd;
	strlcpy(addr.sun_path, sock, sizeof(addr.sun_path));
	if (addr.sun_path[0] == '\0')
		err(1, "unable to write socket path: %s", sock);
	check(connect(fd, (struct sockaddr *)&addr, sizeof(addr)), "unable to connect socket");

	for (i = 1, j = 0, offs = 1; n + 1 < sizeof(buf) && i < argc; i++, j = 0, offs = 1) {
		if ((space = strchr(argv[i], ' ')) || (space = strchr(argv[i], '\t'))) {
			if (!(equal = strchr(argv[i], '=')) || space < equal)
				buf[n++] = '"';
			offs++;
		}
		while (n + offs < sizeof(buf) && argv[i][j]) {
			buf[n++] = argv[i][j++];
			if (equal && space > equal && buf[n - 1] == '=') {
				buf[n++] = '"';
				equal = NULL;
			}
		}
		if (offs > 1)
			buf[n++] = '"';
		buf[n++] = ' ';
	}
	buf[n - 1] = '\0';

	check(send(fd, buf, n, 0), "unable to send command");
	while (poll(fds, 2, -1) > 0) {
		if (fds[1].revents & (POLLERR | POLLHUP)) break;
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
	}
	close(fd);
	return ret;
}
