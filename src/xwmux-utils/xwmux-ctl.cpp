#include <functional>
#include <ipc.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include <optional>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

struct Command {
  public:
    std::string keyword;
    std::string usage_suffix;
    int n_args;
    std::function<std::optional<Msg>(char **)> handle;

    std::optional<Msg> operator()(int argc, char **argv) {
        if (argc != n_args + 2) {
            std::cout << "Usage: wmux-ctl " << keyword << usage_suffix
                      << std::endl;
            return {};
        }

        return handle(argv + 2);
    }
};

Command cmd_res = {.keyword = "res",
                   .usage_suffix = " <rows> <cols>",
                   .n_args = 2,
                   .handle = [](char **argv) {
                       std::size_t rows = std::atoi(argv[0]);
                       std::size_t cols = std::atoi(argv[1]);
                       Msg msg({.rows = rows, .cols = cols});
                       return msg;
                   }};

Command cmd_exit = {.keyword = "exit",
                    .usage_suffix = "",
                    .n_args = 0,
                    .handle = [](char **argv) {
                        (void)argv;
                        Msg msg{};
                        return msg;
                    }};

std::vector<Command> commands = {cmd_exit, cmd_res};

int main(int argc, char **argv) {

    std::string usage = "Usage: xwmux-ctl <cmd>";
    if (argc < 2) {
        std::cout << usage << std::endl;
        return EXIT_FAILURE;
    }

    std::optional<Msg> opt_msg;
    for (auto &cmd : commands) {
        if (cmd.keyword == (std::string)argv[1]) {
            opt_msg = cmd(argc, argv);
            if (!opt_msg.has_value()) {
                return EXIT_FAILURE;
            }
            break;
        }
    }

    if (!opt_msg.has_value()) {
        std::cout << usage << std::endl;
        return EXIT_FAILURE;
    }

    Msg msg = opt_msg.value();

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

    printf("Connected\n");

    if ((send(sock, &msg, sizeof(Msg), 0)) == -1) {
        perror("send");
        exit(EXIT_FAILURE);
    };

    printf("Sent\n");

    // Get resp

    Resp r;
    if ((recv(sock, &r, sizeof(Resp), 0)) == -1) {
        perror("recv");
        exit(EXIT_FAILURE);
    }

    printf("Responded\n");

    close(sock);

    return (r == Resp::OK ? EXIT_SUCCESS : EXIT_FAILURE);
}
