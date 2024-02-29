#include <sys/mman.h>
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

#define __STRINGIFY(X) # X
#define _STRINGIFY2(X) __STRINGIFY(X)
#define TRY(CALL) \
	({ \
		errno = 0; \
		typeof(CALL) __ret = CALL; \
		if (errno) { \
			perror("TRY(" __FILE__ ":" _STRINGIFY2(__LINE__) ")"); \
			exit(1); \
		} \
		__ret; \
	})

#define _send_tmpl(FD, TMPLNAME, ...) _tmplfunc_ ## TMPLNAME (FD, &(struct _tmplargs_ ## TMPLNAME) __VA_ARGS__)

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

static void write_quine(int fd, bool verbose)
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
			TRY(write(fd, "\"", 1));

			const char *line = src_lines[i];
			size_t run = 0;
			while (line[run] != '\0') {
				if (line[run] == '\\' || line[run] == '"') {
					if (run)
						TRY(write(fd, line, run));
					dprintf(fd, "\\%c", line[run]);
					line += run + 1;
					run = 0;
				} else {
					run++;
				}
			}
			if (run)
				TRY(write(fd, line, run));

			TRY(write(fd, "\",\n", 3));
		}
	}
}

static int openat_beneath(int dirfd, const char *pathname, int flags)
{
	if (strstr(pathname, "..") || strstr(pathname, "//"))
		return -1;

	return openat(dirfd, pathname, flags);
}

static int send_file_in_dir(int connfd, const char *status_and_headers, int dirfd, const char *filename)
{
	int filefd = openat_beneath(dirfd, filename, O_RDONLY);
	if (filefd == -1)
		return -1;

	struct stat stats;
	TRY(fstat(filefd, &stats));

	if (!S_ISREG(stats.st_mode)) {
		fprintf(stderr, "Can't send file `%s` - not a regular file\n", filename);
		TRY(close(filefd));
		return -1;
	}

	TRY(write(connfd, status_and_headers, strlen(status_and_headers)));
	char *buf = malloc(stats.st_size);
	TRY(read(filefd, buf, stats.st_size));
	TRY(write(connfd, buf, stats.st_size));
	free(buf);

	TRY(close(filefd));
	return 0;
}

static void not_found(int fd)
{
	render_with_hdrs(fd, RESP_404 CONTENT_TYPE_HTML END_HDRS, 404_page_html, {});
}

static void handle_request(int fd, struct sockaddr_in *sockip, enum method method, char *uri)
{
	if (!strcmp(uri, "/")) {
		time_t now = time(NULL);
		struct tm *lnow = localtime(&now);
		#define TIMEBUFLEN 1024
		char timebuf[TIMEBUFLEN];
		strftime(timebuf, TIMEBUFLEN, "%a %b %e %H:%M:%S %Z %Y", lnow);
		unsigned long nreqs = 1 + atomic_fetch_add(indexcount, 1);

		render_html(
			fd,
			index_html,
			{
				.nowdate = timebuf,
				.reqcnt = nreqs,
				.loc = ARRAY_LEN(src_lines)
			}
		);
	} else if (!strcmp(uri, "/quine.c")) {
		const char *resp = RESP_200 CONTENT_TYPE_PLAINTEXT END_HDRS;
		TRY(write(fd, resp, strlen(resp)));
		write_quine(fd, true);
	} else if (!strcmp(uri, "/website.c")) {
		const char *resp = RESP_200 CONTENT_TYPE_PLAINTEXT END_HDRS;
		TRY(write(fd, resp, strlen(resp)));
		write_quine(fd, false);
	} else if (!strcmp(uri, "/echoip")) {
		char *stringified = inet_ntoa(sockip->sin_addr);

		render_html(fd, yourip_html, { .ip = stringified, .port = sockip->sin_port });
	} else if (!strcmp(uri, "/howitworks")) {
		render_html(fd, howmake_html, {});
	} else if (!strcmp(uri, "/bounty")) {
		render_html(fd, bounty_html, {});
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

static bool validate_request(int fd, enum method method, char *uri, char *vsn, char *hdrs)
{
	if (method == METHOD_NOT_RECOGNIZED) {
		const char *resp = RESP_501 END_HDRS;
		TRY(write(fd, resp, strlen(resp)));
		return false;
	}

	// we only do GETs for now
	if (method != METHOD_GET) {
		const char *resp = RESP_405 "Allow: GET\r\n" END_HDRS;
		TRY(write(fd, resp, strlen(resp)));
		return false;
	}

	if (strcmp(vsn, "HTTP/1.0") && strcmp(vsn, "HTTP/1.1")) {
		const char *resp = RESP_505 END_HDRS;
		TRY(write(fd, resp, strlen(resp)));
		return false;
	}

	return true;
}

static bool parse_request(char *req, enum method *method_out, char **uri_out, char **vsn_out, char **hdrs_out)
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

static int open_dir_for_serving(const char *pathname)
{
	return TRY(open(pathname, O_DIRECTORY | O_RDONLY));
}

int main(int argc, char **argv)
{
#ifdef __OpenBSD__
	TRY(unveil(STATIC_DIRECTORY, "r"));
	TRY(unveil(NULL, NULL));

	#define PLEDGE_PREFORK "inet getpw proc id"
	#define PLEDGE_POSTFORK "stdio rpath"
	TRY(pledge(PLEDGE_PREFORK " " PLEDGE_POSTFORK, NULL));
#endif

	int bindport = DEFAULT_PORT;
	if (argc == 2) {
		char *endptr;
		bindport = strtol(argv[1], &endptr, 10);
		if (*endptr != '\0' || bindport < 1 || bindport > 65535) {
			puts("port must be a number 1..65535");
			return 1;
		}
	}

	TRY(sigaction(SIGCHLD, &(struct sigaction) { .sa_handler = SIG_IGN }, NULL));

	int sockfd = TRY(socket(AF_INET, SOCK_STREAM, 0));

	const int on = 1;
	TRY(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)));

	struct sockaddr_in bind_addr;
	bind_addr.sin_port = htons(bindport);
	bind_addr.sin_addr.s_addr = INADDR_ANY;
	bind_addr.sin_family = AF_INET;
	TRY(bind(sockfd, (struct sockaddr *) &bind_addr, sizeof(bind_addr)));

	if (getuid() == 0) {
		struct passwd *webpwd = TRY(getpwnam("simon"));
		TRY(setuid(webpwd->pw_uid));
	}

	indexcount = TRY(mmap(NULL, sizeof(*indexcount), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0));
	*indexcount = 0;

	staticdirfd = open_dir_for_serving(STATIC_DIRECTORY);

	TRY(listen(sockfd, 5));

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
#ifdef __OpenBSD__
			TRY(pledge(PLEDGE_POSTFORK, NULL));
#endif
			ualarm(1000/*ms*/ * 1000/*us/ms*/, 0);

			char buf[BUFLEN] = {0};
			enum method method;
			char *uri, *vsn, *hdrs;

			int nread = TRY(read(connfd, buf, BUFLEN - 1)); // -1 to keep a NUL at the end
			if (nread == BUFLEN - 1) {
				render_with_hdrs(connfd, RESP_400 CONTENT_TYPE_HTML END_HDRS, req2long_html, {});
			} else if (parse_request(buf, &method, &uri, &vsn, &hdrs)) {
				if (validate_request(connfd, method, uri, vsn, hdrs))
					handle_request(connfd, &saddr, method, uri);
			} else { // parsing failed
				const char *resp = RESP_400 END_HDRS;
				TRY(write(connfd, resp, strlen(resp)));
			}

			TRY(shutdown(connfd, SHUT_RDWR));
			TRY(close(connfd));
			TRY(close(staticdirfd));
			return 0;
		}
	}

	TRY(munmap(indexcount, sizeof(*indexcount)));
	TRY(close(sockfd));
	TRY(close(staticdirfd));
}
