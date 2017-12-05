#include <stdio.h>
#include "csapp.h"
#include "sbuf.h"
#include "cache.h"

#define NTHREADS  5
#define CONNQSIZE  16   //pretty sure these 2 Q sizes must be the same
#define LOGQSIZE  16

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *proxy_connect_hdr = "Proxy-Connection: close\r\n";
static const char *connection_hdr = "Connection: close\r\n";

/* Some global vars for the class */
int_sbuf_t connQ; // Shared buffer of connected descriptors
str_sbuf_t logQ; // Shared buffer of log messages
sem_t mutex;
Node *head = NULL;

void *doit(void *vargp);
void *logger(void *vargp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg);
int fwd_request(char *uri, rio_t *rio, Node *head);
void send_request_line(char *olduri, int clientfd);
void send_fwd_requesthdrs(rio_t *rp, char *host, int clientfd);
void get_host_and_port(char *olduri, char *host, char *port);
void safe_send(int clientfd, char *bytes, int len);
void safe_send_and_copy(int clientfd, char *bytes, int len, int *copy_i, unsigned char *copy);
void fwd_response(int rcvr_fd, int responder_fd, char *url);

int i_after_slashes(int nslashes, char *bytes);

int main(int argc, char **argv)
{
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    Sem_init(&mutex, 0, 1);

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);

    int_sbuf_init(&connQ, CONNQSIZE); //initialize queues
    str_sbuf_init(&logQ, LOGQSIZE);

    Pthread_create(&tid, NULL, logger, NULL); //create logger thread

    for (int i = 0; i < NTHREADS; i++)  // Create worker threads
	     Pthread_create(&tid, NULL, doit, NULL);

    while (1)
    {
        clientlen = sizeof(clientaddr);
	    connfd = Accept(listenfd, (SA *) &clientaddr, &clientlen);
	    int_sbuf_insert(&connQ, connfd); // Insert connfd in buffer
    }
}


/*
 * doit - handle one HTTP request/response transaction
 *
 * See https://www.w3.org/Protocols/rfc2616/rfc2616-sec5.html for HTTP rules
 */
void *doit(void *vargp)
{
    Pthread_detach(pthread_self());
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    rio_t rio;

    while(1)
    {
        int fd = int_sbuf_remove(&connQ); //remove a connection from the buffer
        Rio_readinitb(&rio, fd);
        if (!Rio_readlineb(&rio, buf, MAXLINE)) { //Rio_readlineb gets the first line from the fd tied to rio and stores it in buf
            Close(fd);
            str_sbuf_insert(&logQ, "connection file descriptor seems to have been empty\n"); // Insert message into logQ
            continue;
        }

        sscanf(buf, "%s %s %s", method, uri, version); //copies the 3 strings from request line into these vars
        if (strcasecmp(method, "GET")) {
            clienterror(fd, method, "501", "Not Implemented",
                        "Tiny does not implement this method");
            Close(fd);
            str_sbuf_insert(&logQ, "Method other than GET was requested\n"); // Insert message into logQ
            continue;
        }
        if(uri[0] != '/') { //if it doesn't start with a '/', the URI is absolute URI, so it's a proxy request
            P(&mutex);
            Node *cached_item = find(head, uri);
            V(&mutex);
            if(cached_item != NULL)
            {
                P(&mutex);
                toFront(head, cached_item); //update for LRU policy
                V(&mutex);
                printf("Item for request %s already in cache\n", uri);
                safe_send(fd, cached_item->bytes, cached_item->nbytes);
            }
            else
            {
                int responder_fd = fwd_request(uri, &rio, head);
                fwd_response(fd, responder_fd, uri);
                Close(responder_fd);
            }
            Close(fd);
            str_sbuf_insert(&logQ, "Proxy request processed\n"); // Insert message into logQ
            continue;
        }

        Close(fd);
        str_sbuf_insert(&logQ, "Normal (non-proxy) request processed\n"); // Insert message into logQ
    }
}

void *logger(void *vargp)
{
    Pthread_detach(pthread_self());
    FILE *fp;

    fp = fopen("/tmp/jaxondk-proxylog.txt", "a"); //creates the file if DNE. "a" is for append mode
    while(1)
    {
        char *msg = str_sbuf_remove(&logQ); //remove a log message from the buffer
        fprintf(fp, msg); //write msg to fp
    }

    fclose(fp); //close the file
}

/*
 * Processes the proxy request, modifying request line and host header,
 * attaches User-agent, connection, and proxy-connection headers, and then
 * forwards the request to the desired host.
 *
 * Returns the file descriptor for the socket connection with desired host
 */
int fwd_request(char *uri, rio_t *rio, Node *head)
{
    char host[MAXLINE];
    char port[6];

    get_host_and_port(uri, host, port);
    int clientfd = open_clientfd(host, port);
    send_request_line(uri, clientfd);
    send_fwd_requesthdrs(rio, host, clientfd);
    return clientfd;
}

/*
 * Forwards the response from the host at responder fd to the initiator of the original request at receiver fd.
 * We need the url here for caching purposes (it's the original URI sent to the proxy by the browser)
 */
void fwd_response(int rcvr_fd, int responder_fd, char *url)
{
    rio_t rio_responder;
    Rio_readinitb(&rio_responder, responder_fd);

    unsigned char *completeResponse = (unsigned char*)malloc(MAXLINE*sizeof(unsigned char)); //for cache
    int resp_i = 0; //for cache
    int total_len = 0;
    char bytes[MAXLINE];
    int len = 2; //just in case the response is just "\r\n", as this would mean len would never get initialized
    int content_len = 0;

    //send headers and get content-length
    do
    {
        len = Rio_readlineb(&rio_responder, bytes, MAXLINE);
        total_len += len;
        if(strstr(bytes, "Content-length:"))
        {
            int i = strlen("Content-length:");
            content_len = atoi((bytes+i));
            total_len += content_len;
        }
        safe_send_and_copy(rcvr_fd, bytes, len, &resp_i, completeResponse);
    } while(strcmp(bytes, "\r\n"));

    //send response body
    int body_len = 0;
    while(body_len < content_len)
    {
        len = Rio_readlineb(&rio_responder, bytes, MAXLINE);
        body_len += len;
        safe_send_and_copy(rcvr_fd, bytes, len, &resp_i, completeResponse);
    }

    //TODO make thread safe!!!
    if(total_len > MAX_OBJECT_SIZE)
    {
        printf("Object for %s bigger than max object size, not caching\n", url);
    }
    else
    {
        P(&mutex);
        head = push(head, url, total_len, completeResponse);
        // printLL(head);
        V(&mutex);
    }
}

/*
 * Makes request line to forward to the actual desired client.
 */
void send_request_line(char *olduri, int clientfd)
{
    char newuri[MAXLINE];
    char reqline[MAXLINE];
    int i = i_after_slashes(3, olduri) - 1; //points you at the first slash of the suffix of the URL
    strcpy(newuri, olduri+i);
    int len = sprintf(reqline, "GET %s HTTP/1.1\r\n", newuri);

    safe_send(clientfd, reqline, len);
    // char msg[MAXLINE];
    // sprintf(msg, "Clientfd: %d. Request line received:\n%s",clientfd,reqline);
    // str_sbuf_insert(&logQ, msg);
}

/*
 * Extracts host and port from olduri
 */
void get_host_and_port(char *olduri, char *host, char *port)
{
    int host_i = i_after_slashes(2, olduri); //pointing to first char of host
    int suffix_i = i_after_slashes(3, olduri) - 1; //pointing to the '/' after host:port
    char* colon_ptr = strstr(olduri+host_i, ":");

    //if there's a colon, split the host:port accordingly
    if(colon_ptr)
    {
        int colon_i = colon_ptr-olduri;
        int port_i = colon_i + 1;
        strncpy(host, olduri+host_i, colon_i-host_i);
        strncpy(port, olduri+port_i, suffix_i-port_i);
        host[colon_i-host_i] = '\0';
        port[suffix_i-port_i] = '\0';
    }
    else //if no colon, set host and then make port the default for http (80)
    {
        strncpy(host, olduri+host_i, suffix_i-host_i);
        strcpy(port, "80");
        host[suffix_i-host_i] = '\0';
    }

}

/*
 * send_fwd_requesthdrs - reads HTTP request headers for a proxy request,
 *                          modifies as necessary, and sends them to clientfd one line at a time
 */
void send_fwd_requesthdrs(rio_t *rp, char *host, int clientfd)
{
    char curr_line[MAXLINE];
    int connection_hdr_sent = 0; //bool
    int proxy_hdr_sent = 0; //bool
    int len;

    while(1) {
        len = Rio_readlineb(rp, curr_line, MAXLINE);
        if(!strcmp(curr_line, "\r\n")) //if the curr line = \r\n, end
            break;
        if(strstr(curr_line, "Host:"))
            len = sprintf(curr_line, "Host: %s\r\n", host);
        else if(strstr(curr_line, "User-Agent:"))
            len = sprintf(curr_line, "%s", user_agent_hdr);
        else if(strstr(curr_line, "Proxy-Connection:"))
        {
            len = sprintf(curr_line, "%s", proxy_connect_hdr);
            proxy_hdr_sent = 1;
        }
        else if(strstr(curr_line, "Connection:"))  //Notice the order here is important, only check for "Connection" if not "Proxy-Connection"
        {
            len = sprintf(curr_line, "%s", connection_hdr);
            connection_hdr_sent = 1;
        }

        safe_send(clientfd, curr_line, len);
    }

    if(!connection_hdr_sent) //make sure connection header gets sent
    {
        len = sprintf(curr_line, "%s", connection_hdr);
        safe_send(clientfd, curr_line, len);
    }
    if(!proxy_hdr_sent)
    {
        len = sprintf(curr_line, "%s", proxy_connect_hdr);
        safe_send(clientfd, curr_line, len);
    }

    //send the terminating blank line for the request hdrs last
    safe_send(clientfd, "\r\n", 2);

    printf("Done with send request hdrs\n\n"); //DEBUG purposes
    return;
}

/*
 * Sends bytes of length len to clientfd; prints an error statement if unsuccessful
 */
void safe_send(int clientfd, char *bytes, int len)
{
    printf("%s", bytes);
    if(rio_writen(clientfd, bytes, len) < 0)
        printf("*** Send failed for: %s ***\n", bytes);
}

void safe_send_and_copy(int clientfd, char *bytes, int len, int *copy_i, unsigned char *copy)
{
    printf("%s",bytes);
    memcpy(&copy[*copy_i], bytes, len); //copies all of bytes[] (len) to copy[] starting at copy_i
    *copy_i += len; //incr copy_i
    if(rio_writen(clientfd, bytes, len) < 0)
        printf("*** Send failed for: %s ***\n", bytes);
}

/*
 * Returns an index into an char array after a number of slashes (nslashes)
 * Use for parsing through the uri
 */
int i_after_slashes(int nslashes, char *bytes)
{
    int i = 0;
    for (int slashcnt = 0; slashcnt != nslashes; i++) {
        if(bytes[i] == '/') slashcnt++;
    }
    return i;
}




/***************************************** OLD TINY.C STUFF *****************************************/
/****************************************************************************************************/
/****************************************************************************************************/
/****************************************************************************************************/
/****************************************************************************************************/

/*
 * clienterror - returns an error message to the client
 */
void clienterror(int fd, char *cause, char *errnum,
		 char *shortmsg, char *longmsg)
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
