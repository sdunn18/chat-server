/*
** server.c --> a stream server demo from beej.us guide
*/

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
#include <poll.h>

#define PORT "3490" //the port users will be connecting to

#define MAXDATASIZE 200
#define MAXCONNECTIONS 10 
#define BACKLOG 10 //how many pending connections the queue will hold

struct client
{
    int fd;
    char *name;
};

int get_listener_socket(void); //get a socket to listen on
void sigchld_handler(int s)
{
    //waitpid() might overwrite errno, so we save and restore it
    int saved_errno = errno;

    while(waitpid(-1, NULL, WNOHANG) > 0)
        ;
    errno = saved_errno;
}

//get sockadr, IPV4, or IPV6
void *get_in_addr(struct sockaddr *sa)
{
    if (sa ->sa_family == AF_INET)
    {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int get_listener_socket(void)
{
    int listener;
    int yes=1;
    int rv;

    struct addrinfo hints, *ai, *p;

    memset(&hints,0,sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if((rv = getaddrinfo(NULL,PORT,&hints,&ai)) != 0){
        fprintf(stderr,"select server: %s \n", gai_strerror(rv));
        exit(1);
    }
    for(p = ai; p != NULL; p = p->ai_next)
    {
        listener = socket(p->ai_family, p->ai_socktype,p->ai_protocol);
        if(listener < 0)
            continue;
        
        setsockopt(listener, SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(int));

        if(bind(listener, p->ai_addr,p->ai_addrlen) < 0)
        {
            close(listener);
            continue;
        }
        break;
    }

    if(p == NULL)
        return -1; //didn't bind

    freeaddrinfo(ai);

    if(listen(listener, 10) == -1)
        return -1;

    return listener;
}


void add_to_pfds(struct pollfd *pfds[], int newfd, int *fd_count, int *fd_size)
{
    //if no more room, make some
    if (*fd_count == *fd_size){
        *fd_size *= 2;
        *pfds = realloc(*pfds, sizeof (**pfds) * *fd_size);
    }

    (*pfds)[*fd_count].fd = newfd;
    (*pfds)[*fd_count].events = POLLIN;

    (*fd_count)++;
}

void del_from_pfds(struct pollfd pfds[], int i, int *fd_count)
{
    pfds[i] = pfds[*fd_count-1]; //copy end to the place being removed
    (*fd_count)--;
}

int main(void)
{
    int listener, new_fd; //listen on sock_fd, new connection on new_fd
    struct sockaddr_storage their_addr; //connectors addr info
    socklen_t addr_len;
    struct sigaction sa;
    char s[INET6_ADDRSTRLEN];
    
    struct client clients[MAXCONNECTIONS]; //store client name and fd 
    int rv;

    
    
    int fd_count = 0;
    int fd_size = 5;
    struct pollfd *pfds = malloc(sizeof *pfds * fd_size);
    
    listener = get_listener_socket();

    if (listener == -1){
        fprintf(stderr,"error getting listening socket \n");

    }
    pfds[0].fd = listener;
    pfds[0].events = POLLIN;
    fd_count = 1; //count listener slot
    printf("server: waiting for connections .... \n");

    char buf[MAXDATASIZE];

    for(;;) //main accept() loop
    {

        int poll_count = poll(pfds, fd_count,-1);
        printf("%d events \n", poll_count);
        if(poll_count == -1){
            perror("poll:");
            exit(1);
        }
        // no error, run through the fields, checking to see which ones are ready
        for(int i = 0; i < fd_count; i++)
        {
            //check if data ready to be read

            if(pfds[i].revents & POLLIN)
            {
                printf("event happening, field %d ", i);
                if(pfds[i].fd == listener){
                    printf("event on listener");
                    //its the listener, accept the new connection
                    addr_len = sizeof their_addr;
                    new_fd = accept(listener, (struct sockaddr *)&their_addr,&addr_len);
                
                    if(new_fd == -1){
                        perror("accept");
                    }
                    else{
                        add_to_pfds(&pfds, new_fd, &fd_count, &fd_size);
                        printf("new connection from %s",
                        inet_ntop(their_addr.ss_family,get_in_addr((struct sockaddr*)&their_addr),s,INET6_ADDRSTRLEN));
                       /*
                        //get client name, store in an array of clients
                        int clen;
                        rv = recv(new_fd, &clen,4,0);
                        char name[clen];
                        rv = recv(new_fd,name,clen,0);
                        struct client newclient = *(struct client *) malloc(sizeof (struct client));
                        newclient.fd = new_fd;
                        newclient.name = name;
                        //fd_count starts at 1, adds to pfds also increments, must move back by 2.
                        clients[fd_count -2] = newclient; 
                        
                        */
                    }

                }
                else
                {

                    //not on the listening socket, check for info being sent from connected clients
                    int nbytes = recv(pfds[i].fd,buf, sizeof buf, 0);
                    int sender = pfds[i].fd;

                    if( nbytes <= 0){
                        //there was an error
                        if(nbytes == 0){
                            //connection was closed
                            printf("%d sender hung up \n",sender);
                        }
                        else{
                            perror("recv");
                        }
                        del_from_pfds(pfds,i,&fd_count);
                        //remove from clients
                        clients[i-1] = clients[fd_count -1];
                        close(sender);
                    }
                    else{
                        //real data was sent!
                        //first get who to send to
                        //data is first parsed by client so
                        int recp = buf[0];
                        printf("recepients %d", recp);
                        if (recp == 0){
                             for(i = 1; i < recp; i++){
                                 if (pfds[i].fd != sender)
                                    send(pfds[i].fd,buf,nbytes ,0);
                            }
                        }
                        else{
                          for(i = 1; i < recp; i++){
                              //later filter to not include sender
                                send(pfds[i].fd,buf,nbytes ,0);
                            }
                        }
                    }
                }
                
            }
        }
       
    }



    return 0;
}