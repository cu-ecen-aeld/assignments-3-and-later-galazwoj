
/**
* Create a socket based program with name aesdsocket in the “server” directory which:
*     a. Written in C
*     b. Opens a stream socket bound to port 9000, failing and returning -1 if any of the socket connection steps fail.
*     c. Listens for and accepts a connection
*     d. Logs message to the syslog “Accepted connection from xxx” where XXXX is the IP address of the connected client. 
n     e. Receives data over the connection and appends to file /var/tmp/aesdsocketdata, creating this file if it doesn’t exist.
*    	Your implementation should use a newline to separate data packets received.  In other words a packet is considered complete 
*	when a newline character is found in the input receive stream, and each newline should result in an append to the /var/tmp/aesdsocketdata file.
*    	You may assume the data stream does not include null characters (therefore can be processed using string handling functions).
*    	You may assume the length of the packet will be shorter than the available heap size.  In other words, as long as you handle malloc() 
*	associated failures with error messages you may discard associated over-length packets.
*     f. Returns the full content of /var/tmp/aesdsocketdata to the client as soon as the received data packet completes.
*    	You may assume the total size of all packets sent (and therefore size of /var/tmp/aesdsocketdata) will be less than the size 
*	of the root filesystem, however you may not assume this total size of all packets sent will be less than the size of the available RAM for the process heap.
*     g. Logs message to the syslog “Closed connection from XXX” where XXX is the IP address of the connected client.
*     h. Restarts accepting connections from new clients forever in a loop until SIGINT or SIGTERM is received (see below).
*     i. Gracefully exits when SIGINT or SIGTERM is received, completing any open connection operations, closing any open sockets, 
*	and deleting the file /var/tmp/aesdsocketdata.
*    	Logs message to the syslog “Caught signal, exiting” when SIGINT or SIGTERM is received.
* Modify your program to support a -d argument which runs the aesdsocket application as a daemon. When in daemon mode the program should fork after ensuring it can bind to port 9000.
*
* MISSING FEATURE:
*	A real program should check if it isn't already running when started, and refuse to continue if so (perhaps by checking if DATA_FILE is already present)
**/

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <syslog.h>
#include <getopt.h>
#include <sys/stat.h> 
#include <fcntl.h>    

// #define DEBUG 

#define PORT "9000"  // the port users will be connecting to
#define BACKLOG 10   // how many pending connections queue will hold
#define DATA_PATH "/var/tmp"
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define SOCKET_ERROR (-1)

int server_socket = -1;  // listen on server_socket
int client_socket = -1;  // new connection on client_socket

FILE *data_file = NULL;	
char *packet_buf = NULL;

// cleanup
void cleanup(const char *msg, const char *s, int exitcode) {

	if (packet_buf)
		free(packet_buf);

// Close server soocket
	if (server_socket != -1) {
		shutdown(server_socket, SHUT_RDWR);
		close(server_socket);
	} 	

// Close client soocket
	if (client_socket != -1) {
		shutdown(client_socket, SHUT_RDWR);
		close(client_socket);
	} 	

// WWrit to syslog
	if (msg) {	
		if (s)
			syslog(LOG_ERR, "%s %s", msg, s);
		else
			syslog(LOG_ERR, "%s", msg);
	}

// Close data file;
	if (data_file)
		fclose(data_file);

// Delete the file
	remove(DATA_FILE);

// WWrit to syslog
	closelog();

// Exit with exit code
	exit(exitcode);
}

// Error handling
void exit_on_error(const char *msg, const char *s) {
	cleanup(msg, s, SOCKET_ERROR);
}

// Signal handler
void handle_signal(int signal) {
	syslog(LOG_INFO, "Caught signal, exiting");
	cleanup(NULL, NULL, 0);
}

// Turns into a daemon process
int daemonize() {
	pid_t pid = fork();
	if (pid < 0) 
		exit_on_error("Failed to fork: %s", strerror(errno));

// Exit if parent 
	if (pid > 0) {
		syslog(LOG_INFO, "Parent exiting");
		closelog();
		exit(0);
	}

// Boring stuff before becoming a daemon
	chdir("/");
	setsid();
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);
	open("/dev/null", O_RDONLY);
	open("/dev/null", O_WRONLY);
	open("/dev/null", O_RDWR);
	syslog(LOG_INFO, "Running as a daemon");
	return 0;
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) 
	    return &(((struct sockaddr_in*)sa)->sin_addr);

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void send_file();

int main(int argc, char *argv[])
{
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage their_addr; // connector's address information
	socklen_t sin_size;
	struct sigaction sa;
	int yes=1;
	char s[INET6_ADDRSTRLEN];
	int rv;
	int opt;
	bool daemonize_flag = false;

// Start syslog
	openlog("aesdsocket", LOG_PID, LOG_USER);
	syslog(LOG_INFO, "Starting");
// Check if deamon flag specified
	while ((opt = getopt(argc, argv, "d")) != -1) {
		switch (opt) {
		case 'd':
			daemonize_flag = true;
			break;
		default:
			fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
			exit_on_error("Invalid parameter supplied", NULL);
		}
	}

// Check presence of /vat/tmp and creates it if not exists
    	if (mkdir(DATA_PATH, 0777) && errno != EEXIST)
		exit_on_error("Cannot create /var/tmp path", strerror(errno));

// Set up signal handler
	sa.sa_handler = handle_signal;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

// Get IP address
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) 
		exit_on_error("getaddrinfo: ", gai_strerror(rv));

// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((server_socket = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			syslog(LOG_ERR, "Failed to create socket: %s", strerror(errno));
			continue;
		}

// Work around ... already in use ... errors
		if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
			freeaddrinfo(servinfo); // all done with this structure
			exit_on_error("setsockopt(SO_REUSEADDR) failed:", strerror(errno));
		}
		if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(int)) == -1) {
			freeaddrinfo(servinfo); // all done with this structure
			exit_on_error("setsockopt(SO_REUSEPORT) failed:", strerror(errno));
		}
// Bind 
		if (bind(server_socket, p->ai_addr, p->ai_addrlen) == -1) {
			close(server_socket);
			syslog(LOG_ERR, "Failed to bind socket: %s", strerror(errno));
			continue;
		}

		break;
	}

// Exit if no address bound
	freeaddrinfo(servinfo); // all done with this structure
	if (p == NULL)  
		exit_on_error("server: failed to bind", NULL);

// Become a daemon if selected
	if (daemonize_flag && daemonize() == -1) 
		exit_on_error("Failed to daemonize", NULL);

// Listen to the socket
	if (listen(server_socket, BACKLOG) == -1) 
		exit_on_error("Failed to listen:", strerror(errno));

// Open file for appending
	data_file = fopen(DATA_FILE, "w+");
	if (data_file == NULL) 
		exit_on_error("Failed to open data file: ", strerror(errno));

// Allocate packet buffer
#define PACKET_BUF_SIZE    (1024+10)
#define PACKET_BUF_EXPAND  (1024)
	if (!(packet_buf = malloc(PACKET_BUF_SIZE * sizeof(char))))
		exit_on_error("Failed to malloc memory: %s", strerror(errno));	
	size_t  packet_buf_allocated = PACKET_BUF_SIZE;	
	size_t  packet_buf_used = 0;
	memset(packet_buf, 0, packet_buf_allocated);	
#ifdef DEBUG
	printf("server: waiting for connections...\n");
#endif
	while(1) {  // main accept() loop
#define RECV_BUF_SIZE (1024)
		char recv_buf[RECV_BUF_SIZE];
		sin_size = sizeof their_addr;

// Accept
		client_socket = accept(server_socket, (struct sockaddr *)&their_addr, &sin_size);
		if (client_socket == -1) 
			exit_on_error("Failed to accept connection:", strerror(errno));

// Print IP
		inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
#ifdef DEBUG
		printf("server: got connection from %s\n", s);
#endif
		syslog(LOG_INFO, "Accepted connection from %s", s);

//Read and send packets main loop
		while (1) {

// read packet
			memset(recv_buf, 0, sizeof(recv_buf));	
			int n = recv(client_socket, recv_buf, sizeof(recv_buf), 0);
			if (n == -1) 
				exit_on_error("Failed to recv data: ", strerror(errno));

			if (n == 0) 
				break;

			if (n > 0) { 

// Copy data to packet buffer
				if (packet_buf_used + n + 1 < packet_buf_allocated) {
					strncat(packet_buf, recv_buf, n);
					packet_buf_used += n;			
			 	} else {

// Resize and copy data to packet buffer
#ifdef DEBUG
					puts("packet_buf too small, allocating");
#endif
					packet_buf_allocated += PACKET_BUF_EXPAND;
					char *new_buffer = (char *)realloc(packet_buf, packet_buf_allocated);
					if (new_buffer == NULL) 
						exit_on_error("Failed to realloc memory: ", strerror(errno));
					packet_buf = new_buffer;

					if (packet_buf_used + n + 1 < packet_buf_allocated) {
						strncat(packet_buf, recv_buf, n);
						packet_buf_used += n;			
				 	} else {
#ifdef DEBUG
						puts("packet_buf really too small, exiting");
#endif
						exit_on_error("packet_buf really too small, exiting", NULL);
					}
				}
#ifdef DEBUG
	 			printf("n = '%d' packet_buf_used = '%ld' strlen(packet_buf) = '%ld' packet_buf_allocated = '%ld'\n", n, packet_buf_used, strlen(packet_buf), packet_buf_allocated);				
				printf("recv_buf = '%s'\n", (n < 128) ? recv_buf : "not printing");			     	
				printf("packet_buf = '%s'\n", (packet_buf_used < 128) ? packet_buf : "not printing");			     	
#endif
// Find if packet complete        
				char *newline = strchr(packet_buf, '\n');	
				if (newline) {
#ifdef DEBUG
					puts("Newline found");
#endif
// Compute packet size
					size_t line_length = newline - packet_buf + 1;
#ifdef DEBUG
					printf("line length: '%ld'\n", line_length);
#endif
// Write packet to file
					size_t written_to_file = fwrite(packet_buf, 1, line_length, data_file);
#ifdef DEBUG
					printf("written_to_file = '%ld' '%ld'\n", written_to_file, line_length);
#endif
					if (written_to_file < line_length && ferror(data_file)) 
		            			exit_on_error("Failed to write data: ", strerror(errno));
					fflush(data_file);

// Decrease memory usage
					if (packet_buf_allocated > PACKET_BUF_SIZE) {					
						packet_buf_allocated = PACKET_BUF_SIZE;	
						char *new_buffer = (char *)realloc(packet_buf, packet_buf_allocated);
						if (new_buffer == NULL) 
							exit_on_error("Failed to shrink memory: ", strerror(errno));
						packet_buf = new_buffer;
					}
					memset(packet_buf, 0, packet_buf_allocated);	
					packet_buf_used = 0;
					send_file();
#ifdef DEBUG
				} else {
					puts("no newline found");
#endif
				}
			}
		}

// Close socket
		shutdown(client_socket, SHUT_RDWR);		
		close(client_socket);
		syslog(LOG_INFO, "Closed connection from %s", s);
	}
	return 0;
}

// Send single packet
size_t send_all(int s, char *buf, size_t len, int flag) {
	size_t total = 0;        // how many bytes we've sent
	size_t bytesleft = len; // how many we have left to send
	size_t n;

 	if (!buf || len < 0)
		return -1;	
	if (len == 0) 
		return 0;

	while(total < len) {
		n = send(s, buf+total, bytesleft, flag);
		if (n == -1) 
			return -1;
		total += n;
		bytesleft -= n;
	}
	return total; 
} 

// Send entire file contents 
void send_file() {
#define SEND_BUF_SIZE 1024
	char send_buf[SEND_BUF_SIZE];
	bool no_more_data = false;
	size_t total_bytes_read = 0;	
	size_t total_bytes_sent = 0;	
	int cur_pos;

// Save file pos ptr	
	cur_pos = ftell(data_file);
	fseek(data_file, 0, SEEK_SET);

	memset(send_buf, 0, sizeof(send_buf));
	while(1) {			  

// read from file
		size_t bytes_read = fread(send_buf, 1, sizeof(send_buf), data_file);
		if (bytes_read < sizeof(send_buf)) {
      			if (ferror(data_file))    /* possibility 1 */
            			exit_on_error("Failed to read data: ", strerror(errno));
			else
				if (feof(data_file))   /* possibility 2 */ 
					no_more_data = true;
		}
		if (bytes_read == 0)
			break;
		total_bytes_read += bytes_read;

// Send
		size_t bytes_sent = send_all(client_socket, send_buf, bytes_read, 0);
                if (bytes_sent == -1) 
            		exit_on_error("Failed to send data: ", strerror(errno));
#ifdef DEBUG
		printf("bytes read '%ld' bytes sent '%ld'\n", bytes_read, bytes_sent);
		printf("send_buf = '%s'\n", (bytes_read < 128) ? send_buf : "not printing");			     					
#endif
		total_bytes_sent += bytes_sent;	
                if (bytes_sent < bytes_read) {
            		syslog(LOG_ERR, "Need to fix the send routine");
#ifdef DEBUG
            		puts("Need to fix the send routine");
#endif
			break;
		}
		if (no_more_data)
			break;
	}
#ifdef DEBUG
	printf("total bytes read '%ld' total bytes sent '%ld'\n", total_bytes_read, total_bytes_sent);
#endif

// Restore file pos ptr	
	fseek(data_file, cur_pos, SEEK_SET);
}
