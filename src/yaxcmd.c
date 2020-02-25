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
	size_t j = 0, n = 0;
	int i, sockfd, r = 0;
	struct sockaddr_un sockaddr;
	char *sock, buf[BUFSIZ], cmdresp[BUFSIZ];

	if (argc == 1 || (argv[1][0] == '-' && argv[1][1] == 'h'))
		errx(1, "usage: %s command", argv[0]);
	if (argv[1][0] == '-' && argv[1][1] == 'v')
		errx(0, "%s "VERSION, argv[0]);

	/* create socket */
	sockaddr.sun_family = AF_UNIX;
	if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		err(1, "unable to create socket");

	/* setup socket path */
	if (!(sock = getenv("YAXWM_SOCK")))
		sock = "/tmp/yaxwmsock";
	strlcpy(sockaddr.sun_path, sock, sizeof(sockaddr.sun_path));
	if (sockaddr.sun_path[0] == '\0')
		err(1, "unable to write socket path: %s", sock);

	/* connect to the socket */
	if (connect(sockfd, (struct sockaddr *)&sockaddr, sizeof(sockaddr)) < 0)
		err(1, "unable to connect to socket: %s", sock);

	/* copy argv into buf space separated */
	for (i = 1; n + 1 < sizeof(buf) && i < argc; i++) {
		for (j = 0; n + 1 < sizeof(buf) && argv[i][j]; j++)
			buf[n++] = argv[i][j];	
		buf[n++] = ' ';
	}
	buf[n - 1] = '\0';

	/* send the command */
	if (send(sockfd, buf, n, 0) == -1)
		err(1, "unable to send the command");

	/* poll for response message */
	struct pollfd fds[] = {
		{ sockfd,        POLLIN,  0 },
		{ STDOUT_FILENO, POLLHUP, 0 },
	};
	while (poll(fds, 2, -1) > 0) {
		if (fds[1].revents & (POLLERR | POLLHUP))
			break;
		if (fds[0].revents & POLLIN) {
			if ((s = recv(sockfd, cmdresp, sizeof(cmdresp) - 1, 0)) > 0) {
				r = 1;
				cmdresp[s] = '\0';
				fprintf(stderr, "%s\n", cmdresp);
			} else {
				break;
			}
		}
	}
	close(sockfd);
	return r;
}
