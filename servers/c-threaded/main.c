#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <signal.h>
#include <pthread.h>

#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <errno.h>

#include <sys/wait.h>

static int done = 0;
static const int ONE = 1;

void * handle_client(void * arg);
void sig_handler(int signo) {
	done = 1;
}

int main(int argc, char *argv[])
{
	int opt, ret;
	int port;
	long i;

	signal(SIGINT, sig_handler);
	signal(SIGKILL, sig_handler);
	signal(SIGTERM, sig_handler);

	struct sockaddr_in servaddr;
	struct timeval timeout;
	int ssock, *csock;

	pthread_t thread;

	while ((opt = getopt(argc, argv, "p:")) != -1) {
		switch (opt) {
			case 'p':
				port = atoi(optarg);
				break;
			default: /* '?' */
				fprintf(stderr, "Usage: %s -p port\n", argv[0]);
				exit(EXIT_FAILURE);
		}
	}

	if (optind < argc) {
		fprintf(stderr, "Unexpected additional arguments:");
		for (i = optind; i < argc; i++) {
			fprintf(stderr, " %s", argv[i]);
		}
		fprintf(stderr, "\n");
		exit(EXIT_FAILURE);
	}

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	servaddr.sin_port = htons(port);

	ssock = socket(AF_INET, SOCK_STREAM, 0);
	if (ssock == -1) {
		// TODO: handle error
	}

	timeout.tv_sec = 1;
	timeout.tv_usec = 0;
	setsockopt(ssock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));

	ret = bind(ssock, (struct sockaddr *)&servaddr, sizeof(servaddr));
	if (ret == -1) {
		perror("failed to bind to socket");
		return EXIT_FAILURE;
	}

	ret = listen(ssock, 0);
	if (ret == -1) {
		perror("failed to listen on socket");
		return EXIT_FAILURE;
	}

	while (done == 0) {
		csock = malloc(sizeof(int));

		do {
			*csock = accept(ssock, NULL, NULL);
		} while (*csock == -1 && errno == EAGAIN);

		if (*csock == -1) {
			if (errno == EINTR) {
				free(csock);
				continue;
			}
			perror("failed to accept connection");
			return EXIT_FAILURE;
		}

		setsockopt(*csock, IPPROTO_TCP, TCP_NODELAY, &ONE, sizeof(ONE));

		ret = pthread_create(&thread, NULL, handle_client, csock);
		if (ret != 0) {
			perror("failed to spawn client thread");
			return EXIT_FAILURE;
		}
	}

	close(ssock);
	return EXIT_SUCCESS;
}

void * handle_client(void * arg) {
	int ret;
	int csock = *((int *)arg);

	uint32_t challenge;

	while (done == 0) {
		do {
			ret = recvfrom(csock, &challenge , sizeof(challenge), MSG_WAITALL, NULL, NULL);
		} while (ret == -1 && (errno == EAGAIN || errno == EINTR));

		if (ret == -1) {
			perror("failed to receive challenge from client");
			break;
		}
		if (ret == 0) {
			// connection closed
			break;
		}

		challenge = ntohl(challenge);
		if (challenge == 0) {
			done = 1;
			break;
		}
		challenge = htonl(challenge + 1);

		do {
			ret = sendto(csock, &challenge, sizeof(challenge), 0, NULL, 0);
		} while (ret == -1 && (errno == EAGAIN || errno == EINTR));

		if (ret == -1) {
			perror("failed to send response to client");
			break;
		}
	}

	close(csock);
	free(arg);

	return NULL;
}
