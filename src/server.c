/*
 * server.c
 *
 *  Created on: Oct 19, 2013
 *      Author: Ashraf Saleh
 */

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
#include <math.h>

#define PLP 0.1 // packet loss probabilty 
#define MAX_cwnd 50
#define SERVER_PORT_NO 7777
#define CLIENT_PORT_NO 9999
#define RAND_SEED 3571

#define PKT_DATA_SIZE (1024) // identify data size (in bytes) in a packet
#define TIME_OUT_VAL 10000ll // value in micro seconds 0.01 secs
#define MAX_SEQ_N (4*MAX_cwnd)
#define BUFF_SIZE (PKT_DATA_SIZE*MAX_SEQ_N) //file buffer size in bytes
#define HEADER_SIZE 8 // size of header in bytes 
#define PKT_SIZE (HEADER_SIZE+PKT_DATA_SIZE)

#define timeval2long(a) (a.tv_sec*1000000 + a.tv_usec)
#define close_file(a) fclose(a)
#define open_file(s) (file = fopen(s, "rb"))
#define max(a,b) ((a)>(b)?(a):(b))
#define min(a,b) ((a)>(b)?(b):(a))



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


int pkt_cnt = 0;
int loss_cnt = PLP * 100;
int active_timers = 0; // indicates no of running timer = number of un acked packets
int cwnd = MAX_cwnd; // size of sliding window moving in buffer
//TODO rename --> window base or base 
int buff_base = 0; // base of the moving window
int ssthreshold = (MAX_cwnd/2);
int prev_cwnd = cwnd;//used to keep the value of the last cwnd on filling the buffer [0--> N - prev_cwnd]



// main params
int main_sock, worker_sock;
struct sockaddr_in server_addr, client_addr;
socklen_t server_addr_len = sizeof(server_addr);
socklen_t client_addr_len = sizeof(client_addr);


FILE *file;

// buffers
char file_name[100]; // assuming that the coming request will only contain a file name
int filesize = 0;
pkt_t packet_buff[MAX_SEQ_N];
char file_buff[BUFF_SIZE];

//timers vars
long long timers[MAX_SEQ_N];
//FIXME refactor rename to timer_status
int valid_timer[MAX_SEQ_N]; // boolean if timer is acive = 1,2 and 0 is not active ... 
// 1 indicates that the packet to be resent is a data_pkt. 2 indicates ACK pkt.
int timer_n_index = cwnd; // largest index in the timers array
timeval timeout_tv = {TIME_OUT_VAL/1000000, TIME_OUT_VAL%(1000000)};
itimerval itimer = {timeout_tv, timeout_tv};



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

void process_pkt(int seqno, int data_offset){
	packet_buff[seqno].len = PKT_SIZE;
	packet_buff[seqno].seqno =  data_offset + seqno*PKT_DATA_SIZE ;

	int offset = seqno*PKT_DATA_SIZE;
	memcpy(packet_buff[seqno].data, file_buff+offset, PKT_DATA_SIZE);

 // TODO calc checksum or crc
 // pkt.checksum = ?
}

// next 2 functions are used to handle congestion control
void packet_loss_report(){
	// not called -- pulling back congestion control
	ssthreshold = cwnd/2;
	cwnd = max(1, cwnd/2);
	prev_cwnd = cwnd;
	printf("new cwnd %d\n",cwnd);
}

void packet_received_report(){
	// not called -- pulling back congestion control

	// if the current cwnd is above the ssthreshold then go for additive increase
	// other case we are in the slowstart phase multiply cwnd by 2
	cwnd = cwnd>ssthreshold? cwnd+1 : (cwnd<<1);
	cwnd = min(cwnd, MAX_cwnd);

}

void timer_handler(int sig) {
    //start in method timer
	clock_t local_timer;
    long long min_timer = TIME_OUT_VAL+1; // set to maximum value

    if(active_timers<1)
    	return;

    int i;
    for(i = 0; i < MAX_SEQ_N; i++){
    	local_timer = clock(); 
    	if(valid_timer[i]>0){ // recall 0 = notset   -1 not intialized   1 pkt_timer    2 ack_timer
    		timers[i] -= (timeval2long(timeout_tv) + ((float) local_timer)/CLOCKS_PER_SEC*1000000);
    		
    		if (timers[i] <= 0){
                // re-send packet and update timer 
                int temp =  sendto(worker_sock, (void *)&(packet_buff[i]), (valid_timer[i]==1)?sizeof(pkt_t):sizeof(ack_t),
    				0, (struct sockaddr*) &client_addr, client_addr_len) ;
                // packet_loss_report();
    			if (temp< 0)
    				timers[i] = TIME_OUT_VAL;
    			else {
    				valid_timer[i] = 0;	
    				active_timers--;
    			}

    		}else if (timers[i] < min_timer){
    			min_timer = timers[i];
    		}
    	}
    }

    if (min_timer > TIME_OUT_VAL){ // indicates that no timer is active (based on the intialization of min_timer)
    	return;
    }

    timeout_tv.tv_sec = min_timer/1000000;
    timeout_tv.tv_usec = min_timer%(1000000);

    itimer.it_interval = timeout_tv;
    itimer.it_value = timeout_tv;
    setitimer(ITIMER_REAL, &itimer, NULL);    
}


void hand_shake(){
	// adding file size to the request ack : my protocol .. i do what i want :P 
	ack_t pkt_ack;
 	ack_t req_ack = {8, 0, filesize} ;
 	int n = sendto(worker_sock, (void *)&req_ack, sizeof(req_ack), 0, (struct sockaddr*) &client_addr, client_addr_len);
 	if (n < 0)
 		perror("ERROR couldn't write to socket");

	//start timer for request acceptance pkt
	valid_timer[0] = 2; // quick and dirty sol: 2 indicates that the packet to resent is ack_t
	//also put the  ack_pkt in the data_pkts buffer 
	memcpy(packet_buff, &req_ack, sizeof(ack_t));

	timers[0] = TIME_OUT_VAL;
	itimer.it_interval = timeout_tv;
	itimer.it_value = timeout_tv;
	setitimer(ITIMER_REAL, &itimer, NULL);
	active_timers++;
	
	if(filesize < 0) // no need to complete handshake
		return;

	// this call is supposed to block until an ACK is received ... signal handlers are executed in another thread
	recvfrom(worker_sock, &pkt_ack, sizeof(ack_t), 0, (struct sockaddr*) &client_addr, &client_addr_len);

	// handshaking complete reset values of active_times
	active_timers--;
	valid_timer[0] = -1;
}


void rdt(){
	printf("## starting rdt() ###\n");

	// FIXME refactor rename with pkti since seqno is no in bytes
 	int seqno; // sequence no of the packet to sent, index within the current cwnd-long window
 	int r; // no of bytes of the received message (ACK)
 	int n; // no of bytes of the received method
 	int m; // the # of bytes read from the file 
 	int N = MAX_SEQ_N; // pkt buffer length
	int start = 1; // boolean indicator to indicate first buffer fill 
	int end = MAX_SEQ_N + 1; // end = last packet to send outside the buffer until we meet file end;
	// int prev_cwnd; //used to keep the value of the last cwnd on filling the buffer [0--> N - prev_cwnd]
	int EOF_reached = 0; // boolean indicator to indicate EOF reached
	int data_offset = 0; // data offset from the start of the file
	int ack_seqno;
 	ack_t pkt_ack;
	timeval  timer_difference; 



	// first time = true fill all the buffer
	m = fread(file_buff, PKT_DATA_SIZE, N, file);

	if (m < 0)
		perror("couldn't read from file");
	
	if(m < N)
		end = m;
	

	for (seqno = buff_base;  ;	seqno = (seqno + 1) % N) {
		// printf("cwnd = %d seqno = %d base = %d  end = %d\n", cwnd, seqno, buff_base, end);
		
		// send packet 
		process_pkt(seqno, data_offset);

		if ( (rand()%100) >  loss_cnt){ // if the random num gen [0-->10] is > than the number of packets to drop
			n = sendto(worker_sock, (void *)&(packet_buff[seqno]), sizeof(pkt_t), 0, (struct sockaddr*) &client_addr, client_addr_len);
		}else {
			// loss occured 
			// printf("loss \n");
		}
	 	
	 	// start timer
		valid_timer[seqno] = 1;
		if (active_timers > 0){
		 	// set the timer value = timout - (current_timer_max_val - curren_timer);
			getitimer(ITIMER_REAL, &itimer);
			timer_difference.tv_sec = itimer.it_interval.tv_sec;
			timer_difference.tv_usec = itimer.it_interval.tv_usec;
			timers[seqno] = TIME_OUT_VAL - timeval2long(timer_difference);
		}else {
			timers[seqno] = TIME_OUT_VAL;
			itimer.it_interval = timeout_tv;
			itimer.it_value = timeout_tv;
			setitimer(ITIMER_REAL, &itimer, NULL);
		}
		active_timers++;



		//the breaking condition is added inside the loop (after packet send) rather than in the for definition
		//to avoid the special case when end = N-1; in this case we would go into infinite loop (seqno<=end)
		//or don't send the last packet (end).

		if (seqno ==  end ){
			printf("Transmission Complete :D \n");
			break;
		}

		// recieve acks (n acks) and update window boundries
		r = recvfrom(worker_sock, &pkt_ack, sizeof(ack_t), 0, (struct sockaddr*) &client_addr, &client_addr_len);
		while (r > 0){
			//FIXME refactor ack_seqno to pkt_no
			ack_seqno = (pkt_ack.seqno / PKT_DATA_SIZE) % MAX_SEQ_N;

			// packet_received_report();
			valid_timer[ack_seqno] = 0;
			if (ack_seqno == buff_base)
				while (valid_timer[ack_seqno++]==0){ // if a valid timer ==0 then the timer is off
					valid_timer[buff_base] = -1; // setting the status of this packet to not set bec it's outside the window
					buff_base++;
				}
			r = recvfrom(worker_sock, &pkt_ack, sizeof(ack_t), MSG_DONTWAIT, (struct sockaddr*) &client_addr, &client_addr_len);
		} 					

		// copy the last window of the buffer (was kept while the whole buffer was copied) [N-cwnd-->N]
		if (buff_base == N ) {
			data_offset+=BUFF_SIZE;
			if ( !EOF_reached && !start){
				m = fread(file_buff+(N-prev_cwnd)*PKT_DATA_SIZE, PKT_DATA_SIZE, prev_cwnd, file);

				if(m < prev_cwnd){
					EOF_reached = 1;
					end = N-prev_cwnd + m;
				}
			}
		}


		// copy from the start of the buffer except the last cwnd items
		// the window is at the end of buffer (next base++ --> goes to start)
			if (buff_base + cwnd == N && !EOF_reached) { 
				// prev_cwnd = cwnd;
				start = 0;
				m = fread(file_buff, PKT_DATA_SIZE, N-cwnd, file);
				// printf("buffer = %s\n",file_buff);

				if(m < N-cwnd){
					EOF_reached = 1;
					end = m;
				}
			}
			// printf("\n");

			if (seqno == (buff_base+cwnd)%N && !start){
				sleep(1000000);
			}
			buff_base %= N;
		}

	printf("## return rdt() ###\n");	
}


void connect(){
	
	// initialization require for conestion control
	// intialize the starting cwnd
	// cwnd  = 1;
	// ssthreshold = MAX_SEQ_N/2;

	//intializing valid_timer
	int i;
	for(i=0; i < MAX_SEQ_N; i++)
		valid_timer[i] = -1;

	open_file(file_name);

	if (file == NULL){ // can't open file
		filesize = -1;
		hand_shake();
	}else {
		filesize = calc_file_size();
		hand_shake();
		rdt();
	}
	printf("operation [completed]\n");

}


int main(){


	signal(SIGALRM, timer_handler);

	
	in_port_t server_portno = SERVER_PORT_NO;
	in_port_t client_portno = CLIENT_PORT_NO;


	if( (main_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
		perror("Error: Creating the main socket");
		return EXIT_FAILURE;
	}

	// setting server_addr
	memset((char *)&server_addr, 0, sizeof(server_addr));// initialization
	// 0 init is important since the server_addr.zero[] must be zero
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(server_portno);

	// setting client_addr
	memset((char *) &client_addr, 0, client_addr_len);

	if ( (bind(main_sock, (struct sockaddr*) &server_addr, sizeof(server_addr)))  < 0){
		perror("ERROR on binding socket to server address");
	 	return EXIT_FAILURE;
	}

	int worker_pid;
	while (1) {

 		// receive request
 		// since we know the server the request message only contains the file path/name : my protocol my rules :P
	 	int	recvlen = recvfrom(main_sock, file_name, sizeof(file_name), 0, (struct sockaddr*) &client_addr, &client_addr_len);
	 	printf("requested file is: %s\n",file_name); 	

	 	if ( (worker_pid = fork()) < 0) {
	 		perror("ERROR on forking a new worker process");
	 		continue;
	 	}

  		if (!worker_pid) { // worker_pid = 0 then we are in the child
		  	// create the working socket 
  			close(main_sock);
  			if ( (worker_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
  				perror("Error: Creating the worker socket");
  				return EXIT_FAILURE;
  			}

  			signal(SIGALRM, timer_handler);
  			connect();
  			exit(EXIT_SUCCESS);
   		} else { // pid != 0 then we are in the parent;
   			close(worker_sock);
   		}

   }
   return EXIT_SUCCESS;
}
