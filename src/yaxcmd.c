#include <err.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/socket.h>

#include "stringl.c"

#ifndef VERSION
#define VERSION "0.2"
#endif

int main(int argc, char *argv[])
{
	ssize_t s;
	int i, fd, r = 0;
	size_t j = 0, n = 0;
	char *sock, buf[BUFSIZ], resp[BUFSIZ];
	struct sockaddr_un addr;
	struct pollfd fds[] = {
		{ -1,            POLLIN,  0 },
		{ STDOUT_FILENO, POLLHUP, 0 },
	};

	if (argc == 1 || !strcmp(argv[1], "-h"))
		errx(argc == 1 ? 1 : 0, "usage: %s command", argv[0]);
	if (argv[1][0] == '-' && argv[1][1] == 'v')
		errx(0, "%s "VERSION, argv[0]);

	addr.sun_family = AF_UNIX;
	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		err(1, "unable to create socket");
	fds[0].fd = fd;
	if (!(sock = getenv("YAXWM_SOCK")))
		sock = "/tmp/yaxwmsock";
	strlcpy(addr.sun_path, sock, sizeof(addr.sun_path));
	if (addr.sun_path[0] == '\0')
		err(1, "unable to write socket path: %s", sock);
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		err(1, "unable to connect to socket: %s", sock);

	for (i = 1; n + 1 < sizeof(buf) && i < argc; i++) {
		for (j = 0; n + 1 < sizeof(buf) && argv[i][j]; j++)
			buf[n++] = argv[i][j];	
		buf[n++] = ' ';
	}
	buf[n - 1] = '\0';
	if (send(fd, buf, n, 0) < 0)
		err(1, "unable to send the command");

	while (poll(fds, 2, -1) > 0) {
		if (fds[1].revents & (POLLERR | POLLHUP))
			break;
		if (fds[0].revents & POLLIN) {
			if ((s = recv(fd, resp, sizeof(resp) - 1, 0)) > 0) {
				resp[s] = '\0';
				if (*resp == '!')
					fprintf(stderr, "error: %s\n", resp + 1);
				else {
					fprintf(stdout, "%s\n", resp);
					fflush(stdout);
				}
			} else {
				break;
			}
		}
	}
	close(fd);
	return r;
}
