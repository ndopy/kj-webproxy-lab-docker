#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void doit(int fd);
void read_requesthdrs(rio_t *rp, char *host_header, char *other_header);
void parse_uri(char *uri, char *hostname, char *port, char *path);
void reassemble(char *req, char *path, char *hostname, char *other_header);
void forward_response(int serve_df, int  fd);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

int main(int argc, char **argv) {
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* 프로그램 실행 시 포트 번호를 입력했는지 확인한다. */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    // 입력한 포트 번호로 클라이언트 연결을 기다리는 서버 소켓 열기
    listenfd = Open_listenfd(argv[1]);

    while (1) {
        clientlen = sizeof(clientaddr);
        // 연결이 오면 클라이언트와 통신할 새로운 소켓을 만든다.
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

        // 접속한 클라이언트의 IP 주소와 포트 번호 얻기
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)", hostname, port);

        doit(connfd);
        Close(connfd);
    }
}

void doit(int fd) {
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    // 헤더를 확인하고 저장할 버퍼
    char host_header[MAXLINE], other_header[MAXLINE];
    // 목적지 서버에 연결하기 위한 정보를 담을 버퍼
    char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
    // 목적지 서버로 보낼 요청을 담을 버퍼
    char request_buf[MAXLINE];
    rio_t rio;

    // 1. 소켓에서 데이터를 읽을 준비하기
    Rio_readinitb(&rio, fd);

    // 2. 클라이언트가 보낸 요청의 첫 줄(요청 라인) 읽기
    Rio_readlineb(&rio, buf, MAXLINE);
    printf("Request headers:\n");
    printf("%s", buf);

    // 3. 요청 라인에서 메서드 ,URI, HTTP 버전을 분리해 각 변수에 저장하기
    sscanf(buf, "%s %s %s", method, uri, version);

    // 4. GET, 메서드가 아니면 에러를 보낸다.
    if (strcasecmp(method, "GET") != 0) {
        clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
        return;
    }

    // 5. 요청 헤더를 읽어서 버린다.
    read_requesthdrs(&rio, host_header, other_header);

    parse_uri(uri, hostname, port, path);

    int serve_df = Open_clientfd(hostname, port);

    reassemble(request_buf, path, hostname, other_header);

    Rio_writen(serve_df, request_buf, strlen(request_buf));

    forward_response(serve_df, fd);
}

void read_requesthdrs(rio_t *rp, char *host_header, char *other_header) {
    char buf[MAXLINE];

    host_header[0] = '\0';
    other_header[0] = '\0';

    while (Rio_readlineb(rp, buf, MAXLINE) > 0 && strcmp(buf, "\r\n")) {
        if (!strncasecmp(buf, "Host:", 5)) {
            strcpy(host_header, buf);
        } else if (!strncasecmp(buf, "User-Agent:", 11) ||
                   !strncasecmp(buf, "Connection:", 11) ||
                   !strncasecmp(buf, "Proxy-Connection:", 17)) {
            continue; // 무시
        } else {
            strcat(other_header, buf);
        }
    }
}

void parse_uri(char *uri, char *hostname, char *port, char *path) {
    char *host_begin, *host_end, *port_begin, *path_begin;
    char buf[MAXLINE];

    strcpy(buf, uri);

    host_begin = strstr(buf, "//");
    host_begin = (host_begin != NULL) ? host_begin + 2 : buf;

    path_begin = strchr(host_begin, '/');
    if (path_begin != NULL) {
        strcpy(path, path_begin);
        *path_begin = '\0';
    } else {
        strcpy(path, "/");
    }

    port_begin = strchr(host_begin, ':');
    if (port_begin != NULL) {
        *port_begin = '\0';
        strcpy(hostname, host_begin);
        strcpy(port, port_begin + 1);
    } else {
        strcpy(hostname, host_begin);
        strcpy(port, "80");
    }
}

void reassemble(char *req, char *path, char *hostname, char *other_header) {
    sprintf(req,
        "GET %s HTTP/1.0\r\n"
        "Host: %s\r\n"
        "%s"
        "Connection: close\r\n"
        "Proxy-Connection: close\r\n"
        "%s"
        "\r\n",
        path,
        hostname,
        user_agent_hdr,
        other_header
    );
}

void forward_response(int serve_df, int  fd) {
    rio_t serve_rio;
    char response_buf[MAXBUF];

    Rio_readinitb(&serve_rio, serve_df);
    ssize_t n;

    while ((n = Rio_readnb(&serve_rio, response_buf, MAXBUF)) > 0) {
        rio_writen(fd, response_buf, n);
    }
}


void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg){
    char buf[MAXLINE], body[MAXLINE]; // buf: HTTP 헤더 문자열 저장용, body: 응답 본문 HTML 저장용
    sprintf(body, "<html><title>Tiny Error</title></html>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n</body>", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf)); // 상태줄 전송 예: HTTP/1.0 404 Not Found
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf)); // MIME 타입 명시: HTML이라는 것을 알려줌
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf)); // 본문 길이 알려줌 + 빈 줄로 헤더 종료
    Rio_writen(fd, body, strlen(body)); // 위에서 만든 HTML을 클라이언트에게 전송
}