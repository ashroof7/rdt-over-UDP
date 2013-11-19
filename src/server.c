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


#define PKT_DATA_SIZE 1 // identify data size (in bytes) in a packet
#define TIME_OUT_VAL 1000000ll // value in micro seconds 1 secs
#define SERVER_PORT_NO 7777
#define CLIENT_PORT_NO 9999
#define MAX_cwnd 5

// TODO read from file 
#define MAX_SEQ_N (4*MAX_cwnd)
#define BUFF_SIZE PKT_DATA_SIZE*MAX_SEQ_N //file buffer size in bytes
#define HEADER_SIZE 8 // size of header in bytes 
#define PKT_SIZE HEADER_SIZE+PKT_DATA_SIZE
#define timeval2long(a) (a.tv_sec*1000000 + a.tv_usec)

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

int active_timers = 0; // indicates no of running timer = number of un acked packets
int cwnd = MAX_cwnd;
unsigned int trans_sock;

int main_sock, worker_sock;
struct sockaddr_in server_addr, client_addr;
socklen_t server_addr_len = sizeof(server_addr);
socklen_t client_addr_len = sizeof(client_addr);


pkt_t packet_buff[MAX_SEQ_N];
char file_buff[BUFF_SIZE];

int buff_base = 0;
char file_name[100] = "test.txt";
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



void process_pkt(int seqno, int data_offset){
	packet_buff[seqno].len = PKT_SIZE;
	packet_buff[seqno].seqno =  data_offset + seqno*PKT_DATA_SIZE ;

	int offset = seqno*PKT_DATA_SIZE;
	memcpy(packet_buff[seqno].data, file_buff+offset, PKT_DATA_SIZE);

 // TODO calc checksum or crc
 // pkt.checksum = ?

}

long long timers[MAX_SEQ_N];
//FIXME refactor rename to timer_status
int valid_timer[MAX_SEQ_N]; // boolean if timer is acive = 1,2 and 0 is not active ... 
// 1 indicates that the packet to be resent is a data_pkt. 2 indicates ACK pkt.
int timer_n_index = cwnd; // largest index in the timers array

// void send_pkt(int seqno){
// 	int nbytes = 0;
// 	if ( (nbytes = send(trans_sock, &packet_buff[seqno], PKT_SIZE, 0)) < 0){
// 		perror("Sending packet");
// 	}else {
// 	  //start timer 
// 		valid_timer[seqno-buff_base] = 1;
// 		timers[seqno-buff_base] = TIME_OUT_VAL;
// 	}

// }

timeval timeout_tv = {TIME_OUT_VAL/1000000, TIME_OUT_VAL%(1000000)};
itimerval itimer = {timeout_tv, timeout_tv};

//TODO bete3mel eh di ????
ack_t timer_pkt_ack; // used in timer hadler to receive acks 



void timer_handler(int sig) {
    //start in method timer
	clock_t local_timer;
    long long min_timer = TIME_OUT_VAL+1; // set to maximum value

    int i;
    //TODO azbot el leela di :D 
    // for(i = 0; i <= timer_n_index; i++){
    for(i = 0; i <= MAX_SEQ_N; i++){
    	local_timer = clock(); 
    	if(valid_timer[i]>0){ // recall 0 = notset   -1 not intialized   1 pkt_timer    2 ack_timer
    		timers[i] -= (timeval2long(timeout_tv) + ((float) local_timer)/CLOCKS_PER_SEC*1000000);
    		
    		if (timers[i] <= 0){
                // re-send packet and update timer 
    			if (sendto(worker_sock, (void *)&(packet_buff[i]), (valid_timer[i]==1)?sizeof(pkt_t):sizeof(ack_t),
    				0, (struct sockaddr*) &client_addr, client_addr_len) < 0)
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




#define RQST_BUFF_SZ 100
// assuming that the coming request will always fit in this buffer
char request_buff[RQST_BUFF_SZ]; 


void rdt(){
	printf("## starting rdt() ###\n");

	//intializing valid_timer
	int i;
	for(i=0; i < MAX_SEQ_N; i++)
		valid_timer[i] = -1;

	//TODO seqno ?? for the ack ? and seqno for data pkts add ack size to them ? ?
 	int r; // no of bytes of the received message (ACK)
 	ack_t pkt_ack;
	open_file(file_name);
 	// adding file size to the request ack : my protocol .. i do what i want :P 
 	int filesize = calc_file_size();
 	printf("filesize = %d\n", filesize);

 	ack_t req_ack = {8, 0, filesize} ;
 	int n = sendto(worker_sock, (void *)&req_ack, sizeof(req_ack), 0, (struct sockaddr*) &client_addr, client_addr_len);
 	if (n < 0)
 		perror("ERROR couldn't write to socket");
 	printf("sent request ACK\n");

	//start timer 
	valid_timer[0] = 2; // quick and dirty sol: 2 indicates that the packet to resent is ack_t
	//also put the  ack_pkt in the data_pkts buffer 
	memcpy(packet_buff, &req_ack, sizeof(ack_t));

	timers[0] = TIME_OUT_VAL;
	itimer.it_interval = timeout_tv;
	itimer.it_value = timeout_tv;
	setitimer(ITIMER_REAL, &itimer, NULL);
	active_timers++;

	
	printf("going to wait for ack ack\n");

	// this call is supposed to block until an ACK is received ... signal handlers are executed in another thread
	r = recvfrom(worker_sock, &pkt_ack, sizeof(ack_t), 0, (struct sockaddr*) &client_addr, &client_addr_len);


	printf ("starting data transfer\n");

 	// send data
	int ack_seqno;

	// FIXME refactor rename with pkti since seqno is no in bytes
 	int seqno; // sequence no of the packet to sent, index within the current cwnd-long window
 	int m; // the # of bytes read from the file 
 	int N = MAX_SEQ_N; // pkt buffer length
	int start = 1; // boolean indicator to indicate first buffer fill 
	int end = MAX_SEQ_N + 1; // end = last packet to send outside the buffer until we meet file end;
	int prev_cwnd; //used to keep the value of the last cwnd on filling the buffer [0--> N - prev_cwnd]
	int EOF_reached = 0; // boolean indicator to indicate EOF reached
	int data_offset = 0; // data offset from the start of the file
	timeval  timer_difference; 

	printf("buffer = %s\n",file_buff);

	printf("starting to read from file N = %d  PKT_DATA_SIZE = %d\n",N,PKT_DATA_SIZE);
	// first time = true fill all the buffer
	// m = fread(file_buff, N, PKT_DATA_SIZE, file);
	m = fread(file_buff, PKT_DATA_SIZE, N, file);
	printf("read from file %d bytes\n",m);
	printf("buffer = %s\n",file_buff);

	if (m < 0)
		perror("couldn't read from file");
	

	if(m < N){
		end = ceil(1.0*m/PKT_SIZE);
		printf("file size is less than 1 buffer");
	}


	for (seqno = buff_base;  ;	seqno = (seqno + 1) % N) {

		// added in a separate condition to handle that EOF is reached 
		// if (buff_base == 0 && !start){
			// data_offset+=BUFF_SIZE;
			// printf("jjhafshhdasa\n");
		// }

		printf("ana geet \n");
		printf("seqno = %d base = %d\n", seqno, buff_base);
		// send packet 
		process_pkt(seqno, data_offset);
		n = sendto(worker_sock, (void *)&(packet_buff[seqno]), sizeof(pkt_t), 0, (struct sockaddr*) &client_addr, client_addr_len);
		printf("sent pkt[%c] seqno = %d\n", packet_buff[seqno].data[0], packet_buff[seqno].seqno);
	 	// start timer
		valid_timer[seqno] = 1;
		// if (active_timers > 0){
		//  	// set the timer value = timout - (current_timer_max_val - curren_timer);
		// 	getitimer(ITIMER_REAL, &itimer);
		// 	timer_difference.tv_sec = itimer.it_interval.tv_sec;
		// 	timer_difference.tv_usec = itimer.it_interval.tv_usec;
		// 	timers[seqno] = TIME_OUT_VAL - timeval2long(timer_difference);
		// }else {
		// 	timers[seqno] = TIME_OUT_VAL;
		// 	itimer.it_interval = timeout_tv;
		// 	itimer.it_value = timeout_tv;
		// 	setitimer(ITIMER_REAL, &itimer, NULL);
		// }
		// active_timers++;


		//the breaking condition is added inside the loop (after packet send) rather than in the for definition
		//to avoid the special case when end = N-1; in this case we would go into infinite loop (seqno<=end)
		//or don't send the last packet (end).
		printf("sqno = %d\n",seqno);
		printf("end = %d\n",end);
		if (seqno ==  end ){
			printf("server is leaving ... FUCK this shit\n");
			break;
		}

		

		

		// recieve acks (n acks) and update window boundries
		printf("valid 5ara : ");
		for ( i = 0; i < MAX_cwnd; ++i)
			printf("%d ", valid_timer[i]);
		printf("\n");

		r = recvfrom(worker_sock, &pkt_ack, sizeof(ack_t), 0, (struct sockaddr*) &client_addr, &client_addr_len);
		while (r > 0){
			//FIXME refactor ack_seqno to pkt_no
			ack_seqno = (pkt_ack.seqno / PKT_DATA_SIZE) % MAX_SEQ_N;
			// ack_seqno = pkt_ack.seqno ;
			printf(" >>> received ACK seqno = %d  %d\n",pkt_ack.seqno, ack_seqno);
			valid_timer[ack_seqno] = 0;
			if (ack_seqno == buff_base)
				while (valid_timer[ack_seqno++]==0){ // if a valid timer ==0 then the timer is off
					valid_timer[buff_base] = -1; // setting the status of this packet to not set bec it's outside the window
					buff_base++;
				}
			r = recvfrom(worker_sock, &pkt_ack, sizeof(ack_t), MSG_DONTWAIT, (struct sockaddr*) &client_addr, &client_addr_len);
		} 					


		printf(">>>>> buff_base = %d\n",buff_base);
		// copy the last window of the buffer (was kept while the whole buffer was copied) [N-cwnd-->N]
		if (buff_base == N ) {
			data_offset+=BUFF_SIZE;
			if ( !EOF_reached && !start){
				m = fread(file_buff+(N-prev_cwnd)*PKT_DATA_SIZE, PKT_DATA_SIZE, prev_cwnd, file);
				printf("buffer = %s\n",file_buff);

				if(m < prev_cwnd){
					EOF_reached = 1;
					end = N-prev_cwnd + m;
					// break;
				}
			}
		}


		// copy from the start of the buffer except the last cwnd items
		// the window is at the end of buffer (next base++ --> goes to start)
			if (buff_base + cwnd == N && !EOF_reached) { 
				prev_cwnd = cwnd;
				start = 0;
				m = fread(file_buff, PKT_DATA_SIZE, N-cwnd, file);
				printf("buffer = %s\n",file_buff);

				if(m < N-cwnd){
					printf("EOF reached\n");
					EOF_reached = 1;
					end = m;
					// break;
				}
			}
			printf("\n");

		// sleep case : seqno = last element in window 
		// seqno = (base+cwnd)%N;
			if (seqno == (buff_base+cwnd)%N && !start){
				printf("hanaam nena\n");
				sleep(1000000);
			}
			buff_base %= N;
		}





	}


	void connect(){
		rdt() ;
	}


	int main(){


		signal(SIGALRM, timer_handler);

	//TODO read from input file
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
	 printf("mother pid = %d\n", getpid());
	 while (1) {

 	// receive request
 	// since we know the server the request message only contains the file path/name
	 	int	recvlen = recvfrom(main_sock, request_buff, (size_t)RQST_BUFF_SZ, 0, (struct sockaddr*) &client_addr, &client_addr_len);
	 	printf("%s\n",request_buff); 	

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
  			printf("pid = %d\n", getpid());
  			rdt();
  			exit(EXIT_SUCCESS);
   		} else { // pid != 0 then we are in the parent;
   			close(worker_sock);
   		}

   }
   return EXIT_SUCCESS;



}
