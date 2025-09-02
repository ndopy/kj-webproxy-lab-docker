/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize, char *method);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);
void sigchld_handler(int sig);

/**
 * @brief HTTP 웹 서버의 메인 실행 함수
 *
 * 지정된 포트에서 클라이언트의 연결을 대기하고, 연결이 수립되면
 * 해당 클라이언트의 HTTP 요청을 처리한다. 서버는 무한 루프로 실행된다.
 *
 * @param argc 명령행 인자의 개수
 * @param argv 명령행 인자 배열 (argv[1]은 포트 번호)
 * @return int 프로그램 종료 코드
 */
int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  // 1. 포트 번호 입력 확인
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  /* SIGCHLD 신호가 오면 sigchld_hanlder 함수를 실행한다. */
  Signal(SIGCHLD, sigchld_handler);

  // 2. 입력 받은 포트 번호로 클라이언트의 연결을 기다리는 서버 소켓 열기
  listenfd = Open_listenfd(argv[1]);

  // 3. 서버가 종료되지 않도록 무한루프 처리
  while (1) {
    clientlen = sizeof(clientaddr);

    // 4. 클라이언트의 연결 요청이 올 때까지 실행을 멈추고 기다린다.
    // 연결이 들어오면 해당 클라이언트와 통신할 새로운 소켓 connfd를 만든다.
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen); // line:netp:tiny:accept

    // 5. 접속한 클라이언트의 IP주소와 포트 번호를 얻는다.
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);

    // 6. 클라이언트와의 실제 통신(요청 처리 및 응답)은 doit 함수에게 맡기기
    doit(connfd);  // line:netp:tiny:doit

    // 7. doit 함수가 끝나면(=통신 완료) 클라이언트와 연결을 닫는다.
    Close(connfd); // line:netp:tiny:close

    printf("---------------------------------\n");
    fflush(stdout);
  }
}


/**
 * @brief 자식 프로세스가 종료될 때 호출되는 시그널 핸들러
 * 
 * SIGCHLD 시그널을 받았을 때 실행되며, 종료된 자식 프로세스들을 정리한다.
 * waitpid를 사용하여 좀비 프로세스가 되는 것을 방지한다.
 * 논블로킹 방식으로 동작하여 서버의 주 실행 흐름을 방해하지 않는다.
 * 
 * @param sig 시그널 번호 (SIGCHLD)
 */
void sigchld_handler(int sig) {
  int old_errno = errno; // 현재 errno 값을 백업
  pid_t pid;

  // 종료된 자식이 하나도 없을 때까지 반복하면서 정리한다.
  while ((pid = waitpid(-1, NULL, WNOHANG)) > 0) {
    // waitpid 자체가 좀비를 정리하는 역할을 하기 때문에 몸체는 비어있어도 된다.
  }

  errno = old_errno;
  return;
}


/**
 * @brief 클라이언트의 HTTP 요청을 처리하는 함수
 * 
 * 클라이언트로부터 받은 HTTP 요청을 분석하고 적절한 응답을 생성하여 전송한다.
 * 정적 컨텐츠(파일)와 동적 컨텐츠(CGI 프로그램)에 대한 요청을 모두 처리할 수 있다.
 * 현재는 GET 메서드만 지원한다.
 * 
 * @param fd 클라이언트와 연결된 소켓 파일 디스크립터
 */
void doit(int fd) {
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  // 1. 소켓(fd)에서 데이터를 읽을 준비하기
  Rio_readinitb(&rio, fd);
  // 2. 클라이언트가 보낸 요청의 첫 줄(요청 라인) 읽기
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\n");
  printf("%s", buf);
  // 3. 요청 라인에서 메서드 ,URI, HTTP 버전을 분리해 각 변수에 저장하기
  sscanf(buf, "%s %s %s", method, uri, version);

  // 4. GET, HEAD 메서드가 아니면 에러를 보낸다.
  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) {
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }

  // 5. 요청 헤더를 읽어서 버린다. (tiny는 헤더 정보를 사용하지 않음)
  read_requesthdrs(&rio);

  // 6. URI를 분석해서 정적 요청인지 동적 요청인지 판단한다.
  is_static = parse_uri(uri, filename, cgiargs);

  // 7. 'filename'에 해당하는 파일을 가져온다. 실패하면 '파일 없음' 에러를 보낸다.
  if (stat(filename, &sbuf) < 0) {
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }

  // 8. 정적 컨텐츠 요청 처리
  if (is_static) {
    // 8-1. 해당 파일이 일반 파일이 아니거나, 읽기 권한이 없으면 에러를 보낸다.
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    // 8-2. serve_static 함수를 호출해 파일을 클라이언트에게 보낸다.
    serve_static(fd, filename, sbuf.st_size, method);
  }
  // 9. 동적 컨텐츠 요청 처리
  else {
    // 9-1. 해당 파일이 일반 파일이 아니거나, 실행 권한이 없으면 에러를 보낸다.
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }
    // 9-2. serve_dynamic 함수를 호출해 프로그램을 실행하고, 결과를 클라이언트에게 보낸다.
    serve_dynamic(fd, filename, cgiargs, method);
  }
}


/**
 * @brief URI를 파싱하여 파일 이름과 CGI 인자를 추출하는 함수
 *
 * URI를 분석하여 정적 컨텐츠인지 동적 컨텐츠인지 판단하고,
 * 파일 이름과 CGI 인자(있는 경우)를 추출한다.
 *
 * @param uri 클라이언트가 요청한 URI
 * @param filename 실제 파일 경로를 저장할 버퍼
 * @param cgiargs CGI 인자를 저장할 버퍼
 * @return int 정적 컨텐츠면 1, 동적 컨텐츠면 0
 */
int parse_uri(char *uri, char *filename, char *cgiargs) {
  char *ptr;

  // 1. URI에 "cgi-bin" 문자열이 있는지 확인한다.
  if (!strstr(uri, "cgi-bin")) { // cgi-bin 이 없으면 정적 컨텐츠
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);

    // URI 가 '/'로 끝나면 기본 파일 'home.html'을 요청한 것으로 간주한다.
    if (uri[strlen(uri)-1] == '/') {
      strcat(filename, "home.html");
    }
    
    return 1;   // static 이라는 의미로 반환
  }
  else { // cgi-bin 이 있으면 동적 컨텐츠
    ptr = index(uri, '?');
    // '?' 문자를 기준으로 뒷 부분은 CGI 인자(cgiargs), 앞부분은 실행 파일(filname)로 분리
    if (ptr) {
      strcpy(cgiargs, ptr+1);
      *ptr = '\0';
    } else {
      strcpy(cgiargs, "");
    }
    
    strcpy(filename, ".");
    strcat(filename, uri);
    
    return 0;  // dynamic 이라는 의미로 반환
  }
}


/**
 * @brief 정적 컨텐츠를 클라이언트에게 전송하는 함수
 *
 * 요청된 파일의 내용을 읽어서 HTTP 응답으로 클라이언트에게 전송한다.
 * 응답 헤더와 파일 내용을 포함한다.
 *
 * @param fd 클라이언트와 연결된 소켓 파일 디스크립터
 * @param filename 전송할 파일의 경로
 * @param filesize 파일의 크기
 */
void serve_static(int fd, char *filename, int filesize, char *method) {
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXLINE];

  // 1. 파일 확장자를 보고 HTTP 응답 헤더에 들어갈 파일 타입(Content-Type)을 결정한다.
  get_filetype(filename, filetype);

  // 2. HTTP 응답 헤더를 'buf' 버퍼에 만든다. (상태 코드, 파일 크기, 파일 타입 등)
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  // 3. 만들어진 응답 헤더를 클라이언트에게 보낸다.
  Rio_writen(fd, buf, strlen(buf));

  // 문제 11.11 : HEAD 메서드일 때는 응답 헤더만 보내고 본문은 보내지 않는다.
  if (!strcasecmp(method, "HEAD")) {
    return;
  }

  // 파일을 담을 빈 메모리 공간 생성
  srcp = Malloc(filesize);

  // 4. 보낼 파일을 연다.
  srcfd = Open(filename, O_RDONLY, 0);

  // 5. 파일의 내용을 빈 메모리 공간에 복사
  // (1) 어떤 파일에서 읽어서(=파일 번호표) (2) 어디에 담을지(=메모리 주소) (3) 얼마나 많이 읽을지(=파일 크기)
  Rio_readn(srcfd, srcp, filesize);

  // 6. 파일 디스크립터는 이제 필요없기 때문에 닫는다.
  Close(srcfd);

  // 7. 메모리에 매핑된 파일 내용을 클라이언트에게 전부 보낸다.
  Rio_writen(fd, srcp, filesize);

  // 8. 할당받았던 메모리 해제
  free(srcp);
}


/**
 * @brief 동적 컨텐츠를 생성하여 클라이언트에게 전송하는 함수
 *
 * CGI 프로그램을 실행하고 그 출력을 클라이언트에게 전송한다.
 * 자식 프로세스를 생성하여 CGI 프로그램을 실행한다.
 *
 * @param fd 클라이언트와 연결된 소켓 파일 디스크립터
 * @param filename 실행할 CGI 프로그램의 경로
 * @param cgiargs CGI 프로그램에 전달할 인자
 */
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method) {
  char buf[MAXLINE], *emptylist[] = { NULL };

  // 1. 기본적인 성공 응답 헤더를 먼저 보낸다.
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server \r\n");
  Rio_writen(fd, buf, strlen(buf));

  // 문제 11.11 : HEAD 메서드일 때는 응답 헤더만 보내고 본문은 보내지 않는다.
  if (!strcasecmp(method, "HEAD")) {
    return;
  }

  // 2. Fork()로 현재 프로세스를 복제하여 자식 프로세스를 만든다.
  if (Fork() == 0) { // 자식 프로세스만 이 코드 블록을 실행한다.
    // 3. CGI 인자를 `QUERY_STRING` 환경 변수로 설정한다.
    // CGI 프로그램이 이 값을 읽어 사용한다.
    setenv("QUERY_STRING", cgiargs, 1);
    // 4. `dup2` 를 사용해, 표준 출력(원래 화면으로 향함)의 방향을 클라이언트 소켓(fd)으로 바꾼다.
    Dup2(fd, STDOUT_FILENO);
    // 5. `execve`로 지정된 CGI 프로그램을 실행한다.
    //    이제 이 프로그램이 화면에 출력하는 모든 내용은 소켓을 통해 클라이언트에게 전달된다.
    Execve(filename, emptylist, environ);
  }
  // Wait(NULL);   /* 숙제 문제 11.18 : 주석 처리 */
}


/**
 * @brief 파일의 MIME 타입을 결정하는 함수
 *
 * 파일 확장자를 검사하여 해당하는 MIME 타입을 결정한다.
 * 지원하는 타입: HTML, GIF, PNG, JPG, 일반 텍스트
 *
 * @param filename 파일 이름
 * @param filetype MIME 타입을 저장할 버퍼
 */
void get_filetype(char *filename, char *filetype) {
  if (strstr(filename, ".html")) {
    strcpy(filetype, "text/html");
  } else if (strstr(filename, ".gif")) {
    strcpy(filetype, "image/gif");
  } else if (strstr(filename, ".png")) {
    strcpy(filetype, "image/png");
  } else if (strstr(filename, ".jpg")) {
    strcpy(filetype, "image/jpeg");
  } else if (strstr(filename, ".mpeg")) {
    strcpy(filetype, "video/mpeg");
  } else {
    strcpy(filetype, "text/plain");
  }
}


/**
 * @brief HTTP 요청 헤더를 읽는 함수
 *
 * 클라이언트가 전송한 모든 HTTP 요청 헤더를 읽어서 출력한다.
 * 빈 줄(CRLF)이 나올 때까지 계속해서 헤더를 읽는다.
 *
 * @param rp 요청을 읽기 위한 rio 버퍼 구조체
 */
void read_requesthdrs(rio_t *rp) {
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);

  while (strcmp(buf, "\r\n")) {
    printf("%s", buf);
    fflush(stdout);   // 버퍼를 즉시 비우기
    Rio_readlineb(rp, buf, MAXLINE);
  }
  return;
}


/**
 * @brief 클라이언트에게 에러 메시지를 전송하는 함수
 *
 * HTTP 오류 응답을 생성하여 클라이언트에게 전송한다.
 * 응답은 상태 라인, 헤더, HTML 형식의 에러 메시지 본문을 포함한다.
 *
 * @param fd 클라이언트와 연결된 소켓 파일 디스크립터
 * @param cause 오류의 원인
 * @param errnum HTTP 상태 코드
 * @param shortmsg 짧은 오류 메시지
 * @param longmsg 자세한 오류 설명
 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n",body);
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