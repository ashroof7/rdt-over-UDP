//============================================================================
// Name        : Client_StopAndWait.cpp
// Author      : Motaz Elshaer
// Version     :
// Copyright   : Your copyright notice
// Description : Client in C++, Ansi-style
//============================================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>

#define BUFFER_SIZE 1024
#define CLIENT_PORT_NO 9999
#define SERVER_PORT_NO 7777
#define HOSTNAME "localhost"

char request_buffer[BUFFER_SIZE];
unsigned char buffer[BUFFER_SIZE];
int socket_fd;
struct sockaddr_in server_addr, client_addr;
socklen_t server_addr_len, client_addr_len;

char file_cname[100];
// char read_buffer[BUFFER_SIZE];

// todo remove pkt struct from logic
#define PKT_DATA_SIZE 1000 // identify data size (in bytes) in a packet
typedef struct {
	int16_t len;
	int16_t checksum;
	int32_t seqno;
	char data[PKT_DATA_SIZE];
} pkt_t;

typedef struct {
	int16_t len;
	int16_t checksum;
	int32_t seqno;
} ack_t;

int parse_response() {
	int n = recvfrom(socket_fd, buffer, BUFFER_SIZE, 0,
			(struct sockaddr*) &server_addr, &server_addr_len);
	int16_t len = buffer[0] | buffer[1] << 8;
	printf("len = %d\n", len);
	printf("%d %d %d %d\n", (int) buffer[0], (int) buffer[1], (int) buffer[2],
			(int) buffer[3]);

	if (len == sizeof(ack_t)) {
		printf("is ACK\n");
	} else {
		printf("is data PKT\n");

		FILE *op = fopen(file_cname, "wb");
		while (1) {
			if (n <= 0) {
				fclose(op);
				break;
			}
			fwrite(buffer, sizeof(char), n, op);
			n = read(socket_fd, buffer, BUFFER_SIZE);
		}

	}

}

int main(int argc, char *argv[]) {

	in_port_t server_port_no = SERVER_PORT_NO;
	in_port_t client_port_no = CLIENT_PORT_NO;
	struct hostent *server;
	char *hostname = HOSTNAME;

	server = gethostbyname(hostname);

	if (server == NULL) {
		perror("ERROR no such host exists");
		exit(EXIT_FAILURE);
	}

	char file_cname[] = "oblivion.mp3";

	socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (socket_fd < 0)
		perror("ERROR Opening client socket");

	// setting server address
	memset(&server_addr, 0, sizeof(server_addr)); // initialization
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(server_port_no);
	memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
	server_addr_len = sizeof(server_addr);

	// setting client address
	memset(&client_addr, 0, sizeof(client_addr)); // initialization
	client_addr.sin_family = AF_INET;
	client_addr.sin_port = htons(client_port_no);
	memcpy(&client_addr.sin_addr.s_addr, server->h_addr, server->h_length);
	client_addr_len = sizeof(client_addr);

	if ((bind(socket_fd, (struct sockaddr*) &client_addr, sizeof(client_addr)))
			< 0) {
		perror("ERROR on binding socket to server address");
		return EXIT_FAILURE;
	}

	// TODO
	// request_buffer = file_cname;
	memcpy(&request_buffer, file_cname, sizeof(file_cname));

	//send to server
	int n = sendto(socket_fd, request_buffer, strlen(request_buffer), 0,
			(struct sockaddr*) &server_addr, server_addr_len);
	if (n < 0)
		perror("ERROR couldn't write to socket");

	printf("[starting] read_response\n");
	parse_response();
	printf("[closing] read_response\n");

	close(socket_fd);

	return EXIT_SUCCESS;
}
