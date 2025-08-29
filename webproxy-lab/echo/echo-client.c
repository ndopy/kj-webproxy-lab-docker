#include "csapp.h"


/*
 * 에코 클라이언트 메인 함수
 * 서버에 접속하여 사용자의 입력을 서버로 전송하고
 * 서버로부터 받은 응답을 화면에 출력하는 역할을 수행
 */
int main(int argc, char **argv) {
    // 서버와 연결될 소켓 디스크립터 변수
    int clientfd;

    // 접속할 서버의 호스트(IP 주소), 포트 번호, 입력 데이터를 임시 저장할 버퍼
    char *host, *port, buf[MAXLINE];

    // Robust I/O : 안정적인 데이터 수신을 도와주는 패키지
    rio_t rio;

    // 명령행 인자 검사 (호스트명과 포트번호 필요)
    if (argc != 3) {
        // 프로그램을 실행하는 올바른 사용법을 알려주고 프로그램 종료
        fprintf(stderr, "usage: %s <host> <port>\n", argv[0]);
        exit(0);
    }

    host = argv[1]; // 서버 호스트명 저장
    port = argv[2]; // 서버 포트번호 저장

    // 서버와 연결 수립
    clientfd = Open_clientfd(host, port);
    Rio_readinitb(&rio, clientfd);   // rio 도구와 clientfd 소켓을 연결(초기화)한다.

    // 사용자 입력을 받아 서버로 전송하고 응답을 출력
    while (Fgets(buf, MAXLINE, stdin) != NULL) {  // 사용자가 키보드로 한 줄을 입력할 때까지 기다림
        Rio_writen(clientfd, buf, strlen(buf));   // 사용자가 입력한 메시지가 담긴 buf 를 서버로 보낸다. (write)
        Rio_readlineb(&rio, buf, MAXLINE);   // 서버가 다시 되돌려준(echo) 메시지를 읽어서(read) buf에 쓴다.
        Fputs(buf, stdout);                         // 서버로부터 받은 메시지가 담긴 buf 를 화면에 출력한다.
    }
    Close(clientfd); // 연결 종료
    exit(0);
}