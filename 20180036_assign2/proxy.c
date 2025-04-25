/*
** proxy.c */
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

#include<sys/poll.h>

#define BACKLOG 10   // how many pending connections queue will hold
#define MAXDATASIZE 1024*5 // max bytes can get input at once

void sigchld_handler(int s) {
    while(waitpid(-1, NULL, WNOHANG) > 0); 
} // 

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr); 
}

int main(int argc, char *argv[])
{

char *PORT;
int sockfd, new_fd; // listen on sock_fd, new connection on new_fd 

struct addrinfo hints, *servinfo, *p ;
struct sockaddr_storage their_addr; // connector's address information 
socklen_t sin_size;

struct sigaction sa;
char ReadBuf[MAXDATASIZE];
char totalBuf[MAXDATASIZE]={'\0'};

int yes=1;
char s[INET6_ADDRSTRLEN]; 
int rv;

// for parsing arguments //
if (argc != 2) {
    exit(1);
    }
else{
    PORT = argv[1];
}
char tot_blacklist[MAXDATASIZE] ;
char blacklist[MAXDATASIZE];

// to handle stdin stream //
struct pollfd fd[1];
fd[0].fd = STDIN_FILENO;
fd[0].events = POLL_IN;
poll(fd,1,0);
if(fd[0].revents&POLL_IN){
    while (fgets(blacklist, sizeof(blacklist), stdin)) {
    strcat(tot_blacklist,blacklist);
    }
}


memset(&hints, 0, sizeof hints); 
hints.ai_family = AF_UNSPEC; 
hints.ai_socktype = SOCK_STREAM; 
hints.ai_flags = AI_PASSIVE; // use my IP

// get address information//
if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) { 
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv)); 
    return 1;
}

// loop through all the results and bind to the first we can 
for(p = servinfo; p != NULL; p = p->ai_next) {
    if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
        fprintf(stderr,"server: socket");
        continue; 
        }
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
        fprintf(stderr,"setsockopt");
        exit(1); 
        }
    if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) { 
        close(sockfd);
        fprintf(stderr,"server: bind");
        continue; 
        }
    break; 
    }
if (p == NULL)  {
    fprintf(stderr, "server: failed to bind\n");
    return 2;
    }
freeaddrinfo(servinfo); // all done with this structure

// listen //
if (listen(sockfd, BACKLOG) == -1) { 
    perror("listen");
    exit(1);
}
// reap all dead processes sigemptyset(&sa.sa_mask);
sa.sa_handler = sigchld_handler; 
sa.sa_flags = SA_RESTART;
if (sigaction(SIGCHLD, &sa, NULL) == -1) {
    perror("sigaction");
    exit(1); 
}

// main accept() loop
while(1) { 
    sin_size = sizeof their_addr;
    new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size); 
    if (new_fd == -1) {
        perror("accept");
        continue; 
    }

    inet_ntop(their_addr.ss_family, 
        get_in_addr((struct sockaddr *)&their_addr),s, sizeof s);
    

    if (!fork()) { // this is the child process 
        close(sockfd); // child doesn't need the listener
        ssize_t receivedBytes; // received messages

        char method[MAXDATASIZE];
        char URI[MAXDATASIZE];
        char Host[MAXDATASIZE];
        char PORT[MAXDATASIZE] = "80"; // default = 80
        char version[MAXDATASIZE];
        char HostHead[MAXDATASIZE];

        ssize_t num_tot = 0;

        // receive the rquest, enter twice don't recv //
        while((receivedBytes = recv(new_fd,ReadBuf,MAXDATASIZE-1,0)) > 0){
            ReadBuf[receivedBytes]='\0';
            num_tot = num_tot + receivedBytes;
            strcat(totalBuf,(const char*)ReadBuf);
            if(totalBuf[num_tot-1]=='\n' && totalBuf[num_tot-2]=='\r' && totalBuf[num_tot-3]=='\n' && totalBuf[num_tot-2]=='\r'){
                
                break;
            }
        }
        
        // parsing area//

        //method parsing//
        int walk =0;
        int i = 0;
        while(totalBuf[walk]!=' '){
            method[i]=totalBuf[walk];
            walk ++;
            i++;
        }
        method[i]='\0';
        if(strncmp(method,"GET",3)!=0){
            send(new_fd,"HTTP/1.0 400 Bad Request\r\n\r\n",28,0);
            close(new_fd);
            return EXIT_FAILURE;  
        }
 
        //URI & Port parsing//

        // First, URI//
        i = 0;
        walk++;
        int cnt = 0;
        char* portnum ;
        while(totalBuf[walk]!=' '){
            URI[i]=totalBuf[walk];
            if(totalBuf[walk]==':'){
                cnt ++;
                if(cnt ==2){
                    portnum = &URI[i];
                }
            }
            walk++;
            i++;
        }
        URI[i]='\0';
            // error case: no http:// //
            if(strncmp(URI,"http://",7)!=0){
                send(new_fd,"HTTP/1.0 400 Bad Request\r\n\r\n",28,0);
                close(new_fd);
                return EXIT_FAILURE;
            }

        //Second, Port//
        i = 0;
        if(cnt ==2){
        *portnum = '\0';
        strcpy(PORT,portnum+1);
        }
            //error case: port is not a number //
        for(int i=0; i<strlen(PORT);i++){
            if(isdigit(PORT[i])==0){
                send(new_fd,"HTTP/1.0 400 Bad Request\r\n\r\n",28,0);
                close(new_fd);
                return EXIT_FAILURE;
                break;
            }
        }

        //Final,Hostname//
        char* ptr = & URI[7];
        strcpy(Host,ptr);
        if((ptr = strchr(Host,'/'))!=0){
            *ptr = '\0';
        }
            //error case: if request don't have any ip, 400 bad request//
            if(gethostbyname(Host)==NULL){
                send(new_fd,"HTTP/1.0 400 Bad Request\r\n\r\n",28,0);
                close(new_fd);
                return EXIT_FAILURE;
                // 종료
            }

        //version parsing//
        walk++;
        for(int i = 0; i<8;i++){
            version[i]=totalBuf[walk];
            walk++;
        }
        version[8]='\0';

            //error case: versiono is not proper//
            if(totalBuf[walk]!='\r'){
                send(new_fd,"HTTP/1.0 400 Bad Request\r\n\r\n",28,0);
                close(new_fd);
                return EXIT_FAILURE;
            }

        //Host header existence//
        walk = walk+2;
        ptr = & totalBuf[walk];
        strcpy(HostHead,ptr);
        int Header_len = strlen(HostHead);

            //error case: no Host header //
        if(strncmp(HostHead,"Host: ",6)!=0){
            send(new_fd,"HTTP/1.0 400 Bad Request\r\n\r\n",28,0);
            close(new_fd);
            return EXIT_FAILURE;
        }
        else{
            int ans = 0;
            for(int j = 6; j < Header_len; j++){
                if(HostHead[j]!=' '){
                    ans = 1;
                    break;
                }
            }
            if(ans == 0){
                send(new_fd,"HTTP/1.0 400 Bad Request\r\n\r\n",28,0);
                close(new_fd);
                return EXIT_FAILURE;
            }
        }

        //error case: if the method,URI,version,Host header don't exist //
        if(strlen(method)==0||strlen(URI)==0||strlen(version)==0||strlen(HostHead)==0){
            send(new_fd,"HTTP/1.0 400 Bad Request\r\n\r\n",28,0);
            close(new_fd);
            return EXIT_FAILURE;
        }
        // for redirection //
        if(strstr(tot_blacklist,Host)!=0){
            strcpy(Host,"warning.or.kr");
            strcpy(PORT,"80");
            char new_warning[MAXDATASIZE] = "GET http://warning.or.kr HTTP/1.0\r\n\r\nHost: warning.or.kr\r\n\r\n";
            strcpy(totalBuf,new_warning);
        }
        printf("%s ",Host);

        // request to host //

        // new socket//
        int host_sockfd;
        ssize_t responseBytes;
        char request_buf[MAXDATASIZE];
        struct addrinfo host_hints, *hostinfo, *t;
        int new_rv;

        char new_s[INET6_ADDRSTRLEN];
        memset(&host_hints,0, sizeof host_hints);
        host_hints.ai_family = AF_UNSPEC;
        host_hints.ai_socktype = SOCK_STREAM;

        //error case: if there's no ip address//
        if((new_rv = getaddrinfo(Host,PORT,&host_hints,&hostinfo))!=0){
            send(new_fd,"HTTP/1.0 404 Bad Request\r\n\r\n",28,0);
            close(new_fd);
            return EXIT_FAILURE;
        }

        // bind //
        for(t=hostinfo;t!=NULL;t=t->ai_next){
            if((host_sockfd = socket(t->ai_family,t->ai_socktype,0))==-1){
                // send(new_fd,"HTTP/1.0 500 Internal Serval error\r\n\r\n",30,0);
                close(new_fd);
                return EXIT_FAILURE;
                // perror("proxy: socket");
                continue;
            }
            if((connect(host_sockfd,t->ai_addr,t->ai_addrlen))==-1){
                close(host_sockfd);
                // perror("proxy: connect");
                // send(new_fd,"HTTP/1.0 500 Internal Serval error\r\n\r\n",28,0);
                close(new_fd);
                return EXIT_FAILURE;
                continue;
            }
            break;
        }
        if(t==NULL){
            // send(new_fd,"HTTP/1.0 500 Internal Serval error\r\n\r\n",28,0);
            close(new_fd);
            return EXIT_FAILURE;
            return 2;
        }

        inet_ntop(t->ai_family, get_in_addr((struct sockaddr *)t->ai_addr), new_s, sizeof new_s);
        printf("proxy: connecting to %s\n",new_s);
        freeaddrinfo(hostinfo);
        // send to host //
        send(host_sockfd,totalBuf,strlen(totalBuf),0);
        // send reponse to client//
        while((responseBytes = recv(host_sockfd, request_buf,MAXDATASIZE-1,0 ))>0){
            request_buf[responseBytes]='\0';
            // printf("%s ",request_buf);
            send(new_fd,request_buf,responseBytes,0);
        }
        
        close(new_fd);
        exit(0); 
    }
    close(new_fd); // parent doesn't need this 
    }
    return 0; 
    }