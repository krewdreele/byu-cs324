#include <stdio.h>
#include <string.h>
#include "sockhelper.h"
#include <fcntl.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <errno.h>
#include <arpa/inet.h>


/* Recommended max object size */
#define MAX_OBJECT_SIZE 102400
#define READ_REQUEST 1
#define SEND_REQUEST 2
#define READ_RESPONSE 3
#define SEND_RESPONSE 4
#define MAXEVENTS 64

struct request_info {
	int client_proxy_socket;
	int proxy_server_socket;
	int state;
	char read_client[1024];
	char read_server[16384];
	int bytes_read_client;
	int bytes_to_write_server;
	int bytes_written_server;
	int bytes_read_server;
	int bytes_written_client;
};

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:97.0) Gecko/20100101 Firefox/97.0";

int complete_request_received(char *);
void parse_request(char *, char *, char *, char *, char *);
void test_parser();
void print_bytes(char *, int);
int open_sfd(char **);
void handle_new_clients(int, int);
int connect_to_server(const char *, const char *);
void handle_client(int, struct request_info *);

int main(int argc, char *argv[])
{
	printf("%s\n", user_agent_hdr);

	int efd;
	if ((efd = epoll_create1(0)) < 0)
	{
		perror("Error with epoll_create1");
		exit(EXIT_FAILURE);
	}

	int sfd = open_sfd(argv);

	// allocate memory for a new struct client_info, and populate it with
	// info for the listening socket
	struct request_info *listener =
		malloc(sizeof(struct request_info));
	listener->client_proxy_socket = sfd;
	listener->proxy_server_socket = -1;
	listener->state = READ_REQUEST;
	memset(listener->read_client, 0, sizeof(listener->read_client));
	memset(listener->read_server, 0, sizeof(listener->read_server));
	listener->bytes_read_client = 0;
	listener->bytes_to_write_server = 0;
	listener->bytes_written_server = 0;
	listener->bytes_read_server = 0;
	listener->bytes_written_client = 0;

	// register the listening file descriptor for incoming events using
	// edge-triggered monitoring
	struct epoll_event event;
	event.data.ptr = listener;
	event.events = EPOLLIN | EPOLLET;
	if (epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &event) < 0)
	{
		perror("error adding event\n");
		exit(EXIT_FAILURE);
	}

	struct epoll_event events[MAXEVENTS];

	while(1){
		int n = epoll_wait(efd, events, MAXEVENTS, -1);
		if (n < 0)
		{
			perror("epoll wait");
			exit(EXIT_FAILURE);
		}
		for (int i = 0; i < n; i++)
		{
			struct request_info *info = (struct request_info *)events[i].data.ptr;
			if (info->client_proxy_socket == sfd)
			{
				handle_new_clients(efd, sfd);
			}
			else
			{
				handle_client(efd, info);
			}
		}
	}

	return 0;
}

int complete_request_received(char *request)
{
	char *eof = strstr(request, "\r\n\r\n");
	if (eof == NULL)
	{
		return 0;
	}
	else
	{
		return 1;
	}
}

void parse_request(char *request, char *method, char *hostname, char *port, char *path)
{
	// Ensure buffers are empty
	method[0] = hostname[0] = port[0] = path[0] = '\0';

	// Verify the request is not NULL
	if (request == NULL)
	{
		perror("Error: Null request received.\n");
		return;
	}

	// Parse the method (e.g., "GET")
	const char *method_end = strchr(request, ' ');
	if (method_end == NULL)
	{
		perror("Error: Malformed request (missing method).\n");
		return;
	}
	size_t method_len = method_end - request;
	if (method_len >= 16)
	{
		perror("Error: Method too long.\n");
		return;
	}
	strncpy(method, request, method_len);
	method[method_len] = '\0';

	// Parse the URL (e.g., "http://www.example.com:8080/path")
	const char *url_start = method_end + 1;
	const char *url_end = strchr(url_start, ' ');
	if (url_end == NULL)
	{
		perror("Error: Bad request (missing URL).\n");
		return;
	}
	size_t url_len = url_end - url_start;
	char url[128];
	if (url_len >= sizeof(url))
	{
		perror("Error: URL too long.\n");
		return;
	}
	strncpy(url, url_start, url_len);
	url[url_len] = '\0';

	// Extract the hostname, port, and path from the URL
	const char *host_start = strstr(url, "://");
	if (host_start != NULL)
	{
		host_start += 3; // Skip "://"
	}
	else
	{
		host_start = url; // Assume no scheme (e.g., "www.example.com")
	}

	const char *port_start = strchr(host_start, ':');
	const char *path_start = strchr(host_start, '/');
	if (port_start != NULL && (path_start == NULL || port_start < path_start))
	{
		// Extract hostname up to ':'
		size_t host_len = port_start - host_start;
		if (host_len >= 64)
		{
			perror("Error: Hostname too long.\n");
			return;
		}
		strncpy(hostname, host_start, host_len);
		hostname[host_len] = '\0';

		// Extract port
		const char *port_end = (path_start != NULL) ? path_start : url + url_len;
		size_t port_len = port_end - (port_start + 1);
		if (port_len >= 8)
		{
			perror("Error: Port too long.\n");
			return;
		}
		strncpy(port, port_start + 1, port_len);
		port[port_len] = '\0';
	}
	else
	{
		// Extract hostname up to '/'
		size_t host_len = (path_start != NULL) ? path_start - host_start : url + url_len - host_start;
		if (host_len >= 64)
		{
			perror("Error: Hostname too long.\n");
			return;
		}
		strncpy(hostname, host_start, host_len);
		hostname[host_len] = '\0';

		// Default to port 80 if none is specified
		strncpy(port, "80", 3);
	}

	// Extract path
	if (path_start != NULL)
	{
		size_t path_len = url + url_len - path_start;
		if (path_len >= 64)
		{
			perror("Error: Path too long.\n");
			return;
		}
		strncpy(path, path_start, path_len);
		path[path_len] = '\0';
	}
	else
	{
		// Default to "/"
		strncpy(path, "/", 2);
	}
}

int open_sfd(char **argv)
{
	int sfd;
	if ((sfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		perror("Error creating socket");
		exit(EXIT_FAILURE);
	}

	int optval = 1;
	setsockopt(sfd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

	// set listening file descriptor nonblocking
	if (fcntl(sfd, F_SETFL, fcntl(sfd, F_GETFL, 0) | O_NONBLOCK) < 0)
	{
		perror("error setting socket option\n");
		exit(1);
	}

	struct sockaddr_storage local_addr_ss;
	struct sockaddr *local_addr = (struct sockaddr *)&local_addr_ss;

	// Populate local_addr with the port using populate_sockaddr().
	populate_sockaddr(local_addr, AF_INET, NULL, atoi(argv[1]));
	if (bind(sfd, local_addr, sizeof(struct sockaddr_storage)) < 0)
	{
		perror("Could not bind");
		exit(EXIT_FAILURE);
	}

	if (listen(sfd, SOMAXCONN) < 0)
	{
		perror("Could not listen");
		exit(EXIT_FAILURE);
	}

	return sfd;
}

void handle_new_clients(int efd, int sfd) {
	while (1)
	{
		// Declare structures for remote address and port.
		// See notes above for local_addr_ss and local_addr_ss.
		struct sockaddr_storage remote_addr_ss;
		struct sockaddr *remote_addr = (struct sockaddr *)&remote_addr_ss;
		char remote_ip[INET6_ADDRSTRLEN];
		unsigned short remote_port;

		socklen_t addr_len = sizeof(struct sockaddr_storage);
		int connfd = accept(sfd, remote_addr, &addr_len);

		if (connfd < 0)
		{
			if (errno == EWOULDBLOCK ||
				errno == EAGAIN)
			{
				// no more clients ready to accept
				break;
			}
			else
			{
				perror("accept");
				exit(EXIT_FAILURE);
			}
		}

		parse_sockaddr(remote_addr, remote_ip, &remote_port);
		printf("Connection from %s:%d\n",
			   remote_ip, remote_port);

		// set client file descriptor nonblocking
		if (fcntl(connfd, F_SETFL, fcntl(connfd, F_GETFL, 0) | O_NONBLOCK) < 0)
		{
			perror("error setting socket option\n");
			exit(1);
		}

		// allocate memory for a new struct
		// request_info, and populate it with
		// info for the new client
		struct request_info *new_client =
			(struct request_info *)malloc(sizeof(struct request_info));
		new_client->client_proxy_socket = connfd;
		new_client->proxy_server_socket = -1;
		new_client->state = READ_REQUEST;
		memset(new_client->read_client, 0, sizeof(new_client->read_client));
		memset(new_client->read_server, 0, sizeof(new_client->read_server));
		new_client->bytes_read_client = 0;
		new_client->bytes_to_write_server = 0;
		new_client->bytes_written_server = 0;
		new_client->bytes_read_server = 0;
		new_client->bytes_written_client = 0;

		printf("Client fd: (%d)\n", new_client->client_proxy_socket);

		// register the client file descriptor
		// for incoming events using
		// edge-triggered monitoring
		struct epoll_event event;
		event.data.ptr = new_client;
		event.events = EPOLLIN | EPOLLET;
		if (epoll_ctl(efd, EPOLL_CTL_ADD, connfd, &event) < 0)
		{
			perror("Error adding client fd to epoll\n");
			exit(1);
		}
	}
}

void handle_client(int efd, struct request_info *info)
{
	printf("Client file descriptor: %d\n", info->client_proxy_socket);
	printf("Server file descriptor: %d\n", info->proxy_server_socket);
	printf("State: %d\n", info->state);

	if(info->state == READ_REQUEST){
		while(1){
			// Read bytes from client
			int bytes_read = read(info->client_proxy_socket, info->read_client + info->bytes_read_client, 1024 - info->bytes_read_client);

			if(bytes_read > 0){
				// Append to info
				info->bytes_read_client = info->bytes_read_client + bytes_read;
				info->read_client[info->bytes_read_client] = '\0';
			}

			// If we have a full request
			if(complete_request_received(info->read_client)){

				// Parse the request
				char method[16], hostname[64], port[8], path[64];

				parse_request(info->read_client, method, hostname, port, path);

				printf("METHOD: %s\n", method);
				printf("HOSTNAME: %s\n", hostname);
				printf("PORT: %s\n", port);
				printf("PATH: %s\n", path);
			
				// Build http request for server
				char request[1024];
				if (strcmp(port, "80") == 0)
				{
					snprintf(request, sizeof(request),
							"%s %s HTTP/1.0\r\n"
							"Host: %s\r\n"
							"User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:97.0) Gecko/20100101 Firefox/97.0\r\n"
							"Connection: close\r\n"
							"Proxy-Connection: close\r\n\r\n",
							method, path, hostname);
				}
				else
				{
					snprintf(request, sizeof(request),
							"%s %s HTTP/1.0\r\n"
							"Host: %s:%s\r\n"
							"User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:97.0) Gecko/20100101 Firefox/97.0\r\n"
							"Connection: close\r\n"
							"Proxy-Connection: close\r\n\r\n",
							method, path, hostname, port);
				}

				// Connect to the server
				int server_fd = connect_to_server(hostname, port);
				if (server_fd < 0)
				{
					perror("Error: Failed to connect to server.\n");
					close(info->client_proxy_socket);
					return;
				}

				// Set client file descriptor nonblocking
				if (fcntl(server_fd, F_SETFL, fcntl(server_fd, F_GETFL, 0) | O_NONBLOCK) < 0)
				{
					perror("Error setting socket option\n");
					exit(1);
				}

				// Deregister client - proxy file descriptor
				if (epoll_ctl(efd, EPOLL_CTL_DEL, info->client_proxy_socket, NULL) == -1)
				{
					perror("Error deregistering client");
					exit(1);
				}

				// register the server file descriptor
				// for outgoing events using
				struct epoll_event event;
				info->proxy_server_socket = server_fd;
				info->state = SEND_REQUEST;
				info->bytes_to_write_server = sizeof(request);
				event.data.ptr = info;
				event.events = EPOLLOUT;
				if (epoll_ctl(efd, EPOLL_CTL_ADD, server_fd, &event) < 0)
				{
					perror("Error adding server fd to epoll\n");
					exit(1);
				}

				return;
			}

			if (bytes_read < 0)
			{
				if (errno == EWOULDBLOCK || errno == EAGAIN)
				{
					// nothing to be read
					return;
				}
				else
				{
					perror("Error reading from the client");
					close(info->client_proxy_socket);
					free(info);
					exit(1);
				}
			}
		}
	}
	else if(info->state == SEND_REQUEST){

		while(1){
			// Write request to the server
			int bytes_sent = write(info->proxy_server_socket, info->read_client + info->bytes_written_server, info->bytes_to_write_server - info->bytes_written_server);

			if (bytes_sent > 0)
			{
				info->bytes_written_server += bytes_sent;
			}
				if (info->bytes_written_server >= info->bytes_to_write_server)
				{
					// Deregister proxy - server file descriptor
					if (epoll_ctl(efd, EPOLL_CTL_DEL, info->proxy_server_socket, NULL) == -1)
					{
						perror("Error deregistering client");
						exit(1);
					}

					// register the server file descriptor
					// for reading events
					struct epoll_event event;
					event.data.ptr = info;
					event.events = EPOLLIN | EPOLLET;
					if (epoll_ctl(efd, EPOLL_CTL_ADD, info->proxy_server_socket, &event) < 0)
					{
						perror("Error adding server fd to epoll\n");
						exit(1);
					}

					info->state = READ_RESPONSE;

					return;
				}

				if (bytes_sent < 0)
				{
					if (errno == EWOULDBLOCK || errno == EAGAIN)
					{
						// nothing to be read
						return;
					}
					else
					{
						perror("Error sending to the server");
						close(info->client_proxy_socket);
						free(info);
						exit(1);
					}
				}
			}
	}
	else if(info->state == READ_RESPONSE){
		while(1){
			// Read bytes from the server
			int bytes_read = read(info->proxy_server_socket, info->read_server + info->bytes_read_server, sizeof(info->read_server) - info->bytes_read_server);
			
			if(bytes_read > 0){
				info->bytes_read_server += bytes_read;
			}
			// Successfully finished
			if(bytes_read == 0){
				close(info->proxy_server_socket);

				print_bytes(info->read_server, info->bytes_read_server);

				// Register client proxy fd for writing
				struct epoll_event event;
				info->state = SEND_RESPONSE;
				event.data.ptr = info;
				event.events = EPOLLOUT;
				if (epoll_ctl(efd, EPOLL_CTL_ADD, info->client_proxy_socket, &event) < 0)
				{
					perror("Error adding server fd to epoll\n");
					exit(1);
				}

				return;
			}
			if (bytes_read < 0)
			{
				if (errno == EWOULDBLOCK || errno == EAGAIN)
				{
					// nothing to be read
					return;
				}
				else
				{
					perror("Error reading from the server");
					close(info->client_proxy_socket);
					free(info);
					exit(1);
				}
			}
		}
	}
	else if(info->state == SEND_RESPONSE){
		while(1){
			int bytes_sent = write(info->client_proxy_socket, info->read_server + info->bytes_written_client, sizeof(info->read_server) - info->bytes_written_client);

			if (bytes_sent > 0)
			{
				info->bytes_written_client += bytes_sent;
			}

			if (info->bytes_written_client >= info->bytes_read_server)
			{
				close(info->client_proxy_socket);
				free(info);
				return;
			}

			if (bytes_sent < 0)
			{
				if (errno == EWOULDBLOCK || errno == EAGAIN)
				{
					// nothing to be read
					return;
				}
				else
				{
					perror("Error sending to the server");
					close(info->client_proxy_socket);
					free(info);
					exit(1);
				}
			}
			}
	}
}

int connect_to_server(const char *hostname, const char *port)
{
	struct addrinfo hints, *res, *p;
	int server_fd;

	// Zero out the hints structure
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;		 // Use IPv4
	hints.ai_socktype = SOCK_STREAM; // Use TCP

	// Resolve the hostname and port into address information
	int status = getaddrinfo(hostname, port, &hints, &res);
	if (status != 0)
	{
		perror("getaddrinfo error");
		return -1;
	}

	// Loop through the results and connect to the first valid one
	for (p = res; p != NULL; p = p->ai_next)
	{
		// Create the socket
		server_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (server_fd == -1)
		{
			perror("socket");
			continue; // Try the next address
		}

		// Attempt to connect to the server
		if (connect(server_fd, p->ai_addr, p->ai_addrlen) == -1)
		{
			perror("connect");
			close(server_fd); // Clean up and try the next address
			continue;
		}

		break; // Successfully connected
	}

	// Free the address information
	freeaddrinfo(res);

	// Check if no valid address was found
	if (p == NULL)
	{
		perror("Error: Could not connect to server.\n");
		return -1;
	}

	// Return the file descriptor for the connected socket
	return server_fd;
}

void test_parser() {
	int i;
	char method[16], hostname[64], port[8], path[64];

       	char *reqs[] = {
		"GET http://www.example.com/index.html HTTP/1.0\r\n"
		"Host: www.example.com\r\n"
		"User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:68.0) Gecko/20100101 Firefox/68.0\r\n"
		"Accept-Language: en-US,en;q=0.5\r\n\r\n",

		"GET http://www.example.com:8080/index.html?foo=1&bar=2 HTTP/1.0\r\n"
		"Host: www.example.com:8080\r\n"
		"User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:68.0) Gecko/20100101 Firefox/68.0\r\n"
		"Accept-Language: en-US,en;q=0.5\r\n\r\n",

		"GET http://localhost:1234/home.html HTTP/1.0\r\n"
		"Host: localhost:1234\r\n"
		"User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:68.0) Gecko/20100101 Firefox/68.0\r\n"
		"Accept-Language: en-US,en;q=0.5\r\n\r\n",

		"GET http://www.example.com:8080/index.html HTTP/1.0\r\n",

		NULL
	};
	
	for (i = 0; reqs[i] != NULL; i++) {
		printf("Testing %s\n", reqs[i]);
		if (complete_request_received(reqs[i])) {
			printf("REQUEST COMPLETE\n");
			parse_request(reqs[i], method, hostname, port, path);
			printf("METHOD: %s\n", method);
			printf("HOSTNAME: %s\n", hostname);
			printf("PORT: %s\n", port);
			printf("PATH: %s\n", path);
		} else {
			printf("REQUEST INCOMPLETE\n");
		}
	}
}

void print_bytes(char *bytes, int byteslen) {
	int i, j, byteslen_adjusted;

	if (byteslen % 8) {
		byteslen_adjusted = ((byteslen / 8) + 1) * 8;
	} else {
		byteslen_adjusted = byteslen;
	}
	for (i = 0; i < byteslen_adjusted + 1; i++) {
		if (!(i % 8)) {
			if (i > 0) {
				for (j = i - 8; j < i; j++) {
					if (j >= byteslen_adjusted) {
						printf("  ");
					} else if (j >= byteslen) {
						printf("  ");
					} else if (bytes[j] >= '!' && bytes[j] <= '~') {
						printf(" %c", bytes[j]);
					} else {
						printf(" .");
					}
				}
			}
			if (i < byteslen_adjusted) {
				printf("\n%02X: ", i);
			}
		} else if (!(i % 4)) {
			printf(" ");
		}
		if (i >= byteslen_adjusted) {
			continue;
		} else if (i >= byteslen) {
			printf("   ");
		} else {
			printf("%02X ", bytes[i]);
		}
	}
	printf("\n");
	fflush(stdout);
}
