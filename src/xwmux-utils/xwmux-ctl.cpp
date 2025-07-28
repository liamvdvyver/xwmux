#include <ipc.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int main(int argc, char **argv) {

    // Get message (only resolution supported for now)

    std::string usage = "Usage: xwmux-ctl res <rows> <cols>";

    if (argc != 4 || (std::string)argv[1] != "res") {
        std::cout << usage << std::endl;
        return EXIT_FAILURE;
    }

    std::size_t rows = std::atoi(argv[2]);
    std::size_t cols = std::atoi(argv[3]);

    Msg msg({rows, cols});

    // Send

    struct sockaddr_un remote;
    remote.sun_family = AF_UNIX;
    strncpy(remote.sun_path, SOCK_PATH, sizeof(remote.sun_path) - 1);

    int sock;
    if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    };

    if ((connect(sock, (struct sockaddr *)&remote, sizeof(remote))) == -1) {
        perror("connect");
        exit(EXIT_FAILURE);
    };

    if ((send(sock, &msg, sizeof(Msg), 0)) == -1) {
        perror("send");
        exit(EXIT_FAILURE);
    };

    // Get resp

    Resp r;
    if ((recv(sock, &r, sizeof(Resp), 0)) == -1) {
        perror("recv");
        exit(EXIT_FAILURE);
    }

    close(sock);

    return (r == Resp::OK ? EXIT_SUCCESS : EXIT_FAILURE);
}
