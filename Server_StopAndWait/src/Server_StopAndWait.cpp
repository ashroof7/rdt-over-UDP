//============================================================================
// Name        : Server_StopAndWait.cpp
// Author      : Motaz ElShaer
// Version     :
// Copyright   : Your copyright notice
// Description : Server in C++, Ansi-style
//============================================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <time.h>

#define PKT_DATA_SIZE 500 // identify data size (in bytes) in a packet
#define TIME_OUT_VAL 500000 // value in micro seconds
#define SERVER_PORT_NO 7777
#define CLIENT_PORT_NO 9999
#define MAX_cwnd 30

// TODO read from file
#define MAX_SEQ_N 4*MAX_cwnd
#define BUFF_SIZE PKT_DATA_SIZE*MAX_SEQ_N //file buffer size in bytes
#define HEADER_SIZE 8 // size of header in bytes
#define PKT_SIZE HEADER_SIZE+PKT_DATA_SIZE

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

int cwnd = MAX_cwnd;
unsigned int trans_sock;

int main_sock, worker_sock;
struct sockaddr_in server_addr, client_addr;
socklen_t server_addr_len = sizeof(server_addr);
socklen_t client_addr_len = sizeof(client_addr);

pkt_t packet_buff[MAX_SEQ_N];
char file_buff[MAX_SEQ_N];

int buff_base = 0;
char file_name[100];
FILE *file;
pkt_t curPkt;
int global_counter = 0;
double PLP = 0.2f; 

#define close_file(a) fclose(a)
#define open_file(s) (file = fopen(s, "rb"))

void read_nbyte(unsigned long offset, unsigned long n) {
	// read n bytes from the the file starting from offset
	fseek(file, offset, SEEK_SET);
	n = n >= MAX_SEQ_N ? MAX_SEQ_N - 1 : n;
	fread(file_buff, n, 1, file);
}

unsigned long calc_file_size() {
	//Get file length
	fseek(file, 0, SEEK_END);
	unsigned long len = ftell(file);
	fseek(file, 0, SEEK_SET);
	return len;
}

void process_pkt(int seqno) {
	packet_buff[seqno].len = PKT_SIZE;
	packet_buff[seqno].seqno = seqno;

	int offset = seqno * PKT_DATA_SIZE;
	memcpy(packet_buff[seqno].data, file_buff + offset, PKT_DATA_SIZE);

	// TODO calc checksum or crc
	// pkt.checksum = ?

}

#define RQST_BUFF_SZ 100
// assuming that the coming request will always fit in this buffer
char request_buff[RQST_BUFF_SZ];

void timerHandler(int sig) {
	printf("we will send again from timer handler. \n");
	int n = sendto(worker_sock, (void *) &curPkt, sizeof(curPkt), 0,
			(struct sockaddr*) &client_addr, client_addr_len);
	ualarm(TIME_OUT_VAL, 0);
}

void sendData() {
	FILE *rd = fopen(file_name, "r");
	int32_t seqno = 0;
	while (1) {
		char data[PKT_DATA_SIZE];
		int32_t n = fread(data, sizeof(char), (sizeof(data) / sizeof(char)),
				rd);
		if (n > 0) {
			pkt_t pkt;
			pkt.checksum = 0;
			pkt.seqno = seqno;
			printf("[SendPktSecNo:] %d\n", seqno);
			seqno += n;
			memcpy(pkt.data, data, n);
			pkt.len = sizeof(ack_t) + n;
			memcpy(&curPkt, &pkt, sizeof(pkt));

			int flag = ( global_counter% ((int)(PLP*100)) ); 
			global_counter++;
			printf("flag ******************** %d \n", flag);
			if(flag){
			sendto(worker_sock, (void *) &pkt, sizeof(pkt), 0,
					(struct sockaddr*) &client_addr, client_addr_len);				
			}else{
				printf("[PacketLost]***********************************\n");
			}
			ualarm(TIME_OUT_VAL, 0);
			printf("[WaitingAckNo:] %d\n", seqno);
			int recvlen = recvfrom(worker_sock, request_buff,
					(size_t) RQST_BUFF_SZ, 0, (struct sockaddr*) &client_addr,
					&client_addr_len);
			int16_t len = request_buff[0] | request_buff[1] << 8;
			if (len == sizeof(ack_t)) {
				ualarm(0, 0);
				ack_t recievedAck;
				memcpy(&recievedAck, &request_buff, len);
				printf("[ReceivingAckNo:] %d\n", recievedAck.seqno);
			}
		} else {
			fclose(rd);
			break;
		}
	}
}

void rdt() {
	// ACK request
	ack_t req_ack = { 8, 0, 0 };
	int n = sendto(worker_sock, (void *) &req_ack, sizeof(req_ack), 0,
			(struct sockaddr*) &client_addr, client_addr_len);
	if (n < 0) {
		perror("ERROR couldn't write to socket");
	} else { // send data
		printf("[Starting] Sending Data \n");
		sendData();
		printf("[Closing] Sending Data\n");
	}
}

void connect() {
	rdt();
}

int main() {
	signal(SIGALRM, timerHandler);
	//TODO read from input file
	in_port_t server_portno = SERVER_PORT_NO;
	in_port_t client_portno = CLIENT_PORT_NO;

	if ((main_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("Error: Creating the main socket");
		return EXIT_FAILURE;
	}

	// setting server_addr
	memset((char *) &server_addr, 0, sizeof(server_addr));	// initialization
	// 0 init is important since the server_addr.zero[] must be zero
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(server_portno);

	// setting client_addr
	memset((char *) &client_addr, 0, client_addr_len);

	if ((bind(main_sock, (struct sockaddr*) &server_addr, sizeof(server_addr)))
			< 0) {
		perror("ERROR on binding socket to server address");
		return EXIT_FAILURE;
	}

	int worker_pid;

	while (1) {

		// receive request
		// since we know the server the request message only contains the file path/name
		int recvlen = recvfrom(main_sock, request_buff, (size_t) RQST_BUFF_SZ,
				0, (struct sockaddr*) &client_addr, &client_addr_len);
		// copy the file which you want from server here !! 
		memcpy(&file_name, &request_buff, recvlen);
		printf("[client_addr_lenquestFile:] %s\n", file_name);

		worker_pid = fork();

		if (worker_pid < 0) {
			perror("ERROR on forking a new worker process");
			continue;
		}

		if (!worker_pid) { // worker_pid = 0 then we are in the child
			// create the working socket
			if ((worker_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
				perror("Error: Creating the worker socket");
				return EXIT_FAILURE;
			}
			connect();
			close(main_sock);
			exit(EXIT_SUCCESS);
		} else { // pid != 0 then we are in the parent;
			close(worker_sock);
		}
	}
	return EXIT_SUCCESS;
}
