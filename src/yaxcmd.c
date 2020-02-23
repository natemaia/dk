#include <err.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef VERSION
#define VERSION "0.2"
#endif

int main(int argc, char *argv[])
{
	int i;
	FILE *f;
	size_t j = 0, n = 0, bufsiz = 2048;
	char *path, buf[bufsiz];

	if (argc == 1 || (argv[1][0] == '-' && argv[1][1] == 'h'))
		errx(1, "usage: %s command", argv[0]);
	if (argv[1][0] == '-' && argv[1][1] == 'v')
		errx(0, "%s "VERSION, argv[0]);

	for (i = 1; n + 1 < bufsiz && i < argc; i++) {
		for (j = 0; n + 1 < bufsiz && argv[i][j]; j++)
			buf[n++] = argv[i][j];	
		buf[n++] = ' ';
	}
	buf[n - 1] = '\0';

	if (!(path = getenv("YAXWM_FIFO")))
		errx(1, "unable to get file path from YAXWM_FIFO");
	if (!(f = fopen(path, "a")))
		err(1, "unable to open file: %s", path);
	if (!fprintf(f, "%s", buf))
		warn("unable to write to file: %s", path);
	fclose(f);

	return 0;
}
