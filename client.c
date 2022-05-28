#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <string.h>
enum Constants {
    MAX_UDP_SIZE = 65507
};

int
main(int argc, char **argv)
{
    int udp_socket;
    if ((udp_socket = socket(PF_INET, SOCK_DGRAM, 0)) == -1) {
        fprintf(stderr, "Socket error!\n");
        _exit(1);
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = PF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    if (argv[1] == NULL || argv[2] == NULL || strcmp(argv[1], "-p") != 0) {
        printf("Please enter port value -p PORT!\n");
        _exit(0);
    }
    addr.sin_port = htons(strtol(argv[2], NULL, 10));
    for (int i = 0; i < 10; i++) {
        char str[10000] = "s 10 13 14 100 16 17 891 11 1 1 1; 100";
        if (sendto(udp_socket, (const char *)str, strlen(str), 0, (const struct sockaddr *)&addr, sizeof(addr)) == -1) {
            fprintf(stderr, "Send error!\n");
        }
    }
    for (int i = 0; i < 10; i++) {
        char str[100] = "p 10 13 14 100 16 17 89; 100";
        if (sendto(udp_socket, (const char *)str, strlen(str), 0, (const struct sockaddr *)&addr, sizeof(addr)) == -1) {
            fprintf(stderr, "Send error!\n");
        }
    }
    for (int i = 0; i < 10; i++) {
        char str[100] = "s 10 13 14 100 89";
        if (sendto(udp_socket, (const char *)str, strlen(str), 0, (const struct sockaddr *)&addr, sizeof(addr)) == -1) {
            fprintf(stderr, "Send error!\n");
        }
    }
    for (int i = 0; i < 10; i++) {
        char str[100] = "p 10 13 14";
        if (sendto(udp_socket, (const char *)str, strlen(str), 0, (const struct sockaddr *)&addr, sizeof(addr)) == -1) {
            fprintf(stderr, "Send error!\n");
        }
    }
    
    return 0;
}
