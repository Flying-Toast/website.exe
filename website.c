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

void handle_request(int fd)
{
	write(fd, "Hello", 5);
}

bool validate_request(int fd, char *method, char *uri, char *vsn, char *hdrs)
{
	// we only do GETs for now
	if (strcmp(method, "GET")) {
		const char resp[] = "HTTP/1.0 405 Method Not Allowed\r\nAllow: GET\r\n";
		write(fd, resp, strlen(resp));
		return false;
	}

	if (strcmp(vsn, "HTTP/1.0") && strcmp(vsn, "HTTP/1.1")) {
		const char resp[] = "HTTP/1.0 400 Bad Request\r\n";
		write(fd, resp, strlen(resp));
		return false;
	}

	return true;
}

bool parse_request(char *req, char **method_out, char **uri_out, char **vsn_out, char **hdrs_out)
{
	// extract method
	*method_out = req;
	while (*req != ' ') {
		if (*req++ == '\0')
			return false;
	}
	*req = '\0';

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
	if (*++req != '\n' || *++req == '\0')
		return false;
	*hdrs_out = req;
	while (strncmp(req, "\r\n\r\n", 4)) {
		if (*req == '\0')
			return false;
		req++;
	}
	*req = '\0';

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
			char *method, *uri, *vsn, *hdrs;

			int nread = read(connfd, buf, BUFLEN - 1); // -1 to keep a NUL at the end
			if (nread == -1) {
				perror("read");
			} else if (nread == BUFLEN - 1) {
				fputs("Request too long - aborting", stderr);
			} else if (parse_request(buf, &method, &uri, &vsn, &hdrs) && validate_request(connfd, method, uri, vsn, hdrs)) {
				handle_request(connfd);
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
