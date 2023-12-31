#include "threading.h"
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{

    // wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    int rc;
    
    rc = usleep(thread_func_args->wait_to_obtain_ms * 1000);
    if (rc != 0)
    {
        ERROR_LOG ("Failed to sleep, error: %d", rc);
        goto error;
    }

    rc = pthread_mutex_lock(thread_func_args->mutex);
    if (rc != 0)
    {
        ERROR_LOG ("Failed to lock mutex, error: %d", rc);
        goto error;
    }

    rc = usleep(thread_func_args->wait_to_release_ms * 1000);
    if (rc != 0)
    {
        ERROR_LOG ("Failed to sleep, error: %d", rc);
        pthread_mutex_unlock(thread_func_args->mutex);
        goto error;
    }

    rc = pthread_mutex_unlock(thread_func_args->mutex);
    if (rc != 0)
    {
        ERROR_LOG ("Failed to unlock mutex, error: %d", rc);
        goto error;
    }
    
    thread_func_args->thread_complete_success = true;
error:
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */
    struct thread_data* pthread_data;
    int rc;
    if (!thread || !mutex)
    {
        printf ("No thread or mutex pointers defined");
        return false;
    }

    pthread_data = malloc(sizeof(*pthread_data));
    if (!pthread_data)
    {
        printf ("Failed to allocate memory, error: %d", errno);
        return false;
    }

    *pthread_data = (struct thread_data) {
        mutex,
        wait_to_obtain_ms,
        wait_to_release_ms,
        false,
    };

    rc = pthread_create(thread, NULL, threadfunc, (void*)pthread_data);
    if (rc != 0)
    {
        printf ("Failed to create thread, error: %d", rc);
        goto error; 
    }

    return true;

error:
    free(pthread_data);
    return false;
}

