/**
* Continuation of Assignment 5:
* 1. Modify your socket based program to accept multiple simultaneous connections, with each connection spawning a new thread to handle the connection.
*	a. Writes to /var/tmp/aesdsocketdata should be synchronized between threads using a mutex, to ensure data written by synchronous connections is not intermixed, 
*		and not relying on any file system synchronization.
*		i. For instance, if one connection writes �12345678� and another connection writes �abcdefg� it should not be possible 
*			for the resulting /var/tmp/aesdsocketdata file to contain a mix like �123abcdefg456�, the content should always be �12345678�, followed by �abcdefg�.  
*			However for any simultaneous connections, it's acceptable to allow packet writes to occur in any order in the socketdata file.
*	b. The thread should exit when the connection is closed by the client or when an error occurs in the send or receive steps.
*	c. Your program should continue to gracefully exit when SIGTERM/SIGINT is received, after requesting an exit from each thread and waiting for threads to complete execution.
*	d. Use the singly linked list APIs discussed in the video (or your own implementation if you prefer) to manage threads.
*		i. Use pthread_join() to join completed threads, do not use detached threads for this assignment.
* 
* Validation:
* 1. The automated test environment should pass when running against your assignment implementation.
* 2. You should not have any memory leaks or errors identified when running valgrind.
* 5. Modify your socket server application developed in assignments 5 and 6 to support and use a build switch USE_AESD_CHAR_DEVICE, set to 1 by default, which:
*        a. Redirects reads and writes to /dev/aesdchar instead of /var/tmp/aesdsocketdata
*        b. Removes timestamp printing.
*        c. Ensure you do not remove the  /dev/aesdchar endpoint after exiting the aesdsocket application.
*
*	[X] added USE_AESD_CHAR_DEVICE logic added
*	[X] some debug stuff removed
*	[X] USE_BUFFERED_IO logic, ie unbuffered i/o added
*	[X] USE_FILE_MUTEX logic added 
*	[X] #define debug helpers
*	[X] timer removed
*	[X] file open/close per thread
*	[X] cleanup() logic changed
*
*5.  Add special handling for socket write commands to your aesdsocket server application
*        a. When the string sent over the socket equals AESDCHAR_IOCSEEKTO:X,Y where X and Y are unsigned decimal integer values, 
*	   the X should be considered the write command to seek into and the Y should be considered the offset within the write command.  
*        b. These values should be sent to your driver using the AESDCHAR_IOCSEEKTO ioctl described earlier.  The ioctl command should be performed before any additional writes to your aesdchar device.
*        c. Do not write this string command into the aesdchar device as you do with other strings sent to the socket.
*        d. Send the content of the aesdchar device back over the socket, as you do with any other string received over the socket interface.  We will use this to test the seek command.
*        e. Ensure the read of the file and return over the socket uses the same (not closed and re-opened) file descriptor used to send the ioctl, to ensure your file offset is honored for the read command.
* 
***/
#define _GNU_SOURCE
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
#include <pthread.h>
#include <sys/queue.h>

#define AESD_DEBUG 
#define AESD_DEBUG_PACKET 

#define USE_BUFFERED_IO 1
//#define USE_FILE_MUTEX
#define USE_AESD_CHAR_DEVICE 1

#ifdef USE_AESD_CHAR_DEVICE 
#include <sys/ioctl.h>
#include "../aesd-char-driver/aesd_ioctl.h"
#endif

#ifndef USE_AESD_CHAR_DEVICE 
#define USE_FILE_MUTEX
#endif

#ifdef AESD_DEBUG
#define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#else
#define PDEBUG(fmt, args...) /* not debugging: nothing */
#endif

#ifdef AESD_DEBUG_PACKET 
#define PPDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#else
#define PPDEBUG(fmt, args...) /* not debugging: nothing */
#endif

#define PORT "9000"  // the port users will be connecting to
#define BACKLOG 10   // how many pending connections queue will hold
#ifdef USE_AESD_CHAR_DEVICE
#define DATA_FILE "/dev/aesdchar"
#else
#define DATA_PATH "/var/tmp"
#define DATA_FILE "/var/tmp/aesdsocketdata"
#endif
#define SOCKET_ERROR (-1)

#ifndef  gettid
// glibc from aarm64 buildroot does not support this
pid_t gettid(void) { return 1; }
#endif

volatile int running = false;					// thread loop running ?

#ifdef USE_FILE_MUTEX
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER; 	// file mutex
#endif

struct thread_params {
	int client_socket;			// new connection on client_socket
	char client_address[INET6_ADDRSTRLEN];	// client IP address
	bool finished;             		// is thread finished
};

struct thread_entry {
	pthread_t thread;			// thread_id
	bool joined;                    	// if thread joined
	struct thread_params *params;        	// thread params
	SLIST_ENTRY(thread_entry) entries;
};

SLIST_HEAD(thread_list, thread_entry) threads;	// thread pool

// Signal handler
void handle_signal(int signal) {
	syslog(LOG_INFO, "Caught signal, exiting");
	running = false;
}

// Turns into a daemon process
int daemonize() {
	pid_t pid = fork();
	if (pid < 0) {
		syslog(LOG_ERR, "Failed to fork: %s", strerror(errno));
		return -1;
	}
// Exit if parent 
	if (pid > 0) {
		syslog(LOG_INFO, "Parent exiting");
		closelog();
		exit(0);
	}

// Boring stuff before becoming a daemon
	chdir("/");
	umask(0);
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

// Set signal handlers
void setup_signal_handlers() {
	struct sigaction sa;

	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = handle_signal;
	sigaction(SIGINT,  &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
}

// Create server socket, terminate if failed
int create_server_socket() {
	struct addrinfo hints, *servinfo, *p;
	int rv;
	int yes=1;
	int server_socket = -1; 		

// Get IP address
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) { 
		syslog(LOG_ERR, "getaddrinfo: %s", gai_strerror(rv));
		return -1;
	}
// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((server_socket = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			syslog(LOG_ERR, "Failed to create socket: %s", strerror(errno));
			continue;
		}

// Work around ... already in use ... errors
		if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
			freeaddrinfo(servinfo); // all done with this structure
			syslog(LOG_ERR, "setsockopt(SO_REUSEADDR) failed: %s", strerror(errno));
			return -1;
		}
		if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(int)) == -1) {
			freeaddrinfo(servinfo); // all done with this structure
			syslog(LOG_ERR, "setsockopt(SO_REUSEPORT) failed: %s", strerror(errno));
			return -1;
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
	if (!p) {
		syslog(LOG_ERR, "server: service not available");
		return -1;
	}
	return server_socket;
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
	if (sa->sa_family == AF_INET) 
		return &(((struct sockaddr_in*)sa)->sin_addr);
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

#ifdef USE_AESD_CHAR_DEVICE
int is_char_device(const char *path)
{
    	struct stat path_stat;
    	stat(path, &path_stat);
    	return S_ISCHR(path_stat.st_mode);
}
#endif

void *connection_thread(void *args);

int main(int argc, char *argv[]) {
	int server_socket = -1; 			// listen on server_socket
	int opt;
	bool daemonize_flag = false;
	bool error = true;

// Start syslog
	openlog("aesdsocket", LOG_PID, LOG_USER);
	syslog(LOG_INFO, "Starting");
	SLIST_INIT(&threads);

// Check if deamon flag specified
	while ((opt = getopt(argc, argv, "d")) != -1) {
		switch (opt) {
		case 'd':
			daemonize_flag = true;
			break;
		default:
			fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
			syslog(LOG_INFO,"Invalid parameter supplied");
			goto error_invalid_parameter;
		}
	}

#ifdef USE_AESD_CHAR_DEVICE
// Check presence of /dev/aesdchar and abort if not exists
	if(!is_char_device(DATA_FILE)) {
		fprintf(stderr, DATA_FILE " does not exist, exiting\n");
		syslog(LOG_ERR, DATA_FILE " does not exist, exiting" );
		goto error_path_not_found;
	}
#else
// Check presence of /vat/tmp and create it if not exists
    	if (mkdir(DATA_PATH, 0777) && errno != EEXIST) {
		syslog(LOG_ERR, "Cannot create /var/tmp path %s", strerror(errno));
		goto error_path_not_found;
	}

// Delete stale data file
	remove(DATA_FILE);
#endif
// Set up signal handlers
	setup_signal_handlers();

// Crrate server socket and fork
	if ((server_socket = create_server_socket()) == -1) {
		goto error_socket;
	}

// Become a daemon if selected
	if (daemonize_flag && daemonize() == -1) {
		syslog(LOG_ERR, "Failed to daemonize");
		goto error_cannot_fork;
	}

// Listen to the socket
	if (listen(server_socket, BACKLOG) == -1) {
		syslog(LOG_ERR, "Failed to listen: %s", strerror(errno));
		goto error_cannot_listen;
	}

	PDEBUG("server: waiting for connections...\n");
	running = true;
	while(running) {  // main accept() loop
		struct sockaddr_storage their_addr; // connector's address information
		socklen_t sin_size = sizeof their_addr;
		pthread_t thread_id;
		struct thread_entry *curr;
// Accept
		int client_socket = accept(server_socket, (struct sockaddr *)&their_addr, &sin_size);
		if (client_socket == -1) {
			if (errno != EINTR) 
				syslog(LOG_ERR, "Failed to accept connection: %s", strerror(errno));
			goto error_cannot_accept;
		}

// Fill in thread params
		struct thread_params *params = malloc(sizeof(struct thread_params));
		if (!params) {
			syslog(LOG_ERR, "thread_params malloc %s", strerror(errno));
			goto error_malloc_thread_params;
		}
		params->client_socket = client_socket;

// Get IP address of the client
		inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), params->client_address, sizeof(params->client_address));
		params->finished = false;

		struct thread_entry *new_thread = malloc(sizeof(struct thread_entry));
		if (!new_thread) {
			syslog(LOG_ERR, "thread_entry malloc %s", strerror(errno));
			free(params);
			goto error_malloc_thread_entry;
		}
		if (pthread_create(&thread_id, NULL, connection_thread, params) < 0) {
			syslog(LOG_ERR, "pthread_create %s", strerror(errno));
			goto error_pthread_create;
		}

		new_thread->thread = thread_id;
		new_thread->joined = false;
		new_thread->params = params;
		SLIST_INSERT_HEAD(&threads, new_thread, entries);

// Join exited threads 
		SLIST_FOREACH(curr, &threads, entries) {
			if (!curr->joined && curr->params->finished) {
				if (pthread_join(curr->thread, NULL) < 0) 
					syslog(LOG_ERR, "pthread_join failed, ignoring ...");
				curr->joined = true;
				free(curr->params);
			}
		}
	} /* while() */
	error = false;

error_pthread_create:
error_malloc_thread_entry:
error_malloc_thread_params:
error_cannot_accept:

#ifndef USE_AESD_CHAR_DEVICE
// Delete the file
	remove(DATA_FILE);
#else
	error = error;
#endif

// Join all threads to finish
	struct thread_entry *curr;
	SLIST_FOREACH(curr, &threads, entries) {
		if (!curr->joined) {
			if (pthread_join(curr->thread, NULL) < 0) 
				syslog(LOG_ERR, "pthread_join failed, ignoring ...");
			curr->joined = true;
			free(curr->params);
		}
	}

// Remover elements from list
	while (!SLIST_EMPTY(&threads)) {
		curr = SLIST_FIRST(&threads);
		SLIST_REMOVE_HEAD(&threads, entries);
		free(curr);
	}


error_cannot_listen:
error_cannot_fork:
// Close server soocket
	if (server_socket != -1) {
		shutdown(server_socket, SHUT_RDWR);
		close(server_socket);
	}
error_socket:
error_path_not_found:
error_invalid_parameter:
// Close syslog
	closelog();

// Exit with exit code
	exit(error ? SOCKET_ERROR : 0);
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
#ifdef USE_BUFFERED_IO
size_t send_file(int client_socket, FILE * data_file, bool read_from_zero) {
#else
size_t send_file(int client_socket, int data_file, bool read_from_zero) {
#endif

#define SEND_BUF_SIZE 1024
	char send_buf[SEND_BUF_SIZE];
	bool no_more_data = false;
	size_t total_bytes_read = 0;
	size_t total_bytes_sent = 0;
	bool error = false;
	if (client_socket == -1) {
		syslog(LOG_ERR, "No client socket in send file");
		error = true;
		goto error_bad_parameters;
	}

#ifdef USE_FILE_MUTEX
	pthread_mutex_lock(&file_mutex);
#endif

// Save file pos ptr
#ifdef USE_BUFFERED_IO
	int cur_pos = ftell(data_file);
	if (read_from_zero)
		fseek(data_file, 0, SEEK_SET);
#else
	int cur_pos = lseek(data_file, 0, SEEK_CUR);
	if (read_from_zero)
		lseek(data_file, 0, SEEK_SET);
#endif

	memset(send_buf, 0, sizeof(send_buf));
	while(1) {

// read from file
#ifdef USE_BUFFERED_IO
		size_t bytes_read = fread(send_buf, 1, sizeof(send_buf), data_file);
#else
		size_t bytes_read = read(data_file, send_buf, sizeof(send_buf));
#endif
		if (bytes_read < sizeof(send_buf)) {
#ifdef USE_BUFFERED_IO
      			if (ferror(data_file)) {   
            			syslog(LOG_ERR, "Failed to read data: %s", strerror(errno));
				error = true;
				break;
			} 
			else
				if (feof(data_file))   
					no_more_data = true;
#else
			if (bytes_read == -1) {
            			syslog(LOG_ERR, "Failed to read data: %s", strerror(errno));
				error = true;
				break;
			} 
#endif
		}

		if (bytes_read == 0)
			break;
		total_bytes_read += bytes_read;

// Send 
		size_t bytes_sent = send_all(client_socket, send_buf, bytes_read, 0);
                if (bytes_sent == -1) {
            		syslog(LOG_ERR, "Failed to send data: %s", strerror(errno));
			error = true;
			break;
		} 

		PPDEBUG("bytes read '%ld' bytes sent '%ld'\n", bytes_read, bytes_sent);
		PPDEBUG("send_buf = '%s'\n", (bytes_read < 128) ? send_buf : "not printing");
		total_bytes_sent += bytes_sent;	
                if (bytes_sent < bytes_read) {
            		syslog(LOG_ERR, "Need to fix the send routine");
            		PDEBUG("Need to fix the send routine\n");
			error = true;
			break;
		}
		if (no_more_data)
			break;
	}
	PPDEBUG("total bytes read '%ld' total bytes sent '%ld'\n", total_bytes_read, total_bytes_sent);

// Restore file pos ptr	
#ifdef USE_BUFFERED_IO
	fseek(data_file, cur_pos, SEEK_SET);
#else
	lseek(data_file, cur_pos, SEEK_SET);
#endif

#ifdef USE_FILE_MUTEX
	pthread_mutex_unlock(&file_mutex);
#endif
error_bad_parameters:
	return (error) ? -1 : total_bytes_sent;
}

#ifdef USE_AESD_CHAR_DEVICE
/***
 * @return 
 * 	 1 found ioctl msg, do not write the packet_buf to file
 *	 0 not found ioctl msg, so write the packet_buf to file
 *     	-1 found ioctl msg, failure occured, terminate the program
 */
int handle_ioctl_write_xommand(int fd, char *packet_buf) {
	const char ioctl_msg[] = "AESDCHAR_IOCSEEKTO:";
	int ioctl_n = strlen(ioctl_msg);
	char *xy_pos;
	int result;
	struct aesd_seekto seek_to;
// empty string
	if (!packet_buf) {
       		PDEBUG("handle_ioctl_write_command 1\n");		
		return 0;	
	}
// no substring present
	if (!(xy_pos = strstr(packet_buf, ioctl_msg))) {
       		PDEBUG("handle_ioctl_write_command 2\n");
		return 0;	
	}
// no data for ioctl present - string too short
	PDEBUG("handle_ioctl_write_command sizes (%d, %lu)\n", ioctl_n +4, strlen(packet_buf));
	if (ioctl_n + 4 < strlen(packet_buf)) {
       		PDEBUG("handle_ioctl_write_xommand 3\n");
		return 0;	
	}
	xy_pos += ioctl_n;
// no data for ioctl present - string too short
	if(strlen(xy_pos) < 2) {
       		PDEBUG("handle_ioctl_write_command 4\n");
		return 0;			
	}
// read ioctl command arguments
	result = sscanf(xy_pos, "%d,%d", &seek_to.write_cmd, &seek_to.write_cmd_offset);
	if (result == -1) {
       		PDEBUG("handle_ioctl_write_command 5\n");
		return -1;		
	}
// number of arguments too little or too much
	if (result < 2 || result > 2) {
       		PDEBUG("handle_ioctl_write_command 6\n");
		return 0;		
	}
// perform call
	PDEBUG("handle_ioctl_write_command: (%u, %u )\n", seek_to.write_cmd, seek_to.write_cmd_offset);
	if (ioctl(fd, AESDCHAR_IOCSEEKTO, &seek_to) < 0) {
       		PDEBUG("handle_ioctl_write_command 7\n");
		syslog(LOG_ERR,"Failed to perform ioctl: %s", strerror(errno));
		return -1;
	} 
	PDEBUG("handle_ioctl_write_command success\n");
	return 1;	
}
#endif

// Recv / send thread loop
void *connection_thread(void *args) {
	bool error = false;
	struct thread_params *params = (struct thread_params*)args;
	pid_t tid  = gettid();
	if (!params) {
		syslog(LOG_ERR,"Null parameters in connection thread %d", tid);
		error = true;
		goto error_null_params;
	}
	int client_socket = params->client_socket;
	if (client_socket == -1) {
		syslog(LOG_ERR, "No client socket in connection thread %d", tid);
		error = true;
		goto error_bad_socket;
	}
// Print IP addrerss
	PDEBUG("server: got connection from %s, thread %d\n", params->client_address, tid);
	syslog(LOG_INFO, "Accepted connection from %s, thread %d", params->client_address, tid);

// Open file for writing
#ifdef USE_AESD_CHAR_DEVICE
#ifdef USE_BUFFERED_IO
	FILE *data_file = fopen(DATA_FILE, "w+");
#else
	int data_file = open(DATA_FILE, O_RDWR);
#endif
#else
#ifdef USE_BUFFERED_IO
	FILE *data_file = fopen(DATA_FILE, "a+");
#else
	int data_file = open(DATA_FILE, O_CREAT|O_RDWR|O_APPEND, S_IRUSR|S_IWUSR);  
#endif
#endif

#ifdef USE_BUFFERED_IO
	if (!data_file) { 
#else
	if (data_file == -1) { 
#endif
		syslog(LOG_ERR,"No open file in connection thread");
		error = true;
		goto error_bad_file;
	} 

#ifndef USE_AESD_CHAR_DEVICE
// Set file pos at the end (by default it is set at the beginning)
#ifdef USE_BUFFERED_IO
	fseek(data_file, 0, SEEK_END);
#else
	lseek(data_file, 0, SEEK_END);
#endif
#endif

// Allocate packet buffer if empty 
#define PACKET_BUF_SIZE    (1024+10)
#define PACKET_BUF_EXPAND  (1024)
	char  *packet_buf;
	size_t packet_buf_allocated = PACKET_BUF_SIZE;	
	if (!(packet_buf = malloc(packet_buf_allocated))) {
		syslog(LOG_ERR, "Failed to malloc memory: %s", strerror(errno));
		error = true;
		goto error_packet_malloc;
	} 

	size_t  packet_buf_used = 0;
	memset(packet_buf, 0, packet_buf_allocated);

#define RECV_BUF_SIZE (1024)
	char recv_buf[RECV_BUF_SIZE];

// Read and send packets main loop
	while (1) {

// read packet
		memset(recv_buf, 0, sizeof(recv_buf));	
		int n = recv(client_socket, recv_buf, sizeof(recv_buf), 0);
		if (n == -1) {
			syslog(LOG_ERR,"Failed to recv data: %s", strerror(errno));
			error = true;
			goto error_packet_recv;
		}

		if (n == 0) 
			break;

		if (n > 0) { 

// Resize and copy data to packet buffer
			if (packet_buf_used + n + 1 >= packet_buf_allocated) {
				PPDEBUG("packet_buf too small, allocating\n");
				packet_buf_allocated += PACKET_BUF_EXPAND;
				char *new_buffer = (char *)realloc(packet_buf, packet_buf_allocated);
				if (new_buffer == NULL) {
					syslog(LOG_ERR, "Failed to realloc memory: %s", strerror(errno));
					error = true;
					goto error_packet_realloc;
				}
				packet_buf = new_buffer;
			}

// Buffer still too small
			if (packet_buf_used + n + 1 >= packet_buf_allocated) {
				PPDEBUG("packet_buf really too small, exiting\n");
				syslog(LOG_ERR, "Packet_buf really too small, exiting");
				error = true;
				goto error_packet_too_small;
			}

// Copy data to packet buffer
			strncat(packet_buf, recv_buf, n);
			packet_buf_used += n;
			PPDEBUG("n = '%d' packet_buf_used = '%ld' strlen(packet_buf) = '%ld' packet_buf_allocated = '%ld'\n", n, packet_buf_used, strlen(packet_buf), packet_buf_allocated);				
			PPDEBUG("recv_buf = '%s'\n", (n < 128) ? recv_buf : "not printing");
			PPDEBUG("packet_buf = '%s'\n", (packet_buf_used < 128) ? packet_buf : "not printing");
// Find if packet complete
			char *newline = strchr(packet_buf, '\n');
			if (newline) {
				bool read_from_zero = true;
				PDEBUG("Newline found\n");
// Compute packet size
				size_t line_length = newline - packet_buf + 1;
				
				PPDEBUG("line length: '%ld'\n", line_length);
// handle ioctl 				
#ifdef USE_AESD_CHAR_DEVICE
#ifdef USE_BUFFERED_IO
				int ioctl_result = handle_ioctl_write_xommand(fileno(data_file), packet_buf);
#else
				int ioctl_result = handle_ioctl_write_xommand(data_file, packet_buf);
#endif
				if(ioctl_result == -1) {
					error = true;
					goto error_file_ioctl;
				}  				
// set not to seek file from zero
				if(ioctl_result == 1) {			
					read_from_zero = false;
					goto writing_skipped;
				}
#endif                       

// write to file				
#ifdef USE_FILE_MUTEX
// Write packet to file
				pthread_mutex_lock(&file_mutex);
#endif

#ifdef USE_BUFFERED_IO
				size_t written_to_file = fwrite(packet_buf, 1, line_length, data_file);
				fflush(data_file);
#else
				size_t written_to_file = write(data_file, packet_buf, line_length);
#endif
#ifdef USE_FILE_MUTEX
				pthread_mutex_unlock(&file_mutex);
#endif
				PPDEBUG("written_to_file = '%ld' '%ld'\n", written_to_file, line_length);
#ifdef USE_BUFFERED_IO
				if (written_to_file < line_length && ferror(data_file)) {
#else
				if (written_to_file == -1) {
#endif
	            			syslog(LOG_ERR, "Failed to write data: %s", strerror(errno));
					error = true;
					goto error_file_write;
				}

writing_skipped:
// send file
				if (send_file(client_socket, data_file, read_from_zero) == -1) {
					error = true;
					goto error_packet_send;
				}

// Decrease memory usage
				if (packet_buf_allocated > PACKET_BUF_SIZE) {
					packet_buf_allocated = PACKET_BUF_SIZE;	
					char *new_buffer = (char *)realloc(packet_buf, packet_buf_allocated);
					if (new_buffer == NULL) {
						syslog(LOG_ERR, "Failed to shrink memory: %s", strerror(errno));
						error = true;
						goto error_packet_shrink;
					}
					packet_buf = new_buffer;
				}
				memset(packet_buf, 0, packet_buf_allocated);
				packet_buf_used = 0;
			} else 
				PDEBUG("no newline found\n");
		}
	}

error_packet_shrink:
error_packet_send:
error_file_write:
error_file_ioctl:
error_packet_too_small:
error_packet_realloc:
error_packet_recv:
// Free packet bnuffer
	free(packet_buf);

error_packet_malloc:
#ifdef USE_BUFFERED_IO
	fclose(data_file);
#else
	close(data_file);
#endif

error_bad_file:
// Close socket
	shutdown(client_socket, SHUT_RDWR);
	close(client_socket);
	params->client_socket = -1;

// Print IP address
	PDEBUG("Closed connection from %s. thread %d\n", params->client_address, tid);
	syslog(LOG_INFO, "Closed connection from %s, thread %d", params->client_address, tid);
	memset(params->client_address, 0, sizeof(params->client_address));

error_bad_socket:
error_null_params:
	if (error)
		running = false;

// Mark thread comp;eted
	params->finished = true;
	return params;
}

