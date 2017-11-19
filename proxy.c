#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *proxy_connect_hdr = "Proxy-Connection: close\r\n";
static const char *connection_hdr = "Connection: close\r\n";

void *doit(void *fd);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, 
                 char *shortmsg, char *longmsg);
int fwd_request(char *uri, rio_t *rio);
void send_request_line(char *olduri, int clientfd);
void send_fwd_requesthdrs(rio_t *rp, char *host, int clientfd);
void get_host_and_port(char *olduri, char *host, char *port);
void safe_send(int clientfd, char *bytes, int len);
void fwd_response(int rcvr_fd, int responder_fd);

int i_after_slashes(int nslashes, char *bytes);

int main(int argc, char **argv) 
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:tiny:accept
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE,
                    port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        pthread_t tid;
        Pthread_create(&tid, NULL, doit, connfd);
    }
}


/*
 * doit - handle one HTTP request/response transaction
 *
 * See https://www.w3.org/Protocols/rfc2616/rfc2616-sec5.html for HTTP rules
 */
void *doit(void *fd)
{
    Pthread_detach(pthread_self());
    int is_static;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio;

    /* Read request line and headers */
    Rio_readinitb(&rio, fd);
    if (!Rio_readlineb(&rio, buf, MAXLINE)) { //Rio_readlineb gets the first line from the fd tied to rio and stores it in buf
        Close(fd);
        return NULL;
    }
//    printf("Incoming request line:\n");
//    printf("%s", buf); //prints request line
    sscanf(buf, "%s %s %s", method, uri, version); //copies the 3 strings from request line into these vars
    if (strcasecmp(method, "GET")) {
        clienterror(fd, method, "501", "Not Implemented",
                    "Tiny does not implement this method");
        Close(fd);
        return NULL;
    }
    if(uri[0] != '/') { //if it doesn't start with a '/', the URI is absolute URI, so it's a proxy request
        int responder_fd = fwd_request(uri, &rio);
        fwd_response(fd, responder_fd);
        Close(responder_fd);
        Close(fd);
        return NULL;
    }
    /************************** OLD TINY.C FUNCTIONALITY **************************************/
    //read_requesthdrs(&rio);

    /* Parse URI from GET request */
    is_static = parse_uri(uri, filename, cgiargs);
    
    if (stat(filename, &sbuf) < 0) {
        clienterror(fd, filename, "404", "Not found",
		    "Tiny couldn't find this file");
        Close(fd);
        return NULL;
    }

    if (is_static) { /* Serve static content */          
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) { //line:netp:doit:readable
            clienterror(fd, filename, "403", "Forbidden",
                "Tiny couldn't read the file");
            Close(fd);
            return NULL;
        }
        serve_static(fd, filename, sbuf.st_size);        //line:netp:doit:servestatic
    }
    else { /* Serve dynamic content */
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) { //line:netp:doit:executable
            clienterror(fd, filename, "403", "Forbidden",
                "Tiny couldn't run the CGI program");
            Close(fd);
            return NULL;
        }
        serve_dynamic(fd, filename, cgiargs);            //line:netp:doit:servedynamic
    }
    
    Close(fd);
    return NULL;
}

/*
 * Processes the proxy request, modifying request line and host header,
 * attaches User-agent, connection, and proxy-connection headers, and then
 * forwards the request to the desired host.
 *
 * Returns the file descriptor for the socket connection with desired host
 */
int fwd_request(char *uri, rio_t *rio)
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
 */
void fwd_response(int rcvr_fd, int responder_fd)
{
    rio_t rio_responder;
    Rio_readinitb(&rio_responder, responder_fd);

    char curr_line[MAXLINE];
    int len = 2; //just in case the response is just "\r\n", as this would mean len would never get initialized
    
    do
    {
        len = Rio_readlineb(&rio_responder, curr_line, MAXLINE);
        safe_send(rcvr_fd, curr_line, len);
    } while(strcmp(curr_line, "\r\n"));
    
    safe_send(rcvr_fd, curr_line, len);
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
 * parse_uri - parse URI into filename and CGI args
 *             return 0 if dynamic content, 1 if static
 */
int parse_uri(char *uri, char *filename, char *cgiargs) 
{
    char *ptr;

    if (!strstr(uri, "cgi-bin")) {  /* Static content */
        strcpy(cgiargs, "");
        strcpy(filename, ".");
        strcat(filename, uri);
        if (uri[strlen(uri)-1] == '/')
            strcat(filename, "home.html");
        return 1;
    }
    else {  /* Dynamic content */
        ptr = index(uri, '?');
        if (ptr) {
            strcpy(cgiargs, ptr+1);
            *ptr = '\0';
        }
        else
            strcpy(cgiargs, "");
        strcpy(filename, ".");
        strcat(filename, uri);
        return 0;
    }
}

/*
 * serve_static - copy a file back to the client 
 */
void serve_static(int fd, char *filename, int filesize) 
{
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];
 
    /* Send response headers to client */
    get_filetype(filename, filetype);       //line:netp:servestatic:getfiletype
    sprintf(buf, "HTTP/1.0 200 OK\r\n");    //line:netp:servestatic:beginserve
    sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
    Rio_writen(fd, buf, strlen(buf));       //line:netp:servestatic:endserve
    printf("Response headers:\n");
    printf("%s", buf);

    /* Send response body to client */
    srcfd = Open(filename, O_RDONLY, 0);    //line:netp:servestatic:open
    srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);//line:netp:servestatic:mmap
    Close(srcfd);                           //line:netp:servestatic:close
    Rio_writen(fd, srcp, filesize);         //line:netp:servestatic:write
    Munmap(srcp, filesize);                 //line:netp:servestatic:munmap
}

/*
 * get_filetype - derive file type from file name
 */
void get_filetype(char *filename, char *filetype) 
{
    if (strstr(filename, ".html"))
	strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif"))
	strcpy(filetype, "image/gif");
    else if (strstr(filename, ".png"))
	strcpy(filetype, "image/png");
    else if (strstr(filename, ".jpg"))
	strcpy(filetype, "image/jpeg");
    else
	strcpy(filetype, "text/plain");
}

/*
 * serve_dynamic - run a CGI program on behalf of the client
 */
void serve_dynamic(int fd, char *filename, char *cgiargs) 
{
    char buf[MAXLINE], *emptylist[] = { NULL };

    /* Return first part of HTTP response */
    sprintf(buf, "HTTP/1.0 200 OK\r\n"); 
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));
  
    if (Fork() == 0) { /* Child */ //line:netp:servedynamic:fork
	/* Real server would set all CGI vars here */
	setenv("QUERY_STRING", cgiargs, 1); //line:netp:servedynamic:setenv
	Dup2(fd, STDOUT_FILENO);         /* Redirect stdout to client */ //line:netp:servedynamic:dup2
	Execve(filename, emptylist, environ); /* Run CGI program */ //line:netp:servedynamic:execve
    }
    Wait(NULL); /* Parent waits for and reaps child */ //line:netp:servedynamic:wait
}

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
