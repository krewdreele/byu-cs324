// Replace PUT_USERID_HERE with your actual BYU CS user id, which you can find
// by running `id -u` on a CS lab machine.
#define USERID 1823731858

#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "sockhelper.h"

int verbose = 0;

void print_bytes(unsigned char *bytes, int byteslen);

int main(int argc, char *argv[])
{
	int opt;
	while ((opt = getopt(argc, argv, "v")) != -1)
	{
		switch (opt)
		{
		case 'v':
			verbose = 1;
			break;
		default:
			fprintf(stderr, "Usage: %s [-v] server port level seed\n", argv[0]);
			exit(EXIT_FAILURE);
		}
	}

	// Check for remaining required arguments after option parsing
	if (argc - optind < 4)
	{
		fprintf(stderr, "Usage: %s [-v] server port level seed\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	char *server = argv[optind];
	char *port = argv[optind + 1];
	int port_num = atoi(port);
	int level = atoi(argv[optind + 2]);
	int seed = atoi(argv[optind + 3]);
	unsigned char buf[64];

	// Message setup
	bzero(buf, 64);
	memcpy(&buf[1], &level, 1);

	unsigned int val = htonl(USERID);
	memcpy(&buf[2], &val, 4);

	unsigned short val2 = htons(seed);
	memcpy(&buf[6], &val2, 2);

	if (verbose)
	{
		print_bytes(buf, 8);
	}

	// UDP setup
	struct addrinfo hints;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;

	struct addrinfo *result;
	int s;
	s = getaddrinfo(server, port, &hints, &result);
	if (s != 0)
	{
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
		exit(EXIT_FAILURE);
	}

	int sfd;
	int addr_fam;
	socklen_t addr_len;
	struct sockaddr_storage local_addr_ss;
	struct sockaddr *local_addr = (struct sockaddr *)&local_addr_ss;
	char local_ip[INET6_ADDRSTRLEN];
	unsigned short local_port;
	struct sockaddr_storage remote_addr_ss;
	struct sockaddr *remote_addr = (struct sockaddr *)&remote_addr_ss;
	char remote_ip[INET6_ADDRSTRLEN];
	unsigned short remote_port;

	struct addrinfo *rp;
	for (rp = result; rp != NULL; rp = rp->ai_next)
	{
		sfd = socket(rp->ai_family, rp->ai_socktype, 0);
		if (sfd < 0)
		{
			continue;
		}

		addr_fam = rp->ai_family;
		addr_len = rp->ai_addrlen;
		memcpy(remote_addr, rp->ai_addr, sizeof(struct sockaddr_storage));
		parse_sockaddr(remote_addr, remote_ip, &remote_port);

		if (verbose)
		{
			fprintf(stderr, "Connecting to %s:%d (addr family: %d)\n", remote_ip, remote_port, addr_fam);
		}

		break;

		close(sfd);
	}

	if (rp == NULL)
	{
		fprintf(stderr, "Could not connect\n");
		exit(EXIT_FAILURE);
	}

	freeaddrinfo(result);
	addr_len = sizeof(struct sockaddr_storage);
	s = getsockname(sfd, local_addr, &addr_len);
	parse_sockaddr(local_addr, local_ip, &local_port);

	if (verbose)
	{
		fprintf(stderr, "Local socket info: %s:%d (addr family: %d)\n", local_ip, local_port, addr_fam);
	}

	populate_sockaddr(remote_addr, addr_fam, remote_ip, remote_port);

	size_t n = 8;
	ssize_t nwritten = sendto(sfd, &buf, n, 0, remote_addr, addr_len);
	if (nwritten < 0)
	{
		perror("Error sending message");
		exit(EXIT_FAILURE);
	}

	if (verbose)
	{
		printf("Bytes sent: %ld\n", nwritten);
	}

	unsigned char treasure[1024];
	unsigned int total_len = 0;

	while (1)
	{
		unsigned char response[256];
		ssize_t nread = recvfrom(sfd, response, sizeof(response), 0, remote_addr, &addr_len);

		if (nread < 0)
		{
			perror("Error receiving response");
			exit(EXIT_FAILURE);
		}

		if (verbose)
		{
			printf("Bytes received: %ld\n", nread);
			print_bytes(response, nread);
		}

		unsigned char length = response[0];

		if (length == 0)
		{
			treasure[total_len] = '\0';
			printf("%s\n", treasure);
			exit(EXIT_SUCCESS);
		}

		if (length > 127)
		{
			fprintf(stderr, "Server error: %d", length);
			exit(EXIT_FAILURE);
		}

		memcpy(&treasure[total_len], &response[1], length);
		total_len += length;

		unsigned char op_code = response[length + 1];
		unsigned short op_param;
		memcpy(&op_param, &response[length + 2], 2);
		op_param = ntohs(op_param);

		if(op_code == 1){
			populate_sockaddr(remote_addr, addr_fam, remote_ip, op_param);
		}

		if(op_code == 2){
			if ((sfd = socket(addr_fam, SOCK_DGRAM, 0)) < 0)
			{
				perror("Error creating socket");
				exit(EXIT_FAILURE);
			}

			// Populate local_addr with the port using populate_sockaddr().
			populate_sockaddr(local_addr, addr_fam, NULL, op_param);
			if (bind(sfd, local_addr, sizeof(struct sockaddr_storage)) < 0)
			{
				perror("Could not bind");
				exit(EXIT_FAILURE);
			}
		}

		unsigned int nonce;
		memcpy(&nonce, &response[length + 4], 4);

		nonce = ntohl(nonce);
		nonce++;
		nonce = htonl(nonce);

		size_t n = 4;
		nwritten = sendto(sfd, &nonce, n, 0, remote_addr, addr_len);
		if (nwritten < 0)
		{
			perror("Error sending message");
			exit(EXIT_FAILURE);
		}

		if (verbose)
		{
			printf("Bytes sent: %ld\n", nwritten);
		}
	}
}

void print_bytes(unsigned char *bytes, int byteslen)
{
	int i, j, byteslen_adjusted;

	if (byteslen % 8)
	{
		byteslen_adjusted = ((byteslen / 8) + 1) * 8;
	}
	else
	{
		byteslen_adjusted = byteslen;
	}
	for (i = 0; i < byteslen_adjusted + 1; i++)
	{
		if (!(i % 8))
		{
			if (i > 0)
			{
				for (j = i - 8; j < i; j++)
				{
					if (j >= byteslen_adjusted)
					{
						printf("  ");
					}
					else if (j >= byteslen)
					{
						printf("  ");
					}
					else if (bytes[j] >= '!' && bytes[j] <= '~')
					{
						printf(" %c", bytes[j]);
					}
					else
					{
						printf(" .");
					}
				}
			}
			if (i < byteslen_adjusted)
			{
				printf("\n%02X: ", i);
			}
		}
		else if (!(i % 4))
		{
			printf(" ");
		}
		if (i >= byteslen_adjusted)
		{
			continue;
		}
		else if (i >= byteslen)
		{
			printf("   ");
		}
		else
		{
			printf("%02X ", bytes[i]);
		}
	}
	printf("\n");
	fflush(stdout);
}
