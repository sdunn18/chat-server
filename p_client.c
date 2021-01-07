
/*
*client.c -- enter a name, connect to simple chat server. Modifed to use threads
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <ctype.h>
#include <assert.h>
#include <pthread.h>
#include <arpa/inet.h>

#define PORT "3490" //the port client will connect to
#define MAXNAME 20 //max number of chars in name
#define MAXDATASIZE 100 //max number of bytes that can be recieved at once
#define MAXIN 200 //max input size
#define MAXRECP 10 // max number of people one can @
void login(char *name);
int parseinput(char *, char *, char [][MAXNAME]);
void *getinput(void *vp);
void *getmessages(void *vp);
void trimleading(char *); //trim leading white space
int formatmessage(char *, char *, char [][MAXNAME],int); //format message to be sent out, return length

int sockfd;
//get sockaddr
void *get_in_addr(struct sockaddr *sa)
{
    if(sa->sa_family == AF_INET)
    {
        return &(((struct sockaddr_in *)sa) -> sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}



int main(int argc, char *argv[])
{

    int numbytes;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    char s[INET6_ADDRSTRLEN];

    //first make sure usage is correct
    if(argc != 2){
        fprintf(stderr, "usage: client hostname \n");
        exit(1);
    }
    
    //then get user login
    char name[MAXNAME]; //max name size
    login(name);


    //then try to connect to the server

    memset(&hints,0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if((rv = getaddrinfo(argv[1],PORT, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s \n", gai_strerror(rv));
        return 1;
    }
    
    //loop through all results, connect to first that we can
    for(p = servinfo; p != NULL; p = p->ai_next)
    {
        if((sockfd = socket(p->ai_family, p->ai_socktype,
            p->ai_protocol)) == -1){
                perror("client: socket");
                continue;
        }

        if(connect(sockfd, p->ai_addr, p->ai_addrlen) == -1){
            close(sockfd);
            perror("client: connect");
            continue;
        }

        break;
    }

    if (p == NULL){
        fprintf(stderr, "client: failed to connect \n" );
        return 2;
    }



    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),s, sizeof s);
    //printf("client: connecting to %s \n",s);

    
    freeaddrinfo(servinfo); //done with the structure


    //connected to the server
    //first send server user information -- 4 byte int length of name, then name
    int len = strlen(name);
    int *nlen = &len;
    if((rv = send(sockfd, nlen,sizeof (int), 0)) == -1)
    {
        perror("send:");

    }
    assert(rv == sizeof(int));
    if((rv = send(sockfd, name, sizeof name, 0)) == -1)
        perror("send:");
    
    assert(rv == sizeof name);    
    memset(&name[0],0,sizeof name);
    //clear name entry for reuse.  
    //now take user input and print incoming messages


    //need to make two threads, one to check for input, one to recieve and print incoming messages
    pthread_t tids[2];

    pthread_create(&tids[0],0,getinput,0); //make a thread to watch for input
    pthread_create(&tids[1],0,getmessages,0); //make a thread to watch for incoming messages
    
    for (int i = 0; i < 2; i++) {
        rv = pthread_join(tids[i], 0);
        assert(rv == 0);
    }
    close(sockfd);
    return 0;
}

void login(char *name)
{
    printf("What's your name?");
    scanf("%s", name);
}

void *getinput(void *vp)
{
    while(1){
        char input[MAXIN];
        char message[MAXIN];
        char recp[MAXRECP][MAXNAME];
        char c;
        int i;
        short rv;
        for(i = 0; (c = getchar()) != '\n'; i++)
            input[i] = c;

        input[i] = '\0';
        rv = parseinput(input, message, recp);
        if(rv >= 0){
            //send the message out
            int bytes_sent = 0;
            //send the number of recepients
            bytes_sent = send(sockfd, &rv, sizeof(short),0);
            assert(bytes_sent == sizeof(short));
            int j;
            for(j = 0; j < rv; j++){
                bytes_sent = send(sockfd, recp[j], sizeof(recp[j]),0);
                assert(bytes_sent == sizeof(recp[j]));
            }
            //sent the recp, now send the message                    
            bytes_sent = send(sockfd, message, sizeof message, 0);
            if(bytes_sent == -1){
               perror("send:");
            }
               

        }
    }
    return 0;

}

void *getmessages(void *vp)
{
    char buf[MAXDATASIZE];
    int numbytes;
    while(1){
        if((numbytes = recv(sockfd, buf, MAXDATASIZE-1,0)) == -1){
                    perror("recv");
                    exit(1);
                }
        else if(numbytes > 0){
            buf[numbytes] = '\0';
            if(strcmp(buf, "") != 0)                   
                printf("%s \n",buf);
            }
    }
    return 0;
}

//parse input, return number of recepients if specified, else 0 for all and -1 for invalid
int parseinput(char *in, char *message, char recp[][MAXNAME])
{
    int nr = 0, l = 0, i = 0;
    while(*in != '\0')
    {
        if (*in == '@')
        {
            in++;
            //next part should be a name, collect that
            
            //not sure why it only works if i collect it this way.
            while(!isspace(*in) && *in != '@' && *in != '\0'){
                recp[nr][l] = *in++;
                l++;

            }
            recp[nr][l] = '\0';
            l = 0;

            nr++; 
        }
        else{
            //its part of the message
            *message = *in; 
            message++;
            l++;
        }
        if(*in != '\0' && *in != '@')
            in++;
    }
    *message = '\0';
    message -= l;
    trimleading(message); //in case we collected extra white space

    if (strcmp(message, "")  == 0 || strcmp(message, "\n") == 0)
        return -1; // no input
    if(nr == 0)
        recp[nr][0] = -1; // signify sending to all
    recp[nr+1][0] = -2; //signify hit the end of recp list.
    
    return nr;

}   

//trim leading white space
void trimleading(char *s){
    while(isspace(*s))
        s++;
}

