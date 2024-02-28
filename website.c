#include <sys/mman.h>
#if __has_include(<sys/sendfile.h>)
#include <sys/sendfile.h>
#define __HAS_SENDFILE
#endif
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <fcntl.h>
#include <stdlib.h>
#include <pwd.h>

#include "tmplfuncs.gen"

#define DEFAULT_PORT 80
#define BUFLEN 4096
#define STATIC_DIRECTORY "./static/"
#define SERVE_STATIC_FROM "/static/"

#define RESP_200 "HTTP/1.0 200 OK\r\n"
#define RESP_400 "HTTP/1.0 400 Bad Request\r\n"
#define RESP_404 "HTTP/1.0 404 Not Found\r\n"
#define RESP_405 "HTTP/1.0 405 Method Not Allowed\r\n"
#define RESP_500 "HTTP/1.0 500 Internal Server Error\r\n"
#define RESP_501 "HTTP/1.0 501 Not Implemented\r\n"
#define RESP_505 "HTTP/1.0 505 HTTP Version Not Supported\r\n"

#define CONTENT_TYPE_HTML "Content-Type: text/html\r\n"
#define CONTENT_TYPE_PLAINTEXT "Content-Type: text/plain\r\n"
#define END_HDRS "\r\n"

#define ARRAY_LEN(arr) (sizeof(arr) / sizeof((arr)[0]))

#define _send_tmpl(FD, TMPLNAME, ...) _TMPLFUNC_ ## TMPLNAME (FD, (struct _tmplargs_ ## TMPLNAME) __VA_ARGS__)

#define render_with_hdrs(FD, HDRS, TMPLNAME, ...) \
	do { \
		const char *__resp = HDRS; \
		write(FD, __resp, strlen(__resp)); \
		_send_tmpl(FD, TMPLNAME, __VA_ARGS__); \
	} while (0)

#define render_html(FD, TMPLNAME, ...) \
	render_with_hdrs( \
		FD, \
		RESP_200 CONTENT_TYPE_HTML END_HDRS, \
		TMPLNAME, \
		__VA_ARGS__ \
	)

static const char *src_lines[] = {
#include "quinelines.gen"
};

static int staticdirfd;

static _Atomic(unsigned long) *indexcount;

enum method {
	METHOD_GET,
	METHOD_HEAD,
	METHOD_POST,
	METHOD_NOT_RECOGNIZED
};

void write_quine(int fd, bool verbose)
{
	for (size_t lineidx = 0; lineidx < ARRAY_LEN(src_lines); lineidx++) {
		const char *curr_line = src_lines[lineidx];
		if (strcmp(curr_line, "***LINES***")) {
			dprintf(fd, "%s\n", curr_line);
			continue;
		}

		if (!verbose) {
			dprintf(fd, "\t\"***** lines omitted for brevity - see /quine.c for full quine *****\"\n");
			continue;
		}

		for (size_t i = 0; i < ARRAY_LEN(src_lines); i++) {
			write(fd, "\"", 1);

			const char *line = src_lines[i];
			size_t run = 0;
			while (line[run] != '\0') {
				if (line[run] == '\\' || line[run] == '"') {
					if (run)
						write(fd, line, run);
					dprintf(fd, "\\%c", line[run]);
					line += run + 1;
					run = 0;
				} else {
					run++;
				}
			}
			if (run)
				write(fd, line, run);

			write(fd, "\",\n", 3);
		}
	}
}

int openat_beneath(int dirfd, const char *pathname, int flags)
{
	if (strstr(pathname, "..") || strstr(pathname, "//"))
		return -1;

	return openat(dirfd, pathname, flags);
}

int send_file_in_dir(int connfd, const char *status_and_headers, int dirfd, const char *filename)
{
	int filefd = openat_beneath(dirfd, filename, O_RDONLY);
	if (filefd == -1) {
		perror("openat_beneath");
		return -1;
	}

	struct stat stats;
	fstat(filefd, &stats);

	if (!S_ISREG(stats.st_mode)) {
		fprintf(stderr, "Can't send file `%s` - not a regular file\n", filename);
		close(filefd);
		return -1;
	}

	write(connfd, status_and_headers, strlen(status_and_headers));
#ifdef __HAS_SENDFILE
	sendfile(connfd, filefd, NULL, stats.st_size);
#else
	char *buf = malloc(stats.st_size);
	read(filefd, buf, stats.st_size);
	write(connfd, buf, stats.st_size);
	free(buf);
#endif

	close(filefd);
	return 0;
}

void not_found(int fd)
{
	render_with_hdrs(fd, RESP_404 CONTENT_TYPE_HTML END_HDRS, 404_page_html, {});
}

void handle_request(int fd, struct sockaddr_in *sockip, enum method method, char *uri)
{
	if (!strcmp(uri, "/")) {
		time_t now = time(NULL);
		struct tm *lnow = localtime(&now);
		#define TIMEBUFLEN 1024
		char timebuf[TIMEBUFLEN];
		strftime(timebuf, TIMEBUFLEN, "%a %b %e %H:%M:%S %Z %Y", lnow);
		unsigned long nreqs = 1 + atomic_fetch_add(indexcount, 1);

		render_html(fd, index_html, { .nowdate = timebuf, .reqcnt = nreqs });
	} else if (!strcmp(uri, "/quine.c")) {
		static const char resp[] = RESP_200 CONTENT_TYPE_PLAINTEXT END_HDRS;
		write(fd, resp, strlen(resp));
		write_quine(fd, true);
	} else if (!strcmp(uri, "/website.c")) {
		static const char resp[] = RESP_200 CONTENT_TYPE_PLAINTEXT END_HDRS;
		write(fd, resp, strlen(resp));
		write_quine(fd, false);
	} else if (!strcmp(uri, "/echoip")) {
		char *stringified = inet_ntoa(sockip->sin_addr);

		render_html(fd, yourip_html, { .ip = stringified });
	} else if (!strncmp(uri, SERVE_STATIC_FROM, strlen(SERVE_STATIC_FROM))) {
		char *uri_in_dir = uri + strlen(SERVE_STATIC_FROM);
		char *hdrs;
		if (strstr(uri_in_dir, ".awk") || !strstr(uri_in_dir, ".")) {
			hdrs = RESP_200 CONTENT_TYPE_PLAINTEXT END_HDRS;
		} else {
			hdrs = RESP_200 END_HDRS;
		}

		if (send_file_in_dir(fd, hdrs, staticdirfd, uri_in_dir))
			not_found(fd);
	} else {
		not_found(fd);
	}
}

bool validate_request(int fd, enum method method, char *uri, char *vsn, char *hdrs)
{
	if (method == METHOD_NOT_RECOGNIZED) {
		static const char resp[] = RESP_501;
		write(fd, resp, strlen(resp));
		return false;
	}

	// we only do GETs for now
	if (method != METHOD_GET) {
		static const char resp[] = RESP_405 "Allow: GET\r\n" END_HDRS;
		write(fd, resp, strlen(resp));
		return false;
	}

	if (strcmp(vsn, "HTTP/1.0") && strcmp(vsn, "HTTP/1.1")) {
		static const char resp[] = RESP_505;
		write(fd, resp, strlen(resp));
		return false;
	}

	return true;
}

bool parse_request(char *req, enum method *method_out, char **uri_out, char **vsn_out, char **hdrs_out)
{
	// extract method
	char *method_str = req;
	while (*req != ' ') {
		if (*req++ == '\0')
			return false;
	}
	*req = '\0';
	if (!strcmp(method_str, "GET"))
		*method_out = METHOD_GET;
	else if (!strcmp(method_str, "HEAD"))
		*method_out = METHOD_HEAD;
	else if (!strcmp(method_str, "POST"))
		*method_out = METHOD_POST;
	else
		*method_out = METHOD_NOT_RECOGNIZED;

	// extract uri
	*uri_out = ++req;
	while (*req != ' ') {
		if (*req++ == '\0')
			return false;
	}
	*req = '\0';

	// extract version
	*vsn_out = ++req;
	while (*req != '\r') {
		if (*req++ == '\0')
			return false;
	}
	*req = '\0';

	// extract headers
	if (*++req != '\n')
		return false;
	*hdrs_out = ++req;
	if (*req != '\0') {
		while (strncmp(req, "\r\n\r\n", 4)) {
			if (*req == '\0')
				return false;
			req++;
		}
		*req = '\0';
	}

	return true;
}

int open_dir_for_serving(const char *pathname)
{
	int fd = open(pathname, O_DIRECTORY | O_RDONLY);
	if (fd == -1) {
		fprintf(stderr, "Can't open directory \"%s\" for serving: %s\n", pathname, strerror(errno));
	}
	return fd;
}

int main(int argc, char **argv)
{
#ifdef __OpenBSD__
	unveil(PAGES_DIRECTORY, "r");
	unveil(STATIC_DIRECTORY, "r");
	unveil(NULL, NULL);

	if (pledge("rpath stdio inet getpw proc id", NULL)) {
		perror("pledge");
		return 1;
	}
#endif

	int bindport = DEFAULT_PORT;
	char *endptr;
	if (argc == 2) {
		bindport = strtol(argv[1], &endptr, 10);
		if (*endptr != '\0' || bindport < 1 || bindport > 65535) {
			puts("port must be a number 1..65535");
			return 1;
		}
	}

	signal(SIGCHLD, SIG_IGN);

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1) {
		perror("socket");
		return 1;
	}

	const int on = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) {
		perror("setsockopt");
		return 1;
	}

	struct sockaddr_in bind_addr;
	bind_addr.sin_port = htons(bindport);
	bind_addr.sin_addr.s_addr = INADDR_ANY;
	bind_addr.sin_family = AF_INET;
	if (bind(sockfd, (struct sockaddr *) &bind_addr, sizeof(bind_addr))) {
		perror("bind");
		return 1;
	}

	if (getuid() == 0) {
		struct passwd *webpwd = getpwnam("simon");
		if (!webpwd) {
			perror("getpwnam");
			return 1;
		}

		if (setuid(webpwd->pw_uid)) {
			perror("setuid");
			return 1;
		}
	}

	indexcount = mmap(NULL, sizeof(*indexcount), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	*indexcount = 0;

	staticdirfd = open_dir_for_serving(STATIC_DIRECTORY);
	if (staticdirfd == -1)
		return 1;

	if (listen(sockfd, 5)) {
		perror("listen");
		return 1;
	}

	for (;;) {
		struct sockaddr_in saddr;
		socklen_t saddrlen = sizeof(saddr);
		int connfd = accept(sockfd, (struct sockaddr *) &saddr, &saddrlen);
		if (connfd == -1) {
			perror("accept");
			continue;
		}

		int pid = fork();
		if (pid == -1) {
			perror("fork");
			shutdown(connfd, SHUT_RDWR);
			close(connfd);
			continue;
		}

		if (pid) { // parent
			close(connfd);
		} else { // child
			char buf[BUFLEN] = {0};
			enum method method;
			char *uri, *vsn, *hdrs;

			int nread = read(connfd, buf, BUFLEN - 1); // -1 to keep a NUL at the end
			if (nread == -1) {
				perror("read");
			} else if (nread == BUFLEN - 1) {
				render_with_hdrs(connfd, RESP_400 CONTENT_TYPE_HTML END_HDRS, req2long_html, {});
			} else if (parse_request(buf, &method, &uri, &vsn, &hdrs)) {
				if (validate_request(connfd, method, uri, vsn, hdrs))
					handle_request(connfd, &saddr, method, uri);
			} else { // parsing failed
				static const char resp[] = RESP_400;
				write(connfd, resp, strlen(resp));
			}

			if (shutdown(connfd, SHUT_RDWR))
				perror("shutdown");
			close(connfd);
			close(staticdirfd);
			return 0;
		}
	}

	munmap(indexcount, sizeof(*indexcount));
	close(sockfd);
	close(staticdirfd);
}
