#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#define BIND_PORT 8000
#define BUFLEN 1024

#define RESP_200 "HTTP/1.0 200 OK\r\n"
#define RESP_400 "HTTP/1.0 400 Bad Request\r\n"
#define RESP_404 "HTTP/1.0 404 Not Found\r\n"
#define RESP_405 "HTTP/1.0 405 Method Not Allowed\r\n"
#define RESP_505 "HTTP/1.0 505 HTTP Version Not Supported\r\n"

#define CONTENT_TYPE_HTML "Content-Type: text/html\r\n"
#define CONTENT_TYPE_PLAINTEXT "Content-Type: text/plain\r\n"
#define END_HDRS "\r\n"

#define ARRAY_LEN(arr) (sizeof(arr) / sizeof((arr)[0]))

static const char *src_lines[] = {
#include "quinelines.gen"
};

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

void handle_request(int fd, enum method method, char *uri)
{
	if (!strcmp(uri, "/")) {
		static const char resp[] =
			RESP_200
			CONTENT_TYPE_HTML
			END_HDRS
			#include "pages/index.string.gen"
		;
		write(fd, resp, strlen(resp));
	} else if (!strcmp(uri, "/quine.c")) {
		static const char resp[] = RESP_200 CONTENT_TYPE_PLAINTEXT END_HDRS;
		write(fd, resp, strlen(resp));
		write_quine(fd, true);
	} else if (!strcmp(uri, "/website.c")) {
		static const char resp[] = RESP_200 CONTENT_TYPE_PLAINTEXT END_HDRS;
		write(fd, resp, strlen(resp));
		write_quine(fd, false);
	} else {
		static const char resp[] =
			RESP_404
			CONTENT_TYPE_HTML
			END_HDRS
			#include "pages/404_page.string.gen"
		;
		write(fd, resp, strlen(resp));
	}
}

bool validate_request(int fd, enum method method, char *uri, char *vsn, char *hdrs)
{
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

int main(void)
{
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
	bind_addr.sin_port = htons(BIND_PORT);
	bind_addr.sin_addr.s_addr = INADDR_ANY;
	if (bind(sockfd, (struct sockaddr *) &bind_addr, sizeof(bind_addr))) {
		perror("bind");
		return 1;
	}

	if (listen(sockfd, 5)) {
		perror("listen");
		return 1;
	}

	for (;;) {
		int connfd = accept(sockfd, NULL, NULL);
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
				fputs("Request too long - aborting", stderr);
			} else if (parse_request(buf, &method, &uri, &vsn, &hdrs)) {
				if (validate_request(connfd, method, uri, vsn, hdrs))
					handle_request(connfd, method, uri);
			} else { // parsing failed
				static const char resp[] = RESP_400;
				write(connfd, resp, strlen(resp));
			}

			if (shutdown(connfd, SHUT_RDWR))
				perror("shutdown");
			if (close(connfd))
				perror("close");
			return 0;
		}
	}

	close(sockfd);
}
