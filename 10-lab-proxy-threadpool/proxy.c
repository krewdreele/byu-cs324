#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "sockhelper.h"
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

/* Recommended max object size */
#define MAX_OBJECT_SIZE 102400

	static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:97.0) Gecko/20100101 Firefox/97.0";

int complete_request_received(char *);
void parse_request(char *, char *, char *, char *, char *);
void test_parser();
void print_bytes(char *, int);
int open_sfd(char **);
void handle_client(int);
int connect_to_server(const char *, const char *);

int main(int argc, char *argv[])
{
	printf("%s\n", user_agent_hdr);
	int sfd = open_sfd(argv);
	struct sockaddr_storage remote_addr_ss;
	struct sockaddr *remote_addr = (struct sockaddr *)&remote_addr_ss;
	socklen_t addr_len = sizeof(struct sockaddr_storage);
	int fd = 0;
	while(1){
		
		fd = accept(sfd, remote_addr, &addr_len);
		
		if(fd > 0) handle_client(fd);
	}
	return 0;
}

void handle_client(int client_fd)
{
	char buf[1024];
	ssize_t nread = 0;
	int total_read = 0;

	// Step 1: Read the full HTTP request from the client
	do
	{
		if (total_read >= sizeof(buf))
		{
			fprintf(stderr, "Error: Request buffer overflow.\n");
			close(client_fd);
			return;
		}

		nread = recv(client_fd, buf + total_read, sizeof(buf) - total_read, 0);

		if (nread > 0)
		{
			total_read += nread;
			buf[total_read] = '\0'; // Null-terminate for safety
		}
		else if (nread < 0)
		{
			perror("recv");
			close(client_fd);
			return;
		}

		if (complete_request_received(buf))
		{
			break;
		}
	} while (nread > 0);

	// Step 2: Parse the HTTP request
	char method[16], hostname[64], port[8], path[128];
	parse_request(buf, method, hostname, port, path);

	printf("METHOD: %s\n", method);
	printf("HOSTNAME: %s\n", hostname);
	printf("PORT: %s\n", port);
	printf("PATH: %s\n", path);

	// Step 3: Create a new HTTP request to send to the server
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

	// Step 4: Connect to the HTTP server
	int server_fd = connect_to_server(hostname, port);
	if (server_fd < 0)
	{
		fprintf(stderr, "Error: Failed to connect to server.\n");
		close(client_fd);
		return;
	}

	// Step 5: Send the HTTP request to the server
	size_t total_sent = 0;
	ssize_t nwritten;
	size_t request_len = strlen(request);

	while (total_sent < request_len)
	{
		nwritten = send(server_fd, request + total_sent, request_len - total_sent, 0);

		if (nwritten < 0)
		{
			perror("send");
			close(server_fd);
			close(client_fd);
			return;
		}

		total_sent += nwritten;
	}

	// Step 6: Receive the HTTP response from the server
	char response[16384];
	int total_response = 0;

	do
	{
		if (total_response >= sizeof(response))
		{
			fprintf(stderr, "Error: Response buffer overflow.\n");
			close(server_fd);
			close(client_fd);
			return;
		}

		nread = recv(server_fd, response + total_response, sizeof(response) - total_response, 0);

		if (nread > 0)
		{
			total_response += nread;
		}
		else if (nread < 0)
		{
			perror("recv");
			close(server_fd);
			close(client_fd);
			return;
		}
	} while (nread > 0);

	// Step 7: Send the HTTP response back to the client
	total_sent = 0;

	while (total_sent < total_response)
	{
		nwritten = send(client_fd, response + total_sent, total_response - total_sent, 0);

		if (nwritten < 0)
		{
			perror("send");
			close(server_fd);
			close(client_fd);
			return;
		}

		total_sent += nwritten;
	}

	// Step 8: Close connections
	close(server_fd);
	close(client_fd);
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
		fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
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
		fprintf(stderr, "Error: Could not connect to server.\n");
		return -1;
	}

	// Return the file descriptor for the connected socket
	return server_fd;
}

int open_sfd(char **argv){
	int sfd;
	if ((sfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		perror("Error creating socket");
		exit(EXIT_FAILURE);
	}

	int optval = 1;
	setsockopt(sfd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

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

int complete_request_received(char *request) {
	char *eof = strstr(request, "\r\n\r\n");
	if(eof == NULL){
		return 0;
	}
	else{
		return 1;
	}

}

void parse_request( char *request, char *method, char *hostname, char *port, char *path)
{
	// Ensure buffers are empty
	method[0] = hostname[0] = port[0] = path[0] = '\0';

	// Verify the request is not NULL
	if (request == NULL)
	{
		fprintf(stderr, "Error: Null request received.\n");
		return;
	}

	// Parse the method (e.g., "GET")
	const char *method_end = strchr(request, ' ');
	if (method_end == NULL)
	{
		fprintf(stderr, "Error: Malformed request (missing method).\n");
		return;
	}
	size_t method_len = method_end - request;
	if (method_len >= 16)
	{
		fprintf(stderr, "Error: Method too long.\n");
		return;
	}
	strncpy(method, request, method_len);
	method[method_len] = '\0';

	// Parse the URL (e.g., "http://www.example.com:8080/path")
	const char *url_start = method_end + 1;
	const char *url_end = strchr(url_start, ' ');
	if (url_end == NULL)
	{
		fprintf(stderr, "Error: Malformed request (missing URL).\n");
		return;
	}
	size_t url_len = url_end - url_start;
	char url[128];
	if (url_len >= sizeof(url))
	{
		fprintf(stderr, "Error: URL too long.\n");
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
			fprintf(stderr, "Error: Hostname too long.\n");
			return;
		}
		strncpy(hostname, host_start, host_len);
		hostname[host_len] = '\0';

		// Extract port
		const char *port_end = (path_start != NULL) ? path_start : url + url_len;
		size_t port_len = port_end - (port_start + 1);
		if (port_len >= 8)
		{
			fprintf(stderr, "Error: Port too long.\n");
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
			fprintf(stderr, "Error: Hostname too long.\n");
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
			fprintf(stderr, "Error: Path too long.\n");
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
