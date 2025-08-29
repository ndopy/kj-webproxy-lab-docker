#include "csapp.h"
#include "echo.c"

void echo(int connfd);

int main(int argc, char **argv) {
    /* 클라이언트 접속을 기다리는 소켓, 연결된 클라이언트와 1:1 통신할 소켓 */
    int listenfd, connfd;

    /* 클라이언트의 주소의 크기 */
    socklen_t clientlen;

    /* 접속한 클라이언트의 주소를 저장할 공간 : IPv4, IPv6 주소를 모두 담을 수 있는 넉넉한 크기 */
    struct sockaddr_storage clientaddr;    /* Enough space for any address */

    /* 클라이언트의 호스트 이름, 포트 번호를 저장할 버퍼 */
    char client_hostname[MAXLINE], client_port[MAXLINE];

    if (argc != 2) {  /* 서버를 실행할 때 포트 번호를 제대로 입력했는지 확인 */
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }

    /* 헬퍼 함수 : getaddrinfo, socket, bind, listen 을 한 줄로 처리 */
    // 성공하면 클라이언트의 요청을 받을 준비가 된 소켓 디스크립터를 받는다.
    listenfd = Open_listenfd(argv[1]);

    while (1) {
        /* 클라이언트의 주소 정보를 저장할 크기 지정 */
        clientlen = sizeof(struct sockaddr_storage);

        /*
         * 클라이언트 요청이 올 때까지 멈춰서 기다린다.
         * 클라이언트가 접속하면 해당 클라이언트와 1:1 통신할 전용 소켓의 디스크립터 connfd 를 발급하고
         * 클라이언트의 주소 정보는 clientaddr 에 저장된다.
         * 이 때 실제로 채워진 주소 정보의 정확한 크기를 clientlen 변수에 덮어써서 알려준다.
         */
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

        /*
         * clientaddr 에 저장된 손님의 주소 정보를
         * 사람이 읽을 수 있는 호스트 이름과 포트 번호 문자열로 변환한다.
         *
         * clientlen 에 담긴 실제 주소 크기 정보를 보고
         * clientaddr 에서 정확히 얼마만큼의 데이터를 읽어야 할지 알게 된다.
         */
        Getnameinfo((SA *) &clientaddr, clientlen, client_hostname, MAXLINE,
                    client_port, MAXLINE, 0);
        printf("Connected to (%s, %s)\n", client_hostname, client_port);

        // 정확히 연결된 클라이언트와만 통신하도록 한다.
        echo(connfd);

        // 연결 종료
        Close((connfd));
    }
    exit(0);
}