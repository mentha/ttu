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

#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <ohmic.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <ttu.h>
#include <unistd.h>

static void *_dlhandle;

static int (*_bind)(int, const struct sockaddr *, socklen_t);
static int (*_connect)(int, const struct sockaddr *, socklen_t);

static struct ohm_t *_bindmap = NULL;
static struct ohm_t *_connectmap = NULL;

static void _bail(char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);

	fprintf(stderr, "[E:libttu] ");
	vfprintf(stderr, fmt, ap);
	fflush(stderr);

	va_end(ap);
	abort();
} /* _warn() */

static void _warn(char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);

	fprintf(stderr, "[W:libttu] ");
	vfprintf(stderr, fmt, ap);
	fflush(stderr);

	va_end(ap);
} /* _warn() */

static void _info(char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);

	fprintf(stderr, "[I:libttu] ");
	vfprintf(stderr, fmt, ap);
	fflush(stderr);

	va_end(ap);
} /* _warn() */

static int _chrnstr(char *str, char ch) {
	if(!str)
		return -1;

	int i, count = 0, len = strlen(str);
	for(i = 0; i < len; i++)
		if(str[i] == ch)
			count++;

	return count;
} /* _chrnstr() */

/* "safe" wrapper to sprintf -- returns proper length string */
static char *ssprintf(char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);

	int len = vsnprintf(NULL, 0, fmt, ap);
	char *str = malloc(len + 1);
	if (!str)
		abort();

	va_start(ap, fmt);
	vsnprintf(str, len, fmt, ap);
	str[len] = '\0';

	va_end(ap);
	return str;
} /* ssprintf() */

static int _ttusock(int sockfd) {
	int newsockfd = 0,
		socktp = 0;

	socklen_t size = sizeof(int);

	getsockopt(sockfd, SOL_SOCKET, SO_TYPE, &socktp, &size);
	newsockfd = socket(AF_UNIX, socktp, 0);

	return dup2(newsockfd, sockfd);
} /* _ttusock() */

static char *_find_sockmap(struct ohm_t *map, struct in_addr addr, in_port_t port) {
	char *host = inet_ntoa(addr);

	/* possible keys, in order of preference */
	char *m_exact = ssprintf("%s:%d", host, port),
		 *m_port  = ssprintf("*:%d", port),
		 *m_host  = ssprintf("%s:*", host),
		 *m_any   = ssprintf("*:*");

	char *sockfile = ohm_search(map, m_exact, strlen(m_exact) + 1);

	if(!sockfile)
		sockfile = ohm_search(map, m_port, strlen(m_port) + 1);

	if(!sockfile)
		sockfile = ohm_search(map, m_host, strlen(m_host) + 1);

	if(!sockfile)
		sockfile = ohm_search(map, m_any, strlen(m_any) + 1);

	free(m_exact);
	free(m_port);
	free(m_host);
	free(m_any);
	return sockfile;
} /* _find_sockmap() */

static int getblocking(int fd)
{
	int fl = fcntl(fd, F_GETFL);
	if (fl < 0)
		return 1;
	if (fl & O_NONBLOCK)
		return 0;
	return 1;
}

static int setblocking(int fd, int v)
{
	int fl = fcntl(fd, F_GETFL);
	if (fl < 0)
		return 1;
	if (v)
		fl |= O_NONBLOCK;
	else
		fl &= ~O_NONBLOCK;
	return fcntl(fd, F_SETFL, fl) == -1;
}

static int bind_unix(int sockfd, const char *p, const char *sockfile)
{
	size_t pl = strlen(p);

	struct sockaddr_un uaddr;

	if (pl + 1 > sizeof(uaddr.sun_path)) {
		_warn("path too long: %s\n", sockfile);
		errno = EADDRNOTAVAIL;
		return -1;
	}

	uaddr.sun_family = AF_UNIX;
	memcpy(uaddr.sun_path, p, pl + 1);

	_ttusock(sockfd);

	int r = _bind(sockfd, (struct sockaddr *) &uaddr, sizeof(uaddr));
	if (!r)
		return 0;
	if (errno != EADDRINUSE)
		return -1;
	int s = socket(AF_UNIX, SOCK_STREAM, 0);
	if (setblocking(s, 0) ||
			!connect(s, (struct sockaddr *) &uaddr, sizeof(uaddr)) ||
			errno == EALREADY || errno == EINPROGRESS) {
		close(s);
		errno = EADDRINUSE;
		return -1;
	}
	close(s);
	unlink(p);
	return _bind(sockfd, (struct sockaddr *) &uaddr, sizeof(uaddr));
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
	if (addr->sa_family != AF_INET || addrlen < sizeof(struct sockaddr_in))
		return _bind(sockfd, addr, addrlen);

	const struct sockaddr_in *iaddr = (const struct sockaddr_in *) addr;
	const char *sockfile = _find_sockmap(_bindmap, iaddr->sin_addr,
			htons(iaddr->sin_port));
	if (sockfile == NULL)
		return _bind(sockfd, addr, addrlen);

	int mode = -1;

	size_t sfl = strlen(sockfile);
	if (sfl == 0 || sockfile[sfl - 1] == ':') {
		errno = EADDRNOTAVAIL;
		return -1;
	}

	const char *p, *np;
	unsigned i;

	for (i = 0, p = sockfile;
			(np = strchr((char *) p, ':')) != NULL;
			i++, p = np + 1) {
		if (i == 0) {
			mode = 0;
			for (; p < np; p++) {
				if (*p >= '0' && *p <= '7') {
					mode = mode * 8 + *p - '0';
				} else {
					_warn("invalid mode: %s\n", sockfile);
					errno = EADDRNOTAVAIL;
					return -1;
				}
			}
		} else {
			_warn("unknown parameters at pos %d: %s\n", i, sockfile);
			errno = EADDRNOTAVAIL;
			return -1;
		}
	}

	int blocking = getblocking(sockfd);

	if (bind_unix(sockfd, p, sockfile))
		return -1;

	setblocking(sockfd, blocking);

	if (mode >= 0) {
#ifdef __linux__
		if (chmod(p, mode)) {
			_warn("chmod failed: %s\n", strerror(errno));
			errno = EACCES;
			return -1;
		}
#else
		_warn("setting mode is not supported\n");
		errno = EACCES;
		return -1;
#endif
	}

	return 0;
} /* bind() */

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
	if(addr->sa_family == AF_INET || addr->sa_family == AF_INET6) {
		struct sockaddr_in *iaddr = (struct sockaddr_in *) addr;
		char *sockfile = _find_sockmap(_connectmap, iaddr->sin_addr, htons(iaddr->sin_port));

		if(sockfile != NULL) {
			struct sockaddr_un uaddr;

			memset(&uaddr, 0, sizeof(struct sockaddr_un));

			uaddr.sun_family = AF_UNIX;
			memcpy(uaddr.sun_path, sockfile, 108);

			addr = (struct sockaddr *) &uaddr;
			addrlen = sizeof(struct sockaddr_un);
			_ttusock(sockfd);
		}
	}

	return _connect(sockfd, addr, addrlen);
} /* connect() */

static void *_dlsym(void *handle, char *name) {
	void *symbol = dlsym(handle, name);

	char *err = dlerror();
	if(err)
		_bail(err);

	return symbol;
} /* _dlsym() */

static char *_strdup(char *s) {
	char *r = strdup(s);
	if (!r)
		abort();
	return r;
} /* _strdup() */

static void _etohm(struct ohm_t *hm, char *env) {
	if(!env)
		return;

	env = _strdup(env);

	char *current = strtok(env, ",");
	if(!current) {
		free(env);
		return;
	}

	do {
		if(_chrnstr(current, '=') != 1) {
			_warn("ignoring phony argument: %s\n", current);
			continue;
		}

		char *addr = strtok(current, "="),
		     *sockfile = strtok(NULL, "=");

		if(_chrnstr(addr, ':') != 1) {
			_warn("ignoring phony argument: %s\n", addr);
			continue;
		}

		char *ahost = strtok(addr, ":"),
		     *aport = strtok(NULL, ":");

		if(!sockfile)
			break;

		if(!ahost)
			ahost = "*";

		if(!aport)
			aport = "*";

		_info("add mapping: '%s:%s'='%s'\n", ahost, aport, sockfile);

		addr = ssprintf("%s:%s", ahost, aport);
		ohm_insert(hm, addr, strlen(addr) + 1, sockfile, strlen(sockfile) + 1);

		free(addr);

	} while((current = strtok(NULL, ",")));

	free(env);
} /* _etohm() */

void __attribute__((constructor)) init(void) {
	_dlhandle = dlopen("libc.so.6", RTLD_LAZY | RTLD_LOCAL);

	if(!_dlhandle)
		_bail(dlerror());

	_bind    = _dlsym(_dlhandle, "bind");
	_connect = _dlsym(_dlhandle, "connect");

	_bindmap    = ohm_init(4, NULL);
	_connectmap = ohm_init(4, NULL);

	_etohm(_bindmap, getenv(TTU_BIND_ENV));
	_etohm(_connectmap, getenv(TTU_CONNECT_ENV));

	struct ohm_iter ii = ohm_iter_init(_bindmap);
	for(ii = ohm_iter_init(_bindmap); ii.key != NULL; ohm_iter_inc(&ii))
		unlink(ii.value);
} /* init() */

void __attribute__((destructor)) fini(void) {
	int err = dlclose(_dlhandle);

	if(err)
		_bail(dlerror());

	struct ohm_iter ii = ohm_iter_init(_bindmap);
	for(ii = ohm_iter_init(_bindmap); ii.key != NULL; ohm_iter_inc(&ii))
		unlink(ii.value);

	ohm_free(_bindmap);
	ohm_free(_connectmap);
} /* fini() */
