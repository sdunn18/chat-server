/*
** server.c --> a server using pthreads instead of poll
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
#include <ctype.h>
#include <assert.h>
#include <pthread.h>

#define PORT "3490" //the port users will be connecting to

#define MAXDATASIZE 200
#define MAXCONNECTIONS 10 
#define BACKLOG 10 //how many pending connections the queue will hold
#define MAXNAME 20
#define NO 0
#define YES 1
struct client
{
    int fd;
    char *name;
};

void trimleading(char *);
struct client *addclient(int);
void *m_connection(void *);

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


struct client *clients[MAXCONNECTIONS]; //store client name and fd 
int c_count;
pthread_mutex_t message_mut;

int main(void)
{

    printf("3490 \n");
    int listener, new_fd; //listen on sock_fd, new connection on new_fd
    struct sockaddr_storage their_addr; //connectors addr info
    socklen_t addr_len;
    struct sigaction sa;
    char s[INET6_ADDRSTRLEN];
    
    int rv;
    int clen;
    char name[MAXNAME];
    pthread_t tids[MAXCONNECTIONS]; //need threads to constantly monitor connections

    listener = get_listener_socket();

    if (listener == -1){
        fprintf(stderr,"error getting listening socket \n");

    }
    //main thread now listens for connections
    while(1){

        addr_len = sizeof their_addr;
        new_fd = accept(listener, (struct sockaddr *)&their_addr,&addr_len);
        if(new_fd == -1){
            perror("accept:");
            continue;
        }
        //now create a new thread with the client
        //first generate the new client
        struct client *newclient = addclient(new_fd);
        clients[c_count] = newclient;
        pthread_create(&tids[c_count],0,m_connection,newclient);
        c_count++;

    }

    //after all this is done, make sure to rejoin the threads?

    
    
    return 0;
}

void *m_connection(void *vp){
    //check to see if any messages are being sent by the client, if so, parse and send them.
    struct client *client = (struct client*)vp;
    int rv;
    int sender = client ->fd;
    char *sname = client -> name;
    printf("new client: %s, %d \n", sname,sender);
    free(vp);
    short nrecp = -1;
    
    char message[MAXDATASIZE + MAXNAME + 2]; //2 extra inserted characters ": "

    while(1){           
        int nbytes = recv(sender,&nrecp, sizeof(short), 0);  
        printf("nrecp: %d \n",nrecp);
        if( nbytes <= 0){
            //there was an error
            if(nbytes == 0){
                //connection was closed
                printf("%d sender hung up \n",sender);
            }
            else{
                perror("recv");
            }
            close(sender);
            return 0;
        }
        else if(nrecp >= 0 && nrecp < 10){
            memset(&message[0], 0, sizeof(message)); //clear the message for usage
            pthread_mutex_lock(&message_mut);
            //real data was sent!                
            //data is first parsed by client so
            //lock the message maybe?
            int recp[10];
            char name[20];
            short bytes_recv;
            int found,j;
            for(j = 0; j < nrecp; j++)
            {
                //collect the recepients
                bytes_recv = recv(sender, &name, sizeof name,0 );
                //check if name is in clients list
                int k;
                for(k = 0; k < c_count; k++)
                {
                    if(strcmp(name, clients[k] -> name) == 0){
                        recp[j] = k;            
                        found = YES; 
                    }                       
                                
                
                    if(found == NO){
                        recp[j] = -1; // that person isnt online                    }
                    }
                }
            }
            strcat(message,sname);
            strcat(message, ": ");
            char recieved[200];
            bytes_recv = recv(sender, recieved, MAXDATASIZE,0);
            printf("recieved: %s \n", recieved);
            if(bytes_recv < 0)
                perror("recv:");
            trimleading(recieved);
            if(strcmp(recieved,"") != 0){
                printf("got a real message %s \n", recieved);

                //make sure it wasn't an empty message
                strcat(message, recieved);      
                if(nrecp == 0){
                    //send to everyone
                    for(j = 0; j < c_count ; j++)
                    {
                        if(clients[j] -> fd != sender){
                         //   printf("sent to %s \n", clients[j]->name);
                            rv = send(clients[j] ->fd, message, sizeof(message), 0);
                            if( rv < 0){
                                perror("send:");
                            }
                        }
                    }
                }
                else{                               
                    //send to recepients collected earlier
                    for(j = 0; j < nrecp; j++)
                    {
                        //send out to the intended recepients
                        if(recp[j] == -1){
                            char *msg = "Someone you are tring to reach is not online right now";
                            send(sender,msg, sizeof msg, 0 );
                        }else{
                            send(clients[recp[j]] -> fd, message, sizeof message, 0);
                        }
                    }
                }
                            
            }
            pthread_mutex_unlock(&message_mut);
        }   
    }
    return 0;
}

struct client * addclient(int socket){
    
    struct client *newclient =(struct client *) malloc(sizeof (struct client));
    int rv,clen;
    
    rv = recv(socket, &clen,4,0);
    char name[clen+1];
    assert(rv == 4);
    rv = recv(socket,name,clen,0);
    assert(rv == clen);
    name[clen] = '\0';
    newclient ->name = strdup(name);
    newclient ->fd = socket;
    memset(&name[0],0,sizeof name);

    return newclient;

}
//trim leading white space
void trimleading(char *s){
    while(isspace(*s))
        s++;
}
