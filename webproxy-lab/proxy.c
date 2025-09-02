#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void doit(int fd);
void read_requesthdrs(rio_t *rp, char *other_header);
void parse_uri(char *uri, char *hostname, char *port, char *path);
void reassemble(char *req, char *path, char *hostname, char *other_header);
void forward_response(int serve_df, int  fd);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);


/**
 * 프록시 서버의 메인 함수
 * 
 * @param argc 명령행 인자의 개수
 * @param argv 명령행 인자 배열 (포트 번호 포함)
 * @return 프로그램 종료 코드
 */
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

/**
 * 클라이언트의 HTTP 요청을 처리하는 함수
 * 
 * @param fd 클라이언트와 연결된 파일 디스크립터
 */
void doit(int fd) {
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char other_header[MAXLINE];                             // 헤더를 확인하고 저장할 버퍼
    char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];   // 목적지 서버에 연결하기 위한 정보를 담을 버퍼
    char request_buf[MAXLINE];                              // 목적지 서버로 보낼 요청을 담을 버퍼
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
    
    // 5. 나머지 요청 헤더를 읽는다.
    read_requesthdrs(&rio, other_header);

    // 6. URI를 파싱하여 호스트명, 포트, 경로를 추출한다.
    parse_uri(uri, hostname, port, path);

    // 7. 목적지 서버와 연결할 새로운 소켓을 생성한다.
    int serve_df = Open_clientfd(hostname, port);

    // Open_clientfd는 실패 시 -1을 반환하므로, 에러 처리가 필요하다.
    if (serve_df < 0) {
        clienterror(fd, hostname, "502", "Bad Gateway", "Proxy could not connect to the host");
        return;
    }

    // 8. 목적지 서버로 보낼 HTTP 요청 메시지를 새로 조립한다.
    reassemble(request_buf, path, hostname, other_header);

    // 9. 조립한 HTTP 요청(request_bf)을 목적지 서버와 연결된 소켓(serve_df)을 통해 전송한다.
    Rio_writen(serve_df, request_buf, strlen(request_buf));

    // 10. 목적지 서버로부터 받은 응답을 클라이언트에게 전달한다.
    forward_response(serve_df, fd);
}

/**
 * HTTP 요청 헤더를 읽고 필요한 헤더만 저장하는 함수
 * 
 * @param rp 읽기 작업을 수행할 rio 구조체 포인터
 * @param other_header 유지할 헤더들을 저장할 버퍼
 */
void read_requesthdrs(rio_t *rp, char *other_header) {
    char buf[MAXLINE];

    other_header[0] = '\0';

    while (Rio_readlineb(rp, buf, MAXLINE) > 0 && strcmp(buf, "\r\n")) {
        // 무시할 헤더들 : reassemble 함수에서 새로 생성할 헤더들이다.
        if (!strncasecmp(buf, "Host:", 5) ||
            !strncasecmp(buf, "User-Agent:", 11) ||
            !strncasecmp(buf, "Connection:", 11) ||
            !strncasecmp(buf, "Proxy-Connection:", 17)) {
            continue;
        } else {
            strcat(other_header, buf);
        }
    }
}

/**
 * URI를 파싱하여 호스트명, 포트, 경로를 추출하는 함수
 * 
 * @param uri 파싱할 URI 문자열
 * @param hostname 추출된 호스트명을 저장할 버퍼
 * @param port 추출된 포트 번호를 저장할 버퍼
 * @param path 추출된 경로를 저장할 버퍼
 */
void parse_uri(char *uri, char *hostname, char *port, char *path) {
    // URI는 "http://www.example.com:8080/path/to/resource.html" 형식을 가질 수 있다.
    char *host_begin;
    char *port_begin;
    char *path_begin;
    char buf[MAXLINE];

    // 원본 URI를 수정하지 않기 위해 로컬 버퍼로 복사한다.
    // strcpy는 버퍼 오버플로우에 취약하므로, 크기를 지정하는 strncpy를 사용한다.
    strncpy(buf, uri, MAXLINE - 1);
    buf[MAXLINE - 1] = '\0'; // strncpy는 항상 null로 끝나지 않을 수 있으므로 수동으로 보장

    // "http://"가 있다면 그 다음부터 호스트 이름이 시작된다.
    host_begin = strstr(buf, "//");
    host_begin = (host_begin != NULL) ? host_begin + 2 : buf;
    // host_begin = "www.example.com:8080/path/to/resource.html"

    // 호스트 이름 뒤에 오는 첫 '/'가 경로의 시작이다.
    path_begin = strchr(host_begin, '/');
    if (path_begin != NULL) {
        // path_begin = "/path/to/resource.html"
        strncpy(path, path_begin, MAXLINE - 1);
        path[MAXLINE - 1] = '\0';
        *path_begin = '\0'; // 호스트 이름과 포트 부분만 남기기 위해 문자열을 자르는 처리 추가
    } else {
        strncpy(path, "/", MAXLINE);
    }

    // 남은 부분에서 ':'를 찾아 포트 번호를 분리한다.
    // www.example.com:8080
    port_begin = strchr(host_begin, ':');
    if (port_begin != NULL) {  // 포트 번호가 명시되어 있을 경우
        *port_begin = '\0';    // 호스트 이름만 남기기 위해 문자열을 자르는 처리 추가
        strncpy(hostname, host_begin, MAXLINE - 1);
        hostname[MAXLINE - 1] = '\0';
        strncpy(port, port_begin + 1, MAXLINE - 1);
        port[MAXLINE - 1] = '\0';
    } else {  // 포트 번호가 명시되어 있지 않은 경우 -> 80번 포트로 처리
        strncpy(hostname, host_begin, MAXLINE - 1);
        hostname[MAXLINE - 1] = '\0';
        strncpy(port, "80", MAXLINE);
    }
}

/**
 * HTTP 요청 메시지를 재구성하는 함수
 * 
 * @param req 재구성된 요청을 저장할 버퍼
 * @param path 요청 경로
 * @param hostname 목적지 호스트명
 * @param other_header 추가할 다른 헤더들
 */
void reassemble(char *req, char *path, char *hostname, char *other_header) {
    snprintf(req, MAXLINE,
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

/**
 * 목적지 서버로부터 받은 응답을 클라이언트에게 전달하는 함수
 * 
 * @param serve_df  목적지 서버와 연결된 파일 디스크립터
 * @param fd        클라이언트와 연결된 파일 디스크립터
 */
void forward_response(int serve_df, int  fd) {
    rio_t serve_rio;
    char response_buf[MAXBUF];

    Rio_readinitb(&serve_rio, serve_df);
    ssize_t n;


    // 목적지 서버가 아직 응답을 보내지 않아 소켓에 읽을 데이터가 없다면
    // 이 프로그램은 Rio_readnb 라인에서 실행을 멈추고 대기(block)한다.
    // 목적지 서버가 응답 데이터를 보내기 시작하면 serve_df 소켓으로 데이터가 수신된다.
    while ((n = Rio_readnb(&serve_rio, response_buf, MAXBUF)) > 0) {
        // 원래의 클라이언트(fd)에게 수신 데이터를 전달
        rio_writen(fd, response_buf, n);
    }
}


/**
 * 클라이언트에게 에러 메시지를 전송하는 함수
 * 
 * @param fd 클라이언트와 연결된 파일 디스크립터
 * @param cause 에러의 원인
 * @param errnum 에러 번호
 * @param shortmsg 짧은 에러 메시지
 * @param longmsg 긴 에러 메시지
 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg){
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    snprintf(body, MAXBUF,
            "<html><title>Proxy Error</title>"
            "<body bgcolor=\"ffffff\">\r\n"
            "%s: %s\r\n"
            "<p>%s: %s\r\n"
            "<hr><em>The Tiny Proxy server</em>\r\n"
            "</body></html>", errnum, shortmsg, longmsg, cause);

    /* Print the HTTP response */
    snprintf(buf, MAXLINE, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf)); // 상태줄 전송 예: HTTP/1.0 404 Not Found
    snprintf(buf, MAXLINE, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf)); // MIME 타입 명시: HTML이라는 것을 알려줌
    snprintf(buf, MAXLINE, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf)); // 본문 길이 알려줌 + 빈 줄로 헤더 종료
    Rio_writen(fd, body, strlen(body)); // 위에서 만든 HTML을 클라이언트에게 전송
}