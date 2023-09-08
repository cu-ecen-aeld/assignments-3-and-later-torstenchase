#include <errno.h>
#include <malloc.h>
#include <netdb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>

static volatile bool run = true;
static void sig_handler(int signum);

int main (int argc, char **argv) 
{
    char dst[INET_ADDRSTRLEN];
    bool dm = false;
    if ((argc == 2) && (strcmp(argv[1], "-d") == 0))
    {
        syslog(LOG_DEBUG, "Will run aesdsocket as daemon");
        dm = true;
    }
    const char filename[] = "/var/tmp/aesdsocketdata";
    char* buf = NULL;
    FILE* writefile = NULL;
    FILE* readfile = NULL;
    int sfd, afd = -1;
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

    pid_t pid = (dm) ? fork() : 0;

    syslog(LOG_ERR, "pid: %d, sfd: %d", pid, sfd);
    if (pid == 0)
    {
        //child or non-daemon process
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

        while (run)
        {
            if ((status = listen(sfd, 10)) != 0)
            {
                syslog(LOG_ERR, "Error listening for connection: %s", strerror(errno));
                goto error;
            }

            struct sockaddr addr;
            socklen_t addrlen = sizeof(struct sockaddr);
            if ((afd = accept(sfd, &addr, &addrlen)) <= 0)
            {
                syslog(LOG_ERR, "Error accepting connection: %s", strerror(errno));
                goto error;
            }

            sin = (struct sockaddr_in*)&addr;
            syslog(LOG_INFO, "Accepted connection from %s:%d", inet_ntop(AF_INET, (struct sockaddr_in *)&addr, dst, sizeof(dst)), ntohs(sin->sin_port));

            writefile = fopen(filename, "a+");
            if (!writefile)
            {
                syslog(LOG_ERR, "Could not open write file: %s", filename);
                goto error;
            }

            
            int sz;
            buf = malloc(0x4000);
            while ((sz = recv(afd, buf, 0x4000, 0)) > 0)
            {
                if (sz < 0) 
                {
                    syslog(LOG_ERR, "Error while waiting for receive data: %s", strerror(errno));
                    goto error;
                }
                if (fwrite(buf, 1, sz, writefile) < 1)
                {
                    syslog(LOG_ERR, "Could not write %s to file", buf);
                    goto error;
                }
                syslog(LOG_DEBUG, "Flushing to file");
                fflush(writefile);

                if (buf[sz-1] == '\n')
                {
                    break;
                }
            }

            free(buf);
            buf = NULL;
            if (fclose(writefile) != 0)
            {
                syslog(LOG_ERR, "Could not close write file: %s", filename);
                writefile = NULL;
                goto error;
            }
            writefile = NULL;

            syslog(LOG_DEBUG, "Opening readfile");
            readfile = fopen(filename, "rb");
            if (!readfile)
            {
                syslog(LOG_ERR, "Could not open read file: %s", filename);
                goto error;
            }

            size_t j;
            while (!feof(readfile))
            {
                int tsz;
                j = 0;
                sz = getline(&buf, &j, readfile);
                if (sz < 0)
                {
                    syslog(LOG_DEBUG, "Could not get another line from file %s, error: %s", filename, strerror(errno));
                }
                else
                {
                    syslog(LOG_DEBUG, "Writing %d characters: %s to socket", sz, buf);
                    tsz = send(afd, buf, sz, 0);
                    if (tsz != sz)
                    {
                        syslog(LOG_ERR, "Buffer length mismatch #3");
                        goto error;
                    }
                    syslog(LOG_DEBUG, "Done Writing %d characters: %s to socket", sz, buf);
                }
                free(buf);
                buf = NULL;
            }
            syslog(LOG_INFO, "Found EOF in %s", filename);
            if (fclose(readfile) != 0)
            {
                syslog(LOG_ERR, "Could not close read file: %s", filename);
                readfile = NULL;
                goto error;
            }
            readfile = NULL;

            shutdown(afd, SHUT_RDWR);
            close(afd);
            sin = (struct sockaddr_in*)&addr;
            syslog(LOG_INFO, "Closed connection from %s:%d", inet_ntop(AF_INET, (struct sockaddr_in *)&addr, dst, sizeof(dst)), ntohs(sin->sin_port));
        }

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
    if (buf)
    {
        free(buf);
    }
    if (afd != -1)
    {
        shutdown(afd, SHUT_RDWR);
        close(afd);
    }
    if (sfd != -1)
    {
        shutdown(sfd, SHUT_RDWR);
        close(sfd);
    }
    if (readfile)
    {
        fclose(readfile);
    }
    if (writefile)
    {
        fclose(writefile);
    }
    closelog();
    return -1;
}

static void sig_handler(int signum)
{
    syslog(LOG_INFO, "Caught signal, exiting");
    run = false;
}
