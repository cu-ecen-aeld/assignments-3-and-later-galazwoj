/**
* Continuation of Assignment 5:
* 1. Modify your socket based program to accept multiple simultaneous connections, with each connection spawning a new thread to handle the connection.
*	a. Writes to /var/tmp/aesdsocketdata should be synchronized between threads using a mutex, to ensure data written by synchronous connections is not intermixed, 
*		and not relying on any file system synchronization.
*		i. For instance, if one connection writes “12345678” and another connection writes “abcdefg” it should not be possible 
*			for the resulting /var/tmp/aesdsocketdata file to contain a mix like “123abcdefg456”, the content should always be “12345678”, followed by “abcdefg”.  
*			However for any simultaneous connections, it's acceptable to allow packet writes to occur in any order in the socketdata file.
*	b. The thread should exit when the connection is closed by the client or when an error occurs in the send or receive steps.
*	c. Your program should continue to gracefully exit when SIGTERM/SIGINT is received, after requesting an exit from each thread and waiting for threads to complete execution.
*	d. Use the singly linked list APIs discussed in the video (or your own implementation if you prefer) to manage threads.
*		i. Use pthread_join() to join completed threads, do not use detached threads for this assignment.
* 2. Modify your aesdsocket source code repository to:
*	a. Append a timestamp in the form “timestamp:time” where time is specified by the RFC 2822 compliant strftime format*, followed by newline.  
*		This string should be appended to the /var/tmp/aesdsocketdata file every 10 seconds, where the string includes the year, month, day, hour 
*		(in 24 hour format) minute and second representing the system wall clock time.
*	b. Use appropriate locking to ensure the timestamp is written atomically with respect to socket data
*	 Hint: 
*		Think where should the timer be initialized. Should it be initialized in parent or child?
* 3. Use the updated sockettest.sh script (in the assignment-autotest/test/assignment6 subdirectory) . You can run this manually outside the `./full-test.sh` script by:
*	a. Starting your aesdsocket application
*	b. Executing the sockettest.sh script from the assignment-autotest subdirectory.
*	c. Stopping your aesdsocket application.
* 4. The `./full-test.sh` script in your aesd-assignments repository should now complete successfully.
* 5. Tag the assignment with “assignment-<assignment number>-complete” once the final commit is pushed onto your repository. The instructions to add a tag can be found here
* 
* Validation:
* 1. The automated test environment should pass when running against your assignment implementation.
* 2. You should not have any memory leaks or errors identified when running valgrind.
*	a. You may see one possibly lost message which looks like this, which it’s OK to ignore:
* 
*		272 bytes in 1 blocks are possibly lost in loss record 1 of 2
*		==6362==    at 0x4C33B25: calloc (in /usr/lib/valgrind/vgpreload_memcheck-amd64-linux.so)
*		==6362==    by 0x4013646: allocate_dtv (dl-tls.c:286)
*		==6362==    by 0x4013646: _dl_allocate_tls (dl-tls.c:530)
n		==6362==    by 0x504E227: allocate_stack (allocatestack.c:627)
*		==6362==    by 0x504E227: pthread_create@@GLIBC_2.2.5 (pthread_create.c:644)
*		==6362==    by 0x4E4351A: __start_helper_thread (timer_routines.c:176)
*		==6362==    by 0x5055906: __pthread_once_slow (pthread_once.c:116)
*		==6362==    by 0x4E423BA: timer_create@@GLIBC_2.3.3 (timer_create.c:101)
*		==6362==    by 0x10A157: init_timer_sec (aesdsocket.c:222)
*		==6362==    by 0x10995B: main (aesdsocket.c:88)
* 
* https://sourceware.org/bugzilla/show_bug.cgi?id=29705
* Bug 29705 - libc timer_create with option SIGEV_THREAD system call having memory leaks after timer_delete system call 
* Adhemerval Zanella 2022-10-20 16:45:37 UTC
*	This is by design, Linux timer_crate/SIGEV_THREAD creates a helper detached thread on the first usage that acts as an activator for the callbacks.  
*	The timer_delete removes the timer from the internal list, the background thread is kept so a next timer_create do not need to recreate it.
*
*	This is done as an optimization: it trades some contention on accessing the internal list (so the helper thread can find and execute the timer callback) 
*	with the need to create/destroy a thread for each timer invocation.  I expect that for a limited number of timers it should be way faster 
*	(although thread creation is somewhat fast, it still has some overhead and issues a bunch of syscalls), 
*	I am not sure if the number of configured signals it still yields better performance.
*
* 5. Modify your socket server application developed in assignments 5 and 6 to support and use a build switch USE_AESD_CHAR_DEVICE, set to 1 by default, which:
*        a. Redirects reads and writes to /dev/aesdchar instead of /var/tmp/aesdsocketdata
*        b. Removes timestamp printing.
*        c. Ensure you do not remove the  /dev/aesdchar endpoint after exiting the aesdsocket application.
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

#define DEBUG 
#define DEBUG_PACKET 
#define USE_AESD_CHAR_DEVICE 1

#define PORT "9000"  // the port users will be connecting to
#define BACKLOG 10   // how many pending connections queue will hold
#ifdef  USE_AESD_CHAR_DEVICE
#define DATA_FILE "/dev/aesdchar"
#else
#define DATA_PATH "/var/tmp"
#define DATA_FILE "/var/tmp/aesdsocketdata"
#endif
#define SOCKET_ERROR (-1)

int server_socket = -1;  // listen on server_socket

pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER; 	// file mutex
#ifndef  USE_AESD_CHAR_DEVICE
timer_t timer_id;			                      	// timer id
#endif

struct thread_params {
	int client_socket;			// new connection on client_socket
	char client_address[INET6_ADDRSTRLEN];	// client IP address
	FILE *data_file;			// data file
	bool finished;             		// is thread finished
};

struct thread_entry {
	pthread_t thread;			// thread_id
	bool joined;                    	// if thread joined
	struct thread_params *params;        	// thread params
	SLIST_ENTRY(thread_entry) entries;
};

SLIST_HEAD(thread_list, thread_entry) threads;	// thread pool

// cleanup
void cleanup(const char *msg, const char *s, int exitcode) {
	static bool exit_in_progress = false;
	if (exit_in_progress) {
		syslog(LOG_ERR, "already in  cleanup, ignoring ...");
		return;
	}
	exit_in_progress = true;
	bool data_file_opened = true;	// assimg DATA_FILE open

#ifndef  USE_AESD_CHAR_DEVICE
// Delete timer 
	timer_delete(timer_id);
#endif

// Join all threads to finish
	struct thread_entry *curr;
	SLIST_FOREACH(curr, &threads, entries) {
		if (!curr->joined) {
			if (pthread_join(curr->thread, NULL) < 0) 
				syslog(LOG_ERR, "pthread_join failed, ignoring ...");
			curr->joined = true;

// Close data file. This funny code is because there is purposedly no globsal variable pointing to data_file
			if (data_file_opened && curr->params->data_file) {
				fclose(curr->params->data_file);
				data_file_opened = false;
			}
// packet buffer is closed by the thread itself so no check is made to free it here
// client_socket is closed by the thread itself so no check is made to close it here
			free(curr->params);
		}
	}

// Remover elements from list
	while (!SLIST_EMPTY(&threads)) {
		curr = SLIST_FIRST(&threads);
		SLIST_REMOVE_HEAD(&threads, entries);
		free(curr);
	}

// Close server soocket
	if (server_socket != -1) {
		shutdown(server_socket, SHUT_RDWR);
		close(server_socket);
	}

// WWrite to syslog
	if (msg) {
		if (s)
			syslog(LOG_ERR, "%s %s", msg, s);
		else
			syslog(LOG_ERR, "%s", msg);
	}

// Just in case
	pthread_mutex_unlock(&file_mutex);

#ifndef  USE_AESD_CHAR_DEVICE
// Delete the file
	remove(DATA_FILE);
#endif
// Close syslog
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
		exit_on_error("Failed to fork:", strerror(errno));

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

#ifndef  USE_AESD_CHAR_DEVICE
// Write timestamp to a file
void timer_thread(union sigval arg) {
	char msg[100];
	time_t current_time;
	struct tm *local_time;

        FILE* file = (FILE *)arg.sival_ptr;
	if (!file) 
		exit_on_error(" no open file in timer thread:", strerror(errno));

	current_time = time(NULL);
	if ((local_time = localtime(&current_time)) == NULL) 
		exit_on_error("localtime in timer thread:", strerror(errno));

	if (strftime(msg, sizeof(msg), "timestamp: %F %T\n", local_time) == 0) 
		exit_on_error("strftime in timer thread:", strerror(errno));

	pthread_mutex_lock(&file_mutex);
	if (!file) 
		exit_on_error(" no open file in timer thread:", strerror(errno));
	else
		fputs(msg, file);

	if (!file) 
		exit_on_error(" no open file in timer thread:", strerror(errno));
	else
	        fflush(file);
	pthread_mutex_unlock(&file_mutex);
}

// Create timer that executes every n seconds
// based on LSP p 392-3
void create_timer(unsigned n, FILE *file)
{
	struct itimerspec ts;
	struct sigevent se;

	se.sigev_notify = SIGEV_THREAD;
	se.sigev_value.sival_ptr = file; 
	se.sigev_notify_function = timer_thread;
	se.sigev_notify_attributes = NULL;

	ts.it_value.tv_sec = n;
	ts.it_value.tv_nsec = 0;
	ts.it_interval.tv_sec = n;  
	ts.it_interval.tv_nsec = 0;

	if(timer_create(CLOCK_REALTIME, &se, &timer_id) == -1) 
		exit_on_error("Create timer", NULL);

	if(timer_settime(timer_id, 0, &ts, 0) == -1) 
		exit_on_error("Set timer", NULL);
}
#endif

// Create server socket, terminate if failed
void create_server_socket() {
	struct addrinfo hints, *servinfo, *p;
	int rv;
	int yes=1;

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
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
	if (sa->sa_family == AF_INET) 
		return &(((struct sockaddr_in*)sa)->sin_addr);
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

#ifdef  USE_AESD_CHAR_DEVICE
int is_char_device(const char *path)
{
    struct stat path_stat;
    stat(path, &path_stat);
    return S_ISCHR(path_stat.st_mode);
}
#endif

void *connection_thread(void *args);

int main(int argc, char *argv[]) {
	int opt;
	bool daemonize_flag = false;

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
			exit_on_error("Invalid parameter supplied", NULL);
		}
	}

#ifdef  USE_AESD_CHAR_DEVICE
// Check presence of /dev/aesdchar and abort if not exists
	if(!is_char_device(DATA_FILE)) {
		fprintf(stderr, DATA_FILE " does not exist, exiting\n");
		exit_on_error(DATA_FILE " does not exist, exiting", NULL);
	}
#else
// Check presence of /vat/tmp and create it if not exists
    	if (mkdir(DATA_PATH, 0777) && errno != EEXIST)
		exit_on_error("Cannot create /var/tmp path", strerror(errno));

// Delete stale data file
	remove(DATA_FILE);
#endif
// Set up signal handlers
	setup_signal_handlers();

// Crrate server socket and fork
	create_server_socket();

// Become a daemon if selected
	if (daemonize_flag && daemonize() == -1) 
		exit_on_error("Failed to daemonize", NULL);

// Listen to the socket
	if (listen(server_socket, BACKLOG) == -1) 
		exit_on_error("Failed to listen:", strerror(errno));

// Open file for writing
	FILE *data_file = fopen(DATA_FILE, "w+");
	if (data_file == NULL) {
		exit_on_error("Failed to open data file: ", strerror(errno));
	}

// Create timer thread
#ifndef  USE_AESD_CHAR_DEVICE
#define TIMER_PERIOD 10 
	create_timer(TIMER_PERIOD, data_file);
#endif
#ifdef DEBUG
	printf("server: waiting for connections...\n");
#endif
	while(1) {  // main accept() loop
		struct sockaddr_storage their_addr; // connector's address information
		socklen_t sin_size = sizeof their_addr;
		pthread_t thread_id;
		struct thread_entry *curr;
// Accept
		int client_socket = accept(server_socket, (struct sockaddr *)&their_addr, &sin_size);
		if (client_socket == -1) 
			exit_on_error("Failed to accept connection:", strerror(errno));

// Fill in thread params
		struct thread_params *params = malloc(sizeof(struct thread_params));
		params->client_socket = client_socket;

// Get IP address of the client
		inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), params->client_address, sizeof(params->client_address));

// Set data file handle
		params->data_file = data_file;
		params->finished = false;

		if (pthread_create(&thread_id, NULL, connection_thread, params) < 0) 
			exit_on_error("pthread_create", strerror(errno));

		struct thread_entry *new_thread = malloc(sizeof(struct thread_entry));
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
	}
	/* notreached*/
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
void send_file(int client_socket, FILE * data_file) {
#define SEND_BUF_SIZE 1024
	char send_buf[SEND_BUF_SIZE];
	bool no_more_data = false;
	size_t total_bytes_read = 0;
	size_t total_bytes_sent = 0;
	pthread_mutex_lock(&file_mutex);

#ifndef  USE_AESD_CHAR_DEVICE
// Save file pos ptr
	if (!data_file) 
		exit_on_error(" no open file in send file 1:", strerror(errno));

	int cur_pos = ftell(data_file);
	fseek(data_file, 0, SEEK_SET);
#endif
	memset(send_buf, 0, sizeof(send_buf));
	while(1) {

// read from file
		if (!data_file) 
			exit_on_error(" no open file in send file 2:", strerror(errno));

		size_t bytes_read = fread(send_buf, 1, sizeof(send_buf), data_file);
		if (bytes_read < sizeof(send_buf)) {
      			if (ferror(data_file))    
            			exit_on_error("Failed to read data: ", strerror(errno));
			else
				if (feof(data_file))   
					no_more_data = true;
		}
		if (bytes_read == 0)
			break;
		total_bytes_read += bytes_read;

// Send 
		size_t bytes_sent = send_all(client_socket, send_buf, bytes_read, 0);
                if (bytes_sent == -1) 
            		exit_on_error("Failed to send data: ", strerror(errno));
#ifdef DEBUG_PACKET
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
#ifdef DEBUG_PACKET
	printf("total bytes read '%ld' total bytes sent '%ld'\n", total_bytes_read, total_bytes_sent);
#endif

#ifndef  USE_AESD_CHAR_DEVICE
// Restore file pos ptr	
	if (!data_file) 
		exit_on_error(" no open file in send file 3:", strerror(errno));

	fseek(data_file, cur_pos, SEEK_SET);
#endif
	pthread_mutex_unlock(&file_mutex);
}

// Recv / send thread loop
void *connection_thread(void *args) {
	struct thread_params *params = (struct thread_params*)args;
// Print IP addrerss
#ifdef DEBUG
	pid_t tid  = gettid();
	printf("server: got connection from %s. thread %d\n", params->client_address, tid);
#endif
	syslog(LOG_INFO, "Accepted connection from %s", params->client_address);

	int    client_socket = params->client_socket;
	FILE  *data_file     = params->data_file;

// Allocate packet buffer if empty 
#define PACKET_BUF_SIZE    (1024+10)
#define PACKET_BUF_EXPAND  (1024)
	char  *packet_buf;
	size_t packet_buf_allocated = PACKET_BUF_SIZE;	
	if (!(packet_buf = malloc(packet_buf_allocated)))
		exit_on_error("Failed to malloc memory:", strerror(errno));

	size_t  packet_buf_used = 0;
	memset(packet_buf, 0, packet_buf_allocated);

#define RECV_BUF_SIZE (1024)
	char recv_buf[RECV_BUF_SIZE];

// Read and send packets main loop
	while (1) {

// read packet
		memset(recv_buf, 0, sizeof(recv_buf));	
		int n = recv(client_socket, recv_buf, sizeof(recv_buf), 0);
		if (n == -1) 
			exit_on_error("Failed to recv data:", strerror(errno));

		if (n == 0) 
			break;

		if (n > 0) { 

// Resize and copy data to packet buffer
			if (packet_buf_used + n + 1 >= packet_buf_allocated) {
#ifdef DEBUG
				puts("packet_buf too small, allocating");
#endif
				packet_buf_allocated += PACKET_BUF_EXPAND;
				char *new_buffer = (char *)realloc(packet_buf, packet_buf_allocated);
				if (new_buffer == NULL) 
					exit_on_error("Failed to realloc memory:", strerror(errno));
				packet_buf = new_buffer;
			}

// Buffer still too small
			if (packet_buf_used + n + 1 >= packet_buf_allocated) {
#ifdef DEBUG
				puts("packet_buf really too small, exiting");
#endif
				exit_on_error("packet_buf really too small, exiting", NULL);
			}

// Copy data to packet buffer
			strncat(packet_buf, recv_buf, n);
			packet_buf_used += n;
#ifdef DEBUG_PACKET
			printf("n = '%d' packet_buf_used = '%ld' strlen(packet_buf) = '%ld' packet_buf_allocated = '%ld'\n", n, packet_buf_used, strlen(packet_buf), packet_buf_allocated);				
			printf("recv_buf = '%s'\n", (n < 128) ? recv_buf : "not printing");
			printf("packet_buf = '%s'\n", (packet_buf_used < 128) ? packet_buf : "not printing");			     	
#endif
// Find if packet complete
			char *newline = strchr(packet_buf, '\n');
			if (newline) {
#ifdef DEBUG_PACKET
				puts("Newline found");
#endif
// Compute packet size
				size_t line_length = newline - packet_buf + 1;
#ifdef DEBUG_PACKET
				printf("line length: '%ld'\n", line_length);
#endif
//	 Write packet to file
				pthread_mutex_lock(&file_mutex);

				if (!data_file) 
					exit_on_error(" no open file in connection thread:", strerror(errno));

				size_t written_to_file = fwrite(packet_buf, 1, line_length, data_file);
				fflush(data_file);
				pthread_mutex_unlock(&file_mutex);
#ifdef DEBUG_PACKET
				printf("written_to_file = '%ld' '%ld'\n", written_to_file, line_length);
#endif
				if (written_to_file < line_length && ferror(data_file)) 
	            			exit_on_error("Failed to write data: ", strerror(errno));

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
				send_file(client_socket, data_file);
#ifdef DEBUG_PACKET
			} else {
				puts("no newline found");
#endif
			}
		}
	}

// Close socket
	shutdown(client_socket, SHUT_RDWR);
	close(client_socket);
	params->client_socket = -1;

// Free packet bnuffer
	free(packet_buf);

// Print IP address
#ifdef DEBUG
	printf("Closed connection from %s. thread %d\n", params->client_address, tid);
#endif
	syslog(LOG_INFO, "Closed connection from %s", params->client_address);
	memset(params->client_address, 0, sizeof(params->client_address));

// Mark thread comp;eted
	params->finished = true;
	return params;
}
