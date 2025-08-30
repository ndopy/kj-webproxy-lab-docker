#include <stdio.h>
#include <unistd.h>

int main() {
    printf("Content-type: text/plain\r\n\r\n");
    printf("Starting slow job...\n");
    fflush(stdout);

    sleep(5);

    printf("Slow job finished!\n");
    fflush(stdout);

    return 0;
}