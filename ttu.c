/* ttu: A program to silently convert TCP sockets to Unix sockets
 * Copyright (C) 2014 Cyphar
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * 1. The above copyright notice and this permission notice shall be included in
 *    all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <ttu.h>

#define TTU_LICENSE \
	"ttu: A program to silently convert TCP sockets to Unix sockets\n" \
	"Copyright (C) 2014 Cyphar\n" \
	"\n" \
	"Permission is hereby granted, free of charge, to any person obtaining a copy of\n" \
	"this software and associated documentation files (the \"Software\"), to deal in\n" \
	"the Software without restriction, including without limitation the rights to\n" \
	"use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of\n" \
	"the Software, and to permit persons to whom the Software is furnished to do so,\n" \
	"subject to the following conditions:\n" \
	"\n" \
	"1. The above copyright notice and this permission notice shall be included in\n" \
	"   all copies or substantial portions of the Software.\n" \
	"\n" \
	"THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n" \
	"IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n" \
	"FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n" \
	"AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n" \
	"LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\n" \
	"OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\n" \
	"SOFTWARE.\n"

#define DEFAULT_LIBNAME "libttu.so"

static const char *lpath = NULL;
static const char *lname = DEFAULT_LIBNAME;

static int quiet = 0;
extern const char *__progname;

static void license(void) {
	fprintf(stderr, TTU_LICENSE);
} /* license() */

static void usage(void) {
	fprintf(stderr, "usage: %s [-Lhq] [-b bind-map] [-c connect-map] [-l library-name] [-p library-path] program [args]\n", __progname);
} /* usage() */

static void _bail(char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);

	fprintf(stderr, "[E:ttu] ");
	vfprintf(stderr, fmt, ap);
	fflush(stderr);

	va_end(ap);
	abort();
} /* _warn() */

static void _info(char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);

	fprintf(stderr, "[I:ttu] ");
	vfprintf(stderr, fmt, ap);
	fflush(stderr);

	va_end(ap);
} /* _warn() */

static void bake_args(int argc, char **argv) {
	int ch;

	while((ch = getopt(argc, argv, "b:c:l:p:Lhq")) > 0) {
		switch(ch) {
			case 'b':
				setenv(TTU_BIND_ENV, optarg, 1);
				break;
			case 'c':
				setenv(TTU_CONNECT_ENV, optarg, 1);
				break;
			case 'l':
				lname = optarg;
				break;
			case 'p':
				lpath = optarg;
				break;
			case 'L':
				license();
				exit(0);
				break;
			case 'h':
				usage();
				exit(0);
				break;
			case 'q':
				quiet = 1;
				break;
		}
	}
} /* bake_args() */

static char *which(const char *file, const char *path) {
	char *prefix = NULL,
		 *_path = strdup(path);

	prefix = strtok(_path, ":");

	while(prefix) {
		struct stat st;
		char *full = malloc(strlen(prefix) + strlen(file) + 2);

		/* create full path */
		strcpy(full, prefix);
		strcat(full, "/");
		strcat(full, file);

		/* file found! */
		if(!stat(full, &st)) {
			free(_path);
			return full;
		}

		/* next token */
		free(full);
		prefix = strtok(NULL, ":");
	}

	free(_path);
	return NULL;
} /* which() */

int main(int argc, char **argv) {
	bake_args(argc, argv);

	if(optind >= argc) {
		usage();
		return 1;
	}

	char *libttu = NULL;
	if (lpath) {
		libttu = which(lname, lpath);
		if(!libttu)
			_bail("could not find '%s' in '%s'\n", lname, lpath);
	} else {
		libttu = strdup(DEFAULT_LIBNAME);
	}

	if (getenv("LD_PRELOAD")) {
		const char *p = getenv("LD_PRELOAD");
		size_t pl = strlen(p);
		size_t l = strlen(libttu);
		char *n = realloc(libttu, pl + l + 2);
		if (n == NULL)
			abort();
		libttu = n;
		strcat(n, ":");
		strcat(n, p);
	}

	setenv("LD_PRELOAD", libttu, 1);

	if(!quiet)
		_info("running %s\n", argv[optind]);

	execvp(argv[optind], argv + optind);

	perror("execvp");
	return -1;
}
