/*
 * client.c
 *
 *  Created on: Oct 19, 2013
 *      Author: Ashraf Saleh
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>

#define CLIENT_PORT_NO 9999
#define SERVER_PORT_NO 7777 
#define HOSTNAME "localhost"
#define MAX_cwnd 30

#define MAX_SEQ_N 4*MAX_cwnd // FIXME how should the client know that  

 //TODO fix
#define PKT_DATA_SIZE 10000
#define HEADER_SIZE 8
#define BUFFER_SIZE PKT_DATA_SIZE*MAX_SEQ_N //file buffer size in bytes

 char request_buffer[BUFFER_SIZE];
 unsigned char buffer[BUFFER_SIZE];
 int socket_fd;
 struct sockaddr_in server_addr, client_addr;
 socklen_t server_addr_len, client_addr_len;


 char file_cname[100];
// char read_buffer[BUFFER_SIZE];

// todo remove pkt struct from logic
 typedef struct{
 	int16_t len;
 	int16_t checksum;
 	int32_t seqno;
 	char data[PKT_DATA_SIZE];
 }pkt_t;

 typedef struct{
 	int16_t len;
 	int16_t checksum;
 	int32_t seqno;
 }ack_t;




 //TODO move
 int filesize;
int acked[MAX_cwnd]; // boolean indicator for each pkt in the window .. indicates acked or not

int parse_response() {
  ack_t ack = {8, 0, -1};
  ack_t recv_ack;
  pkt_t pkt;
  int r = recvfrom(socket_fd, &recv_ack, sizeof(recv_ack), 0, (struct sockaddr*) &server_addr, &server_addr_len);

    filesize = recv_ack.seqno; // file size --> my protocol i do what i want :P :D :D 
    // and of course assuming that the max file size is 4GB 

    int n;
    while (1){
        // loop while the server starts sending data pkts
        // resending the accepting connection ACK means that the client ack ack didn't reach the server
        // when the first data pkt is received break the loop

        // ack the ACK of the server (accepting connection ack)
        n = sendto(socket_fd, &ack, sizeof(ack_t), 0, (struct sockaddr*) &server_addr, server_addr_len);

        r = recvfrom(socket_fd, &pkt, sizeof(pkt_t), 0, (struct sockaddr*) &server_addr, &server_addr_len);
        if (pkt.len != sizeof(ack_t))    
            break;
    }

 	// int16_t len = buffer[0]|buffer[1]<<8;
 	// printf("len = %d\n",len);
 	// printf("%d %d %d %d\n",(int) buffer[0], (int) buffer[1], (int) buffer[2], (int) buffer[3]);
    FILE *op = fopen(file_cname, "wb");

    //FIXME move to top
    int i;
    int seqno;
    int base  = 0;
    int ws = MAX_SEQ_N;
    int end = MAX_SEQ_N + 1; // index of last packet should be received
    int MAX_PKT_CNT = BUFFER_SIZE/PKT_DATA_SIZE;
    int iscopied = 0; // flag used not to copy the first part of the buffer many times

    while(1){
        // assuming that the server and the client share the same MAX_SEQ_N 
        // Hence the seqno will overlap from the server (no need to take mod here)

        seqno = pkt.seqno;
        // means that the server is sending a previously acked pkt --> resend ack
        if (seqno < base){
            ack.len = sizeof(ack_t);
            ack.seqno = seqno;
            n = sendto(socket_fd, &ack, sizeof(ack_t), 0, (struct sockaddr*) &server_addr, server_addr_len);            

        } else if ( (base+ws < MAX_PKT_CNT && seqno > base + ws) 
            || (base+ws >= MAX_PKT_CNT && seqno > (base + ws)%MAX_PKT_CNT &&seqno < base )  ) {
            // this case should never happen
            perror("receiving a packet with a seqno > window");
        }else {

            memcpy(&(buffer[(base+seqno)*PKT_DATA_SIZE]), &(pkt.data), pkt.len-HEADER_SIZE);

            filesize -= PKT_DATA_SIZE;    

            acked[base+seqno] = 1 ;
            while(base<MAX_SEQ_N && acked[base])
                    (base + 1)%MAX_PKT_CNT; // move window to pass all acked pkts

                if (base == 0){
                // write the last window if the buffer.
                    fwrite(&(buffer[(MAX_PKT_CNT-ws)*PKT_DATA_SIZE]), PKT_DATA_SIZE, ws, op);
                // clear acked in the same range
                    memset(&acked[MAX_PKT_CNT-ws], 0, sizeof(int)*ws);
                    iscopied = 0;

                } else if ( base+ws >= MAX_PKT_CNT && !iscopied){
                // last window reached 
                // copy data in buffer from 0 --> base-1
                    fwrite(buffer, PKT_DATA_SIZE, MAX_SEQ_N-ws, op);
                // clear acked in the same range
                    memset(acked, 0, sizeof(int)*MAX_PKT_CNT-ws);                
                    iscopied = 1;
                }   

                ack.seqno = seqno ;
                n = sendto(socket_fd, &ack, sizeof(ack_t), 0, (struct sockaddr*) &server_addr, server_addr_len);            

                if(filesize <= 0){
                    // in the next line filesize should be = 0 or in negative ?? --> the data of the last packet was < PKT_DATA_SIZE
                    if (base < MAX_SEQ_N - ws) // i'm sure that the last ws of the buffer is written to the file
                        //write from base pkt 0 to pkt base
                        fwrite(&buffer, sizeof(char), base*PKT_DATA_SIZE + filesize, op);        

                    else // i'm sure that the first part (0--> MAX_SEQ_N-ws) of the buffer is written to the file
                        // write from MAX_SEQ_N -ws --> buffer
                        fwrite(&(buffer[MAX_SEQ_N-ws]), sizeof(char), (base-(MAX_SEQ_N-ws))*PKT_DATA_SIZE + filesize, op);        
                    break;
                }   
            }
            r = recvfrom(socket_fd, &pkt, sizeof(pkt_t), 0, (struct sockaddr*) &server_addr, &server_addr_len);
        }
        fclose(op);
    }


int main(int argc, char *argv[]) {

  in_port_t server_port_no = SERVER_PORT_NO;
  in_port_t client_port_no = CLIENT_PORT_NO;
  struct hostent *server;
  char *hostname = HOSTNAME;

  server = gethostbyname(hostname);

  if (server == NULL ) {
   perror("ERROR no such host exists");
   exit(EXIT_FAILURE);
}

char file_cname[] = "oblivion_rec.mp3";

socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
if (socket_fd < 0)
   perror("ERROR Opening client socket");


 	// setting server address
    memset(&server_addr, 0, sizeof(server_addr));// initialization
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port_no);
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);	
    server_addr_len = sizeof(server_addr);


	// setting client address
    memset(&client_addr, 0, sizeof(client_addr));// initialization
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(client_port_no);
    memcpy(&client_addr.sin_addr.s_addr, server->h_addr, server->h_length);	
    client_addr_len = sizeof(client_addr);


    if ( (bind(socket_fd, (struct sockaddr*) &client_addr, sizeof(client_addr)))  < 0){
    	perror("ERROR on binding socket to server address");
    	return EXIT_FAILURE;
    }


 // TODO 
 // request_buffer = file_cname;
    memcpy(&request_buffer, file_cname, sizeof(file_cname));

 	//send to server
    int n = sendto(socket_fd, request_buffer, strlen(request_buffer) , 0, (struct sockaddr*) &server_addr, server_addr_len);
    if (n < 0)
    	perror("ERROR couldn't write to socket");

    
    printf("[starting] read_response\n");
    parse_response();
    printf("[closing] read_response\n");

    close(socket_fd);

    return EXIT_SUCCESS;
}