/*
 * proxy.c - CS:APP Web proxy
 *
 * IMPORTANT: Give a high level description of your code here. You
 * must also provide a header comment at the beginning of each
 * function that describes what that function does.
 */ 

#include <pthread.h>
#include "csapp.h"
/* 
 * Globals
 */
#define PROXY_LOG "proxy.log"  // proxy log file
FILE *log_file; /* Log file with one line per HTTP request */

/*
 * Function prototypes
 */
void *process_request(int connfd, struct sockaddr_in clientaddr);
int parse_uri(char *uri, char *target_addr, char *path, int  *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size);

/* 
 * main - Main routine for the proxy program 
 */
int main(int argc, char **argv)
{
    int listenfd;
    int clientlen;

    int port;
    int connfd; 

    struct sockaddr_in clientaddr;

    /* Check arguments */
    if (argc != 2) {
	fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
	exit(0);
    }

    Signal(SIGPIPE, SIG_IGN);

    /* Initialize */
    log_file = Fopen(PROXY_LOG, "a");
    port = atoi(argv[1]); // what is the port number?

    // Create a listening descriptor
    listenfd = Open_listenfd(port);
    if (listenfd < 0) {
        fprintf(stderr, "Cannot listen on port: %d\n", port);
        exit(1);
    }

    /* Wait for and process client connections */
    while (1) { 
	// accept a connection with client and set to connfd
	clientlen = sizeof(clientaddr);

        connfd = Accept(listenfd,(SA *)&clientaddr,(socklen_t *)&clientlen);

	//Pthread_create(&tid, NULL, (void *)process_request, (void *)connfd);

        /* sequential call to process_request */
      	process_request(connfd, clientaddr);
    }

    fclose(log_file);
    exit(0);
}

/*
 * process_request - Executed sequentially or by each thread
 * 
 * Read an HTTP request from a client, forward it to the
 * end server (always as a simple HTTP/1.0 request), wait for the
 * response, and then forward back to the client.
 * 
 */
void *process_request(int connfd, struct sockaddr_in clientaddr)
{
    int realloc_factor;             /* Used to increase size of request buffer if necessary */  
    char *request;                  /* HTTP request from client */
    char *request_uri;              /* Start of URI in first HTTP request header line */
    char *request_uri_end;          /* End of URI in first HTTP request header line */
    char *rest_of_request;          /* Beginning of second HTTP request header line */
    char buf[MAXLINE];              /* General I/O buffer */
    int request_len;                /* Total size of HTTP request */
    int i, n;                       /* General index and counting variables */
    rio_t  rio;

    /* 
     * Read the entire HTTP request into the request buffer, one line
     * at a time.
     */
    request = (char *)Malloc(MAXLINE);
    request[0] = '\0';
    realloc_factor = 2;
    request_len = 0;
    Rio_readinitb(&rio, connfd);
    while (1) {
	if ((n = Rio_readlineb(&rio, buf, MAXLINE)) <= 0) {
	    printf("process_request: client issued a bad request (1).\n");
	    close(connfd);
	    free(request);
	    return NULL;
	}

	/* If not enough room in request buffer, make more room */
	if (request_len + n + 1 > MAXLINE)
	    Realloc(request, MAXLINE*realloc_factor++);

	strcat(request, buf);
	request_len += n;

	/* An HTTP requests is always terminated by a blank line */
	if (strcmp(buf, "\r\n") == 0)
	    break;
    }
    
    /* 
     * Make sure that this is indeed a GET request
     */
    if (strncmp(request, "GET ", strlen("GET "))) {
	printf("process_request: Received non-GET request\n");
	close(connfd);
	free(request);
	return NULL;
    }
    request_uri = request + 4;

    /* 
     * Extract the URI from the request
     */
    request_uri_end = NULL;
    for (i = 0; i < request_len; i++) {
	if (request_uri[i] == ' ') {
	    request_uri[i] = '\0';
	    request_uri_end = &request_uri[i];
	    break;
	}
    }

    /* 
     * If we hit the end of the request without encountering a
     * terminating blank, then there is something screwy with the
     * request
     */
    if ( i == request_len ) {
	printf("process_request: Couldn't find the end of the URI\n");
	close(connfd);
	free(request);
	return NULL;
    }

    /*
     * Make sure that the HTTP version field follows the URI 
     */
    if (strncmp(request_uri_end + 1, "HTTP/1.0\r\n", strlen("HTTP/1.0\r\n")) &&
	strncmp(request_uri_end + 1, "HTTP/1.1\r\n", strlen("HTTP/1.1\r\n"))) {
	printf("process_request: client issued a bad request (4).\n");
	close(connfd);
	free(request);
	return NULL;
    }

    /*
     * We'll be forwarding the remaining lines in the request
     * to the end server without modification
     */
    rest_of_request = request_uri_end + strlen("HTTP/1.0\r\n") + 1;

    //Call parse_uri, URI is in request_uri, to be used as input to parse_uri
    int clientfd;
    char *target_addr;
    target_addr = (char *)Malloc(MAXLINE);
    char *path;
    path = (char *)Malloc(MAXLINE);
    int port = 0;
    parse_uri(request_uri, target_addr, path, &port);

    //Forward request to the end server using open_clientfd
    clientfd = open_clientfd(target_addr, port);

    char *msg;
    msg = (char *)Malloc(MAXLINE);
   
    //Request of following form:
    //"GET /"
    strcat(msg, "GET /");
    //pathname returned from parse_uri
    strcat(msg, path);
    //" HTTP/1.0\r\n"
    strcat(msg, " HTTP/1.0\r\n");
    //rest_of_request
    strcat(msg, rest_of_request);

    size_t size;
    size_t totalsize = 0;
    char *buf2;
    buf2 = (char *)Malloc(MAXLINE);
    rio_t rio2;

    //while loop reads from server and writes to client, see Echo, Fig. 11.22
    Rio_writen(clientfd, msg, strlen(msg));
    Rio_readinitb(&rio2, clientfd);
    while((size = Rio_readlineb(&rio2, buf2, MAXLINE)) != 0)
    {
	totalsize += size;
	printf("server received %d bytes\n", (int)size);
	Rio_writen(connfd, buf2, size);
    }
    printf("done\n");

    //Log the request to disk
    char *log;
    log = (char *)Malloc(MAXLINE);

    format_log_entry(log, &clientaddr, target_addr, totalsize); 
    
    fprintf(log_file, "%s\n", log);
    fflush(log_file);

    //Clean up to avoid memory leaks, close fds
    Close(clientfd);
    Close(connfd);

    return NULL;
}

/*
 * parse_uri - URI parser
 * 
 * Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 * the host name, path name, and port.  The memory for hostname and
 * pathname must already be allocated and should be at least MAXLINE
 * bytes. Return -1 if there are any problems.
 */
int parse_uri(char *uri, char *hostname, char *pathname, int *port)
{
    char *hostbegin;
    char *hostend;
    char *pathbegin;
    int len;

    if (strncasecmp(uri, "http://", 7) != 0) {
	hostname[0] = '\0';
	return -1;
    }
       
    /* Extract the host name */
    hostbegin = uri + 7;
    hostend = strpbrk(hostbegin, " :/\r\n\0");
    len = hostend - hostbegin;
    strncpy(hostname, hostbegin, len);
    hostname[len] = '\0';
    
    /* Extract the port number */
    *port = 80; /* default */
    if (*hostend == ':')   
	*port = atoi(hostend + 1);
    
    /* Extract the path */
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin == NULL) {
	pathname[0] = '\0';
    }
    else {
	pathbegin++;	
	strcpy(pathname, pathbegin);
    }

    return 0;
}

/*
 * format_log_entry - Create a formatted log entry in logstring. 
 * 
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), and the size in bytes
 * of the response from the server (size).
 */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, 
		      char *uri, int size)
{
    time_t now;
    char time_str[MAXLINE];
    unsigned long host;
    unsigned char a, b, c, d;

    /* Get a formatted time string */
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    /* 
     * Convert the IP address in network byte order to dotted decimal
     * form. Note that we could have used inet_ntoa, but chose not to
     * because inet_ntoa is a Class 3 thread unsafe function that
     * returns a pointer to a static variable (Ch 13, CS:APP).
     */
    host = ntohl(sockaddr->sin_addr.s_addr);
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;

    /* Return the formatted log entry string */
    sprintf(logstring, "%s: %d.%d.%d.%d %s %d", time_str, a, b, c, d, uri, size);
}
