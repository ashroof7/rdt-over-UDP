#include <sys/socket.h>
#include <sys/types.h>

#define PKT_DATA_SIZE 1000 // identify data size (in bytes) in a packet

struct pkt{
	int16_t len;
	int16_t checksum;
	byte[PKT_DATA_SIZE] data;
}

int main(){
	int main_sock;
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

	if (bind(main_sock, (struct sockaddr*) &server_addr, sizeof(server_addr)) 	< 0){
		perror("ERROR on binding socket to server address");
		return EXIT_FAILURE;
	}

	int worker_pid;
	socklen_t client_addr_len = sizeof(client_addr);

	while (1) {
		listen(main_sock, 5);
		worker_socket_fd = accept(main_sock, (struct sockaddr*) &client_addr, &client_addr_len);

		if (worker_socket_fd < 0) {
			perror("ERROR accepting the new connection");
			continue;
		}

		if (worker_pid = fork() < 0) {
			perror("ERROR on forking a new worker process");
			continue;
		}

		if (!worker_pid) { // worker_pid = 0 then we are in the child
			close(main_sock);
			exit(EXIT_SUCCESS);
		} else { // pid != 0 then we are in the parent;
			close(worker_socket_fd);
		}

	}
	return EXIT_SUCCESS;



}