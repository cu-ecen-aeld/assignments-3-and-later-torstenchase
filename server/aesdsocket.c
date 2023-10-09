#include <errno.h>
#include <malloc.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "queue.h"

static volatile bool run = true;
static void sig_handler(int signum);
static void* receive_send_thread(void* arg);
static void timer_thread (union sigval sigval);

const char filename[] = "/var/tmp/aesdsocketdata";

typedef struct slist_data_s slist_data_t;
struct slist_data_s {
    int fd;
    pthread_t thread;
    pthread_mutex_t* mutex;
    bool complete;
    SLIST_ENTRY(slist_data_s) entries;
};
SLIST_HEAD(slisthead, slist_data_s) head;


int main (int argc, char **argv) 
{
    char dst[INET_ADDRSTRLEN];
    bool dm = false;
    int rc;
    
    if ((argc == 2) && (strcmp(argv[1], "-d") == 0))
    {
        syslog(LOG_DEBUG, "Will run aesdsocket as daemon");
        dm = true;
    }
    int sfd = -1;
    syslog(LOG_DEBUG, "Running aesdsocket");
    openlog(NULL, 0, LOG_USER);

    int status;
    sfd = socket(PF_INET, SOCK_STREAM, 0);
    if (sfd == -1)
    {
        syslog(LOG_ERR, "Error creating socket: %s", strerror(errno));
        goto error;
    }
    const int enable = 1;
    if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
    {
        syslog(LOG_ERR, "setsockopt(SO_REUSEADDR) failed");
        goto error;
    }
    struct addrinfo hints;
    struct addrinfo* res = NULL;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if ((status = getaddrinfo(NULL, "9000", &hints, &res)) != 0)
    {
        syslog(LOG_ERR, "Error getting addr info: %s", gai_strerror(status));
        goto error;
    }

    struct sockaddr_in* sin = (struct sockaddr_in*)res->ai_addr;
    syslog (LOG_DEBUG, "sockaddr: fam:%d, addr:%s:%d", 
        sin->sin_family, 
        inet_ntop(AF_INET, (struct sockaddr_in *)res->ai_addr, dst, sizeof(dst)),
        ntohs(sin->sin_port)
        );

    if ((status = bind(sfd, res->ai_addr, sizeof(struct sockaddr))) != 0)
    {
        syslog(LOG_ERR, "Error binding socket: %s", strerror(errno));
        freeaddrinfo(res);
        goto error;
    }
    freeaddrinfo(res);

    SLIST_INIT(&head);

    pid_t pid = (dm) ? fork() : 0;

    syslog(LOG_INFO, "pid: %d, sfd: %d", pid, sfd);
    if (pid == 0)
    {
        //child or non-daemon process
        slist_data_t* datap = NULL;
        slist_data_t* tempp = NULL;
        struct sigaction act;
        memset(&act, 0, sizeof(struct sigaction));
        act.sa_handler = sig_handler;
        if (sigaction(SIGTERM, &act, NULL) != 0)
        {
            syslog(LOG_ERR, "Failed registering for SIGTERM: %s", strerror(errno));
            goto error;
        }
        if (sigaction(SIGINT, &act, NULL) != 0)
        {
            syslog(LOG_ERR, "Failed registering for SIGINT: %s", strerror(errno));
            goto error;
        }

        pthread_mutex_t mutex;
        if ((rc = pthread_mutex_init(&mutex, NULL)) != 0)
        {
            syslog(LOG_ERR, "Failed to initialize mutex: %d", rc);
            goto error;
        }

        struct sigevent sev;
        timer_t timerid;
        memset(&sev, 0, sizeof(struct sigevent));
        sev.sigev_notify = SIGEV_THREAD;
        sev.sigev_value.sival_ptr = &mutex;
        sev.sigev_notify_function = timer_thread;
        if (timer_create(CLOCK_MONOTONIC, &sev, &timerid) != 0)
        {
            syslog(LOG_ERR, "Failed to create timer: %d", errno);
            goto error;
        }
        struct itimerspec its;
        its.it_interval.tv_sec = 10;
        its.it_interval.tv_nsec = 0;
        its.it_value.tv_sec = 10;
        its.it_value.tv_nsec = 0;
        if (timer_settime(timerid, TIMER_ABSTIME, &its, NULL) != 0)
        {
            syslog(LOG_ERR, "Failed to set timer: %d", errno);
            timer_delete(timerid);
            goto error;
        }

        while (run)
        {      
            int afd;
            if ((status = listen(sfd, 10)) != 0)
            {
                syslog(LOG_ERR, "Error listening for connection: %s", strerror(errno));
                timer_delete(timerid);
                goto error;
            }
            else
            {
                struct sockaddr addr;
                socklen_t addrlen = sizeof(struct sockaddr);
                if ((afd = accept(sfd, &addr, &addrlen)) <= 0)
                {
                    syslog(LOG_ERR, "Error accepting connection: %s", strerror(errno));
                    timer_delete(timerid);
                    goto error;
                }
                else
                {
                    sin = (struct sockaddr_in*)&addr;
                    syslog(LOG_INFO, "Accepted connection from %s:%d", inet_ntop(AF_INET, (struct sockaddr_in *)&addr, dst, sizeof(dst)), ntohs(sin->sin_port));

                    datap = malloc(sizeof(slist_data_t));
                    if (!datap)
                    {
                        syslog(LOG_ERR, "Could not allocate memory for list entry");
                        timer_delete(timerid);
                        shutdown(afd, SHUT_RDWR);
                        close(afd);
                        goto error;
                    }
                    datap->fd = afd;
                    datap->mutex = &mutex;
                    datap->complete = false;
                    rc = pthread_create(&datap->thread, 
                                            NULL,
                                            receive_send_thread,
                                            datap);
                    if (rc != 0)
                    {
                        syslog(LOG_ERR, "Could not create thread");
                        free(datap);
                        timer_delete(timerid);
                        shutdown(afd, SHUT_RDWR);
                        close(afd);
                        goto error;
                    }
                    SLIST_INSERT_HEAD(&head, datap, entries);

                    SLIST_FOREACH_SAFE(datap, &head, entries, tempp)
                    {
                        if (datap->complete)
                        {
                            pthread_join(datap->thread, NULL);
                            shutdown(datap->fd, SHUT_RDWR);
                            close(datap->fd);
                            syslog(LOG_INFO, "Closed connection");
                            SLIST_REMOVE(&head, datap, slist_data_s, entries);
                            free(datap);
                        }
                    }
                }
            }
        }
        SLIST_FOREACH_SAFE(datap, &head, entries, tempp)
        {
            pthread_join(datap->thread, NULL);
            shutdown(datap->fd, SHUT_RDWR);
            close(datap->fd);
            syslog(LOG_INFO, "Closed connection");
            SLIST_REMOVE(&head, datap, slist_data_s, entries);
            free(datap);
        }
        timer_delete(timerid);
        shutdown(sfd, SHUT_RDWR);
        close(sfd);
        remove(filename);
        closelog();
        exit(EXIT_SUCCESS);
    }
    else if (pid > 0)
    {
        exit(EXIT_SUCCESS);
    }
    else
    {
        exit(EXIT_FAILURE);
    }

error:
    remove(filename);
    if (sfd != -1)
    {
        shutdown(sfd, SHUT_RDWR);
        close(sfd);
    }
    closelog();
    status = EXIT_FAILURE;
    return -1;
}

static void* receive_send_thread(void* arg)
{
    slist_data_t* datap = (slist_data_t*)arg;
    FILE* file = NULL;
    char* buf = NULL;

    syslog(LOG_DEBUG, "Opening writefile");
    file = fopen(filename, "a+");
    if (!file)
    {
        syslog(LOG_ERR, "Could not open write file: %s", filename);
        goto error;
    }

    
    int sz, rc;
    buf = malloc(0x4000);
    while ((sz = recv(datap->fd, buf, 0x4000, 0)) > 0)
    {
        if (sz < 0) 
        {
            syslog(LOG_ERR, "Error while waiting for receive data: %s", strerror(errno));
            goto error;
        }

        syslog(LOG_DEBUG, "Read %d characters: %s from socket", sz, buf);
        rc = pthread_mutex_lock(datap->mutex);
        if (rc != 0)
        {
            syslog(LOG_ERR, "Error locking mutex: %d", rc);
        }
        else
        {
            if (fwrite(buf, 1, sz, file) < 1)
            {
                syslog(LOG_ERR, "Could not write %s to file", buf);
                pthread_mutex_unlock(datap->mutex);
                goto error;
            }
            fflush(file);
            rc = pthread_mutex_unlock(datap->mutex);
            if (rc != 0)
            {
                syslog(LOG_ERR, "Error unlocking mutex: %d", rc);
            }
            if (buf[sz-1] == '\n')
            {
                break;
            }
        }
    }
    free(buf);
    buf = NULL;

    if (fclose(file) != 0)
    {
        syslog(LOG_ERR, "Could not close write file: %s", filename);
        file = NULL;
        goto error;
    }

    syslog(LOG_DEBUG, "Opening readfile");
    file = fopen(filename, "rb");
    if (!file)
    {
        syslog(LOG_ERR, "Could not open read file: %s", filename);
        goto error;
    }

    size_t j;
    while (!feof(file))
    {
        int tsz;
        j = 0;
        sz = getline(&buf, &j, file);
        if (sz < 0)
        {
            syslog(LOG_DEBUG, "Could not get another line from file %s, error: %s", filename, strerror(errno));
        }
        else
        {
            syslog(LOG_DEBUG, "Writing %d characters: %s to socket", sz, buf);
            tsz = send(datap->fd, buf, sz, 0);
            if (tsz != sz)
            {
                syslog(LOG_ERR, "Buffer length mismatch #3");
                goto error;
            }
            syslog(LOG_DEBUG, "Done writing %d characters: %s to socket", sz, buf);
        }
        free(buf);
        buf = NULL;
    }
    syslog(LOG_INFO, "Found EOF in %s", filename);
    if (fclose(file) != 0)
    {
        syslog(LOG_ERR, "Could not close read file: %s", filename);
    }
    file = NULL;
error:
    if (buf)
    {
        free(buf);
    }
    if (file)
    {
        fclose(file);
    }
    datap->complete = true;
    return NULL;
}

static void timer_thread(union sigval sigval)
{
    pthread_mutex_t* mutex = (pthread_mutex_t*)sigval.sival_ptr;
    char buf[200];
    FILE* file = NULL;

    syslog(LOG_DEBUG, "Opening writefile");
    file = fopen(filename, "a+");
    if (!file)
    {
        syslog(LOG_ERR, "Could not open write file: %s", filename);
        goto error;
    }

    sprintf(buf, "timestamp:");
    time_t t;
    struct tm* tmp;

    t = time(NULL);
    tmp = localtime(&t);
    if(!tmp)
    {
        syslog(LOG_ERR, "Error getting time");
        goto error;
    }
    if (strftime(&buf[10], sizeof(buf) - 10, "%a, %d %b %Y %T %z\n", tmp) == 0) 
    {
        syslog(LOG_ERR, "Error strftime");
        goto error;
    }
        
    size_t sz = strlen(buf);
    syslog(LOG_DEBUG, "Writing %ld characters: %s to file", sz, buf);
    int rc = pthread_mutex_lock(mutex);
    if (rc != 0)
    {
        syslog(LOG_ERR, "Error locking mutex: %d", rc);
        goto error;
    }
    else
    {
        if (fwrite(buf, 1, strlen(buf), file) < 1)
        {
            syslog(LOG_ERR, "Could not write %s to file", buf);
            pthread_mutex_unlock(mutex);
            goto error;
        }
        fflush(file);
        rc = pthread_mutex_unlock(mutex);
        if (rc != 0)
        {
            syslog(LOG_ERR, "Error unlocking mutex: %d", rc);
        }
    }

    if (fclose(file) != 0)
    {
        syslog(LOG_ERR, "Could not close read file: %s", filename);
    }
    file = NULL;
error:
    if (file)
    {
        fclose(file);
    }
}


static void sig_handler(int signum)
{
    syslog(LOG_INFO, "Caught signal, exiting");
    run = false;
}
