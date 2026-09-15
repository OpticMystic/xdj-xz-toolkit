#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

/* One bounded transfer between explicit USB-link addresses. The invoking shell
 * chooses stdin/stdout; the network peer can never supply a path or command. */
int main(int argc, char **argv) {
    if (argc != 5 || (strcmp(argv[1], "send") && strcmp(argv[1], "receive"))) return 2;
    char *end;
    unsigned long remaining = strtoul(argv[2], &end, 10);
    if (*end || !remaining || remaining > 64u * 1024u * 1024u) return 2;
    int sending = !strcmp(argv[1], "send");
    struct sockaddr_in local = {0}, peer = {0};
    struct in_addr expected;
    if (inet_pton(AF_INET, argv[3], &local.sin_addr) != 1 || inet_pton(AF_INET, argv[4], &expected) != 1) return 2;
    local.sin_family = AF_INET; local.sin_port = htons(50008);
    int listener = socket(AF_INET, SOCK_STREAM, 0), one = 1;
    if (listener < 0) return 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct timeval timeout = {20, 0};
    setsockopt(listener, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    if (bind(listener, (struct sockaddr *)&local, sizeof(local)) || listen(listener, 1)) { close(listener); return 1; }
    socklen_t length = sizeof(peer);
    int client = accept(listener, (struct sockaddr *)&peer, &length);
    close(listener);
    if (client < 0) return 1;
    if (peer.sin_addr.s_addr != expected.s_addr) { close(client); return 1; }
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    unsigned char buffer[32768];
    while (remaining) {
        size_t want = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        ssize_t got = read(sending ? STDIN_FILENO : client, buffer, want);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) { close(client); return 1; }
        size_t done = 0;
        while (done < (size_t)got) {
            ssize_t wrote = write(sending ? client : STDOUT_FILENO, buffer + done, (size_t)got - done);
            if (wrote < 0 && errno == EINTR) continue;
            if (wrote <= 0) { close(client); return 1; }
            done += (size_t)wrote;
        }
        remaining -= (unsigned long)got;
    }
    if (!sending) fsync(STDOUT_FILENO);
    close(client);
    return 0;
}
