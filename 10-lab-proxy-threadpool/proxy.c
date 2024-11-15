#include <stdio.h>
#include <string.h>

#include "sockhelper.h"

/* Recommended max object size */
#define MAX_OBJECT_SIZE 102400

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:97.0) Gecko/20100101 Firefox/97.0";

int complete_request_received(char *);
void parse_request(char *, char *, char *, char *, char *);
void test_parser();
void print_bytes(unsigned char *, int);


int main(int argc, char *argv[])
{
	test_parser();
	printf("%s\n", user_agent_hdr);
	return 0;
}

int complete_request_received(char *request) {
	char * eof = strstr(request, "\r\n\r\n");
	if(eof == NULL){
		return 0;
	}
	else{
		return 1;
	}

}

void parse_request(char *request, char *method,
		char *hostname, char *port, char *path) {

	int numSpaces = 0;
	int index1 = 0;
	char url[50];

	for(int i=0; i < strlen(request); i++){
		if(request[i] == ' ' && numSpaces == 0){
			numSpaces++;
			index1 = i;
			memcpy(method, &request[0], index1);
			method[index1] = '\0';
			index1++;
		}
		else if (request[i] == ' ' && numSpaces == 1){
			numSpaces++;
			int length = i - index1 - 7;
			memcpy(url, &request[index1+7], length);
			url[length] = '\0';
		}
	}

	char *colon = strstr(url, ":");
	char *slash = strstr(url, "/");

	int portlen = 0;
	int hostlen = 0;

	if(colon != NULL){
		portlen = slash - colon - 1;
		memcpy(port, colon + 1, portlen);

		hostlen = colon - url;
		memcpy(hostname, &url[0], hostlen);
	}
	else{
		portlen = 2;
		memcpy(port, &"80", portlen);

		hostlen = slash - url;
		memcpy(hostname, &url[0], hostlen);
	}

	int pathlen = strlen(slash);
	memcpy(path, slash, pathlen);

	path[pathlen] = '\0';
	port[portlen] = '\0';
	hostname[hostlen] = '\0';

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

void print_bytes(unsigned char *bytes, int byteslen) {
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
