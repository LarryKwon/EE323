/*
** client.c -- a stream socket client demo */
#include <stdio.h> 
#include <stdlib.h> 
#include <unistd.h> 
#include <errno.h> 
#include <string.h> 
#include <netdb.h> 
#include <sys/types.h> 
#include <netinet/in.h> 
#include <sys/socket.h>

#include <arpa/inet.h>

// #define PORT "3491" // the port client will be connecting to
#define MAXDATASIZE 1024*5 // max number of bytes we can get at once

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr); 
}

int main(int argc, char *argv[]){
    char *PORT;
    char *Host;
    int num;
    int sockfd, numbytes;
    char buf[MAXDATASIZE];
    char prev_buf[MAXDATASIZE];
    char *result;
    struct addrinfo hints, *servinfo, *p; 
    int rv;

    char s[INET6_ADDRSTRLEN];

    if (argc != 5) {
        fprintf(stderr,"usage: client hostname\n");
        exit(1);
    }
    while((num = getopt(argc,argv,"p:h:"))!=EOF){
        switch(num){
            case 'p':
                PORT = optarg;
            break;
            case 'h' :
                Host = optarg;
            break;
        }
    }

    memset(&hints, 0, sizeof hints); 
    hints.ai_family = AF_UNSPEC; 
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(Host, PORT, &hints, &servinfo)) != 0) { 
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv)); 
        return 1;
    }

// loop through all the results and connect to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("client: socket");
            continue; 
        }
        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) { 
            close(sockfd);
            perror("client: connect");
            continue; 
        }
        break; 
        }
    
    if (p == NULL) {
        fprintf(stderr, "client: failed to connect\n");
        return 2;
    }

    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof s);
    printf("client: connecting to %s\n", s);
    freeaddrinfo(servinfo); // all done with this structure

    // sending messages until meeting double enter or EOF
    while(1){
        strcpy(prev_buf,buf);

        result = fgets(buf,MAXDATASIZE-1,stdin);
        if(result == NULL){
            exit(0);
        }
        if(strchr(prev_buf,'\n')!=NULL&& buf[0]=='\n'){
            exit(0);
        }
        send(sockfd,buf,strlen(buf),0);

    }

    close(sockfd);
    return 0; 
}