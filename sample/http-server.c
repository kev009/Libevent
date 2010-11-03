/*
  A trivial static http webserver.

 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/socket.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include <event2/event.h>
#include <event2/http.h>
#include <event2/buffer.h>
#include <event2/util.h>

static void
send_document_cb(struct evhttp_request *req, void *arg)
{
	struct evbuffer *evb;
	const char *docroot = arg;
	const char *uri = evhttp_request_get_uri(req);
	struct evhttp_uri *decoded = NULL;
	const char *path;
	char *decoded_path;
	char *whole_path = NULL;
	size_t len;
	int fd = -1;
	struct stat st;

	printf("Got request for <%s>\n",  uri);

	if (evhttp_request_get_command(req) != EVHTTP_REQ_GET) {
		printf("It wasn't a GET request. Sending ???\n");
		evhttp_send_error(req, 404, "Not the right error");
		return;
	}

	/* Decode the URI */
	decoded = evhttp_uri_parse(uri);
	if (!decoded) {
		printf("It's not a good URI. Sending ???\n");
		evhttp_send_error(req, 404, "Not the right error");
		return;
	}

	path = evhttp_uri_get_path(decoded);
	if (!path) path = "";

	decoded_path = evhttp_uridecode(path, 0, NULL);

	if (strstr(decoded_path, "..")) /*XXX overzealous */
		goto err;

	len = strlen(decoded_path)+strlen(docroot)+2;
	whole_path = malloc(len);
	snprintf(whole_path, len, "%s/%s", docroot, decoded_path);

	fd = open(whole_path, O_RDONLY);
	if (fd<0)
		goto err;

	if (fstat(fd, &st)<0)
		goto err;

	evb = evbuffer_new();
	evbuffer_add_file(evb, fd, 0, st.st_size);

	evhttp_send_reply(req, 200, "OK", evb);
	evbuffer_free(evb);

	return;
err:
	evhttp_send_error(req, 404, "Document was not found");
	if (fd>=0)
		close(fd);
	if (decoded)
		evhttp_uri_free(decoded);
	if (decoded_path)
		free(decoded_path);
	if (whole_path)
		free(whole_path);
}

static void
syntax(void)
{
	fprintf(stdout, "Syntax: http-server <docroot>\n");
}

int
main(int argc, char **argv)
{
	struct event_base *base;
	struct evhttp *http;
	struct evhttp_bound_socket *handle;

	unsigned short port = 0;
#ifdef WIN32
	WSADATA WSAData;
	WSAStartup(0x101, &WSAData);
#else
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
		return (1);
#endif

	base = event_base_new();
	if (!base) {
		fprintf(stderr, "Couldn't create an event_base: exiting\n");
		return 1;
	}

	if (argc < 2) {
		syntax();
		return 1;
	}

	http = evhttp_new(base);
	if (!http) {
		fprintf(stderr, "couldn't create evhttp. Exiting.\n");
		return 1;
	}

	evhttp_set_gencb(http, send_document_cb, argv[1]);

	handle = evhttp_bind_socket_with_handle(http, "0.0.0.0", port);
	if (!handle) {
		fprintf(stderr, "couldn't bind to port %d. Exiting.\n",
		    (int)port);
		return 1;
	}

	{
		struct sockaddr_storage ss;
		evutil_socket_t fd;
		ev_socklen_t socklen = sizeof(ss);
		char addrbuf[128];
		void *inaddr;
		const char *addr;
		int got_port = -1;
		fd = evhttp_bound_socket_get_fd(handle);
		memset(&ss, 0, sizeof(ss));
		if (getsockname(fd, (struct sockaddr *)&ss, &socklen)) {
			perror("getsockname() failed");
			return 1;
		}
		if (ss.ss_family == AF_INET) {
			got_port = ntohs(((struct sockaddr_in*)&ss)->sin_port);
			inaddr = &((struct sockaddr_in*)&ss)->sin_addr;
		} else if (ss.ss_family == AF_INET6) {
			got_port = ntohs(((struct sockaddr_in6*)&ss)->sin6_port);
			inaddr = &((struct sockaddr_in6*)&ss)->sin6_addr;
		} else {
			fprintf(stderr, "Weird address family %d\n",
			    ss.ss_family);
			return 1;
		}
		addr = evutil_inet_ntop(ss.ss_family, inaddr, addrbuf,
		    sizeof(addrbuf));
		if (addr) {
			printf("Listening on %s:%d\n", addr, got_port);
		} else {
			fprintf(stderr, "evutil_inet_ntop failed\n");
			return 1;
		}
	}

	event_base_dispatch(base);

	return 0;
}
