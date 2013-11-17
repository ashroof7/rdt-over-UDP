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


#define PKT_DATA_SIZE 1000 // identify data size (in bytes) in a packet
#define TIME_OUT_VAL 500000ll // value in micro seconds
#define PORT_NO 7777 
#define MAX_cwnd 30

// TODO read from file 
#define MAX_SEQ_N 4*MAX_cwnd
#define BUFF_SIZE PKT_DATA_SIZE*MAX_SEQ_N //file buffer size in bytes
#define HEADER_SIZE 8 // size of header in bytes 
#define PKT_SIZE HEADER_SIZE+PKT_DATA_SIZE

typedef struct{
 int16_t len;
 int16_t checksum;
 int32_t seqno;
 char data[PKT_DATA_SIZE];
}pkt_t;

int cwnd = MAX_cwnd;
unsigned int trans_sock;


pkt_t packet_buff[MAX_SEQ_N];
char file_buff[MAX_SEQ_N];

int buff_base = 0;
char file_name[100] = "oblivion.mp3";
FILE *file;

#define close_file(a) fclose(a)
#define open_file(s) (file = fopen(s, "rb"))

void read_nbyte(unsigned long offset, unsigned long n){
 // read n bytes from the the file starting from offset 
 fseek(file, offset, SEEK_SET);
 n = n>=MAX_SEQ_N? MAX_SEQ_N-1 : n;
 fread(file_buff, n, 1, file);
}

unsigned long calc_file_size(){
 //Get file length
 fseek(file, 0, SEEK_END);
 unsigned long len=ftell(file);
 fseek(file, 0, SEEK_SET);
 return len;
}



void process_pkt(int seqno){
 packet_buff[seqno].len = PKT_SIZE;
 packet_buff[seqno].seqno =  seqno ;

 int offset = seqno*PKT_DATA_SIZE;
 memcpy(packet_buff[seqno].data, file_buff+offset, PKT_DATA_SIZE);

 // TODO calc checksum or crc
 // pkt.checksum = ?

}


long long timer[MAX_cwnd];
short valid_timer[MAX_cwnd]; // boolean if timer is acive = true, false otherwise.
int timer_n_index = cwnd; // largest index in the timers array

void send_pkt(int seqno){
	int nbytes = 0;
 if ( (nbytes = send(trans_sock, &packet_buff[seqno], PKT_SIZE, 0)) < 0){
  perror("Sending packet");
 }else {
  //start timer 
  valid_timer[seqno-buff_base] = 1;
  timer[seqno-buff_base] = TIME_OUT_VAL;
 }

}


timeval welcome_timeval = {TIME_OUT_VAL/1000000, TIME_OUT_VAL%(1000000)};
itimerval welcome_itimer = {welcome_timeval, welcome_timeval};

long long request_timers[MAX_cwnd];
unsigned char valid_w_timer[MAX_cwnd]; // boolean if timer is acive = true, false otherwise.

// rename 
void timer_handler(int sig) {
    //start in method timer
    clock_t local_timer;
    long long min_timer = TIME_OUT_VAL; // set to maximum value
  
    int i;
    for(i = 0; i <= timer_n_index; i++){
        local_timer = clock(); 
        if(valid_timer[i]){
            request_timers[i] -= (welcome_timeval.tv_usec+welcome_timeval.tv_sec*1000000 
             + ((float) local_timer)/CLOCKS_PER_SEC*1000000);
            if (request_timers[i] <= 0){
                // fire_action(timer[i]);
                request_timers[i] = TIME_OUT_VAL;
            }else if (request_timers[i] < min_timer){
                min_timer = request_timers[i];
            }
        }
    }

    welcome_timeval.tv_sec = min_timer/1000000;
    welcome_timeval.tv_usec = min_timer%(1000000);

    welcome_itimer.it_interval = welcome_timeval;
    welcome_itimer.it_value = welcome_timeval;
 setitimer(ITIMER_REAL, &welcome_itimer, NULL);
}



int main(){


    signal(SIGALRM, timer_handler);


 int main_sock, worker_sock;
 in_port_t port_no = PORT_NO;
 struct sockaddr_in server_addr, client_addr;



 if(main_sock = socket(AF_INET, SOCK_DGRAM, 0) < 0){
  perror("Error: Creating the main socket");
  return EXIT_FAILURE;
 }


 memset((char *)&server_addr, 0, sizeof(server_addr));// initialization
 // 0 init is important since the server_addr.zero[] must be zero
 server_addr.sin_addr.s_addr = INADDR_ANY;
 server_addr.sin_family = AF_INET;
 server_addr.sin_port = htons(port_no);

 if (bind(main_sock, (struct sockaddr*) &server_addr, sizeof(server_addr))  < 0){
  perror("ERROR on binding socket to server address");
  return EXIT_FAILURE;
 }

 int worker_pid;
 socklen_t client_addr_len = sizeof(client_addr);

 while (1) {
  listen(main_sock, 5);
  worker_sock = accept(main_sock, (struct sockaddr*) &client_addr, &client_addr_len);

  if (worker_sock < 0) {
   perror("ERROR accepting the new connection");
   continue;
  }

  if (worker_pid = fork() < 0) {
   perror("ERROR on forking a new worker process");
   continue;
  }

  if (!worker_pid) { // worker_pid = 0 then we are in the child
      signal(SIGALRM, timer_handler);

   close(main_sock);
   exit(EXIT_SUCCESS);
  } else { // pid != 0 then we are in the parent;
   close(worker_sock);
  }

 }
 return EXIT_SUCCESS;



}
