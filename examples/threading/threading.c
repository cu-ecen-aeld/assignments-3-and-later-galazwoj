#define _GNU_SOURCE
#include "threading.h"
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

//#define DEBUG 1

// Optional: use these functions to add debug or error prints to your application
#ifdef DEBUG
#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)
#else
#define DEBUG_LOG(msg,...)
#define ERROR_LOG(msg,...)
#endif

/**
* 1. Start a thread which sleeps @param wait_to_obtain_ms number of milliseconds, 
*	then obtains the mutex in @param mutex, 
*	then holds for @param wait_to_release_ms milliseconds, 
*	then releases.
* 2. The thread started should return a pointer to the thread_data structure when it exits, which can be used to free memory as well as to check thread_complete_success for successful exit.	
*/

void* threadfunc(void* thread_param)
{

    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    int ret;
    bool locked = false;
#ifdef DEBUG
    pid_t tid = gettid();
#endif
    DEBUG_LOG("thread '%d' started", tid);

    sleep(thread_func_args->wait_to_obtain_ms/1000);
    DEBUG_LOG("thread '%d' slept awaiting", tid);

    ret = pthread_mutex_lock(thread_func_args->mutex);
    if (ret) {
	errno = ret;
	perror("pthread_mutex_lock");
        ERROR_LOG("thread '%d' mutex not acquired", tid);
    } else {
	locked = true;
    	DEBUG_LOG("thread '%d' mutex acquired", tid);
    }
	
    sleep(thread_func_args->wait_to_release_ms/1000);
    DEBUG_LOG("thread '%d' slept releasing", tid);
    
    if(locked) {
	    ret = pthread_mutex_unlock(thread_func_args->mutex);
	    if (ret) {
		errno = ret;
		perror("pthread_mutex_unlock");
	    	ERROR_LOG("thread '%d' mutex not released", tid);
	    } else {
	    	DEBUG_LOG("thread '%d' mutex released", tid);
	    }	
    } else	
    	DEBUG_LOG("thread '%d' mutex release skipped", tid);

    thread_func_args->thread_complete_success = true;
    DEBUG_LOG("thread '%d' finished", tid);
    return thread_param;
}

/**
* 1. Start a thread which parameters
	@param wait_to_obtain_ms number of milliseconds, 
*	@param mutex, 
*	@param wait_to_release_ms milliseconds, 
* 2. The start_thread_obtaining_mutex function should only start the thread and should not block for the thread to complete.
* 3. The start_thread_obtaining_mutex function should use dynamic memory allocation for thread_data structure passed into the thread.  
* 4. The number of threads active should be limited only by the amount of available memory.
* 6. If a thread was started succesfully @param thread should be filled with the pthread_create thread ID coresponding to the thread which was started.
* 7. @return true if the thread could be started, false if a failure occurred.
*/

bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */

    int ret;
    struct thread_data *td;

    DEBUG_LOG("malloc");
    if (!(td = (struct thread_data *)malloc(sizeof(struct thread_data)))) {	
	perror("malloc");
        ERROR_LOG("malloc failed");
	return false;
    }

    td->wait_to_obtain_ms = wait_to_obtain_ms;
    td->mutex = mutex;
    td->wait_to_release_ms = wait_to_release_ms;
    td->thread_complete_success = false;

    DEBUG_LOG("pthread_create call");
    ret = pthread_create (thread, NULL, threadfunc, td);
    if (ret) {
	errno = ret;
	perror("pthread_create");
        ERROR_LOG("pthread_create failed");
	return false;
    }

    DEBUG_LOG("pthread_create success");
    return true;
}
