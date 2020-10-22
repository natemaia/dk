#include <sys/un.h>
#include <sys/socket.h>

#include <poll.h>
#include <stdio.h>
#include <unistd.h>

#include "include/strl.c"
#include "include/util.c"

#ifndef VERSION
#define VERSION "0.84"
#endif

int main(int argc, char *argv[])
{
	ssize_t s;
	size_t j = 0, n = 0;
	int i, fd, ret = 0, offs = 1;
	char *sock, *eq = NULL, *sp = NULL, buf[BUFSIZ], resp[BUFSIZ];
	struct sockaddr_un addr;
	struct pollfd fds[] = {
		{ -1,            POLLIN,  0 },
		{ STDOUT_FILENO, POLLHUP, 0 },
	};

	if (argc == 1 || !strcmp(argv[1], "-h"))
		errx(argc == 1, "usage: %s [-hv] <COMMAND>", argv[0]);
	if (!strcmp(argv[1], "-v"))
		errx(0, "%s "VERSION, argv[0]);

	if (!(sock = getenv("YAXWM_SOCK")))
		err(1, "unable to get socket path from environment");

	addr.sun_family = AF_UNIX;
	check((fd = socket(AF_UNIX, SOCK_STREAM, 0)), "unable to create socket");
	fds[0].fd = fd;

	strlcpy(addr.sun_path, sock, sizeof(addr.sun_path));
	if (addr.sun_path[0] == '\0')
		err(1, "unable to write socket path: %s", sock);
	check(connect(fd, (struct sockaddr *)&addr, sizeof(addr)), "unable to connect socket");

	for (i = 1, j = 0, offs = 1; n + 1 < sizeof(buf) && i < argc; i++, j = 0, offs = 1) {
		if ((sp = strchr(argv[i], ' ')) || (sp = strchr(argv[i], '\t'))) {
			if (!(eq = strchr(argv[i], '=')) || sp < eq) {
				/* no equal found or equal is part of the quoted string */
				buf[n++] = '"';	
			}
			offs++;
		}
		while (n + offs < sizeof(buf) && argv[i][j]) {
			buf[n++] = argv[i][j++];	
			if (eq && sp > eq && buf[n - 1] == '=') {
				buf[n++] = '"';
				eq = NULL;
			}
		}
		if (offs > 1)
			buf[n++] = '"';
		buf[n++] = ' ';
	}
	buf[n - 1] = '\0';
	check(send(fd, buf, n, 0), "unable to send command");

	while (poll(fds, 2, 1000) > 0) {
		if (fds[1].revents & (POLLERR | POLLHUP))
			break;
		if (fds[0].revents & POLLIN) {
			if ((s = recv(fd, resp, sizeof(resp) - 1, 0)) > 0) {
				resp[s] = '\0';
				if ((ret = *resp == '!')) {
					fprintf(stderr, "yaxcmd: error: %s\n", resp + 1);
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
