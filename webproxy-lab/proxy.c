#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void doit(int fd);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
int parse_uri(char *uri, char *hostname, char *port, char *path);

int main(int argc, char **argv)
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command-line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);

    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        doit(connfd);
        Close(connfd);
        printf("---------------------------------\n");
        fflush(stdout);
    }
}

/*
 * doit - handle one HTTP request/response transaction
 */
void doit(int fd)
{
    char buf[MAXLINE];        // 클라이언트 요청 메시지를 통째로 담을 버퍼
    char method[MAXLINE];     // 클라이언트 요청의 메서드 ("무엇을")
    char uri[MAXLINE];        // 클라이언트 요청의 URI  ("어디에서")
    char version[MAXLINE];    // 클라이언트 요청의 HTTP 버전  ("어떤 규칙으로")

    // 클라이언트가 요청한 URI 를 더 잘게 쪼갠다.
    // ex. http://www.google.com:80/index.html
    char hostname[MAXLINE];   // 어떤 서버로 가야하는지? (www.google.com)
    char port[MAXLINE];       // 어떤 포트로 가야하는지? (80)
    char path[MAXLINE];       // 가서 무엇을 달라고 할지? (/index.html)

    // 네트워크 데이터를 안전하고 편리하게 읽기 위한 도구
    rio_t rio;

    /* ! 클라이언트의 요청 라인 읽기 */
    // rio 초기화 및 fd 연결하기
    Rio_readinitb(&rio, fd);

    // fd 로 들어온 데이터의 첫 번째 줄바꿈(\n)이 나올 때까지 읽어서 buf 에 담는다.
    Rio_readlineb(&rio, buf, MAXLINE);

    // method, uri, version 정보를 저장한다.
    sscanf(buf, "%s %s %s", method, uri, version);

    // HTTP 메서드 검증 : GET 요청
    if (strcasecmp(method, "GET") == 0) {
        // URI 파싱
        parse_uri(uri, hostname, port, path);

        printf(">> Parsed URI: \n");
        printf("   Hostname: %s\n", hostname);
        printf("   Port: %s\n", port);
        printf("   Path: %s\n", path);
        fflush(stdout);
    }
    // CONNECT 요청 처리 (HTTPS)
    else if (strcasecmp(method, "CONNECT") == 0) {
        printf(">> Handling CONNECT request for: %s\n", uri);
        fflush(stdout);

        // TODO: HTTPS 터널링 구현
    } else {
        clienterror(fd, method, "501", "Not Implemented", "Tiny does not implement this method");
        return;
    }
}

int parse_uri(char *uri, char *hostname, char *port, char *path) {
    // uri : http://www.google.com:80/index.html

    // 1. http:// 뒤의 호스트네임 시작 위치 찾기
    char *host_ptr = strstr(uri, "//");
    host_ptr = (host_ptr != NULL) ? host_ptr + 2 : uri;

    // 2. 경로 시작 위치 찾기 ('/' 문자)
    char *path_ptr = strchr(host_ptr, '/');
    if (path_ptr == NULL) { // ex.http://www.google.com
        strcpy(path, "/");
    } else {
        strcpy(path, path_ptr);
        *path_ptr = '\0'; // 호스트/포트 부분만 남기기 위해 임시로 문자열을 자름
    }

    // 3. 포트 번호 찾기
    char *port_ptr = strchr(host_ptr, ':');
    if (port_ptr != NULL) { // ex. http://www.google.com:80
        *port_ptr = '\0'; // 호스트네임 부분만 남기기 위해 임시로 문자열을 자름
        strcpy(hostname, host_ptr);
        strcpy(port, port_ptr + 1);
    } else {
        strcpy(port, "80");
        strcpy(hostname, host_ptr);
    }
}

/*
 * clienterror - returns an error message to the client
 */
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg)
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Proxy Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Proxy server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
