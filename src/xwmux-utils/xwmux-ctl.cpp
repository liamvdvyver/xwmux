#include "tmux.h"
#include <ipc.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include <memory>
#include <optional>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

struct Command {
  public:
    virtual std::string keyword() = 0;
    virtual std::string usage_suffix() = 0;

    virtual int n_args() = 0;
    virtual std::optional<Msg> handle(char **) = 0;

    virtual ~Command() {}

    virtual std::optional<Msg> operator()(int argc, char **argv) {
        std::optional<Msg> ret = handle(argv);
        if (argc != n_args() || !ret.has_value()) {
            std::cerr << "Usage: xwmux-ctl " << keyword() << usage_suffix()
                      << std::endl;
        }

        return ret;
    }
};

struct TellResolution : Command {
    std::string keyword() override { return "res"; }
    std::string usage_suffix() override { return " <rows> <cols>"; }
    int n_args() override { return 3; };
    std::optional<Msg> handle(char **argv) override {
        std::size_t h = std::atoi(argv[1]);
        std::size_t w = std::atoi(argv[2]);
        Msg msg(Resolution{w, h});
        return msg;
    }
};

struct Exit : Command {
    std::string keyword() override { return "exit"; }
    std::string usage_suffix() override { return ""; }
    int n_args() override { return 1; }
    std::optional<Msg> handle(char **argv) override {
        (void)argv;
        return Msg(MsgType::EXIT);
    }
};

struct NotifyTmux : Command {
    std::string keyword() override { return "tmux-event"; }
    std::string usage_suffix() override {
        return " focus $<session-id>@<window-id>%<pane-id>";
    }
    int n_args() override { return 3; }
    std::optional<Msg> handle(char **argv) override {
        (void)argv;
        uint32_t session_id = 0, window_id = 0, pane_id = 0;

        if (strcmp(argv[1], std::string("focus").c_str())) {
            return std::nullopt;
        }

        std::string arg = argv[2];

        size_t a_idx = arg.find('@');
        size_t p_idx = arg.find('%');
        if (arg[0] != '$' || a_idx == arg.npos || p_idx == arg.npos)
            return std::nullopt;

        session_id = std::stoi(arg.substr(1, a_idx));
        window_id = std::stoi(arg.substr(a_idx + 1, p_idx - a_idx));
        pane_id = std::stoi(arg.substr(p_idx + 1));

        (void)session_id;

        Msg msg{std::make_pair(window_id, pane_id)};
        return msg;
    }
};

struct KillPane : Command {
    std::string keyword() override { return "kill-pane"; }
    std::string usage_suffix() override { return " [ %<pane-id> | focused ]"; }
    int n_args() override { return 2; }
    std::optional<Msg> handle(char **argv) override {
        TmuxPaneID tm_pane = -1;
        if (std::strcmp(argv[1], "focused")) {
            tm_pane = stoi(std::string(argv[1]));
        }
        return Msg(tm_pane);
    }
};

std::unique_ptr<Command> parse_cmd(std::string cmd) {
    if (cmd == TellResolution().keyword()) {
        return std::make_unique<TellResolution>();
    } else if (cmd == Exit().keyword()) {
        return std::make_unique<Exit>();
    } else if (cmd == NotifyTmux().keyword()) {
        return std::make_unique<NotifyTmux>();
    } else if (cmd == KillPane().keyword()) {
        return std::make_unique<KillPane>();
    }
    return nullptr;
}

int main(int argc, char **argv) {

    static std::string usage = "Usage: xwmux-ctl <cmd>";

    if (argc < 2) {
        std::cerr << usage << std::endl;
        return EXIT_FAILURE;
    }

    std::string cmd_str = argv[1];

    std::unique_ptr<Command> cmd = parse_cmd(cmd_str);
    if (!cmd) {
        std::cerr << usage << std::endl;
        return EXIT_FAILURE;
    }
    std::optional<Msg> opt_msg = (*cmd.get())(argc - 1, argv + 1);

    if (!opt_msg.has_value()) {
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

    std::cerr << "Connected\n";

    if ((send(sock, &msg, sizeof(Msg), 0)) == -1) {
        perror("send");
        exit(EXIT_FAILURE);
    };

    std::cerr << "Sent\n";

    // Get resp

    Resp r;
    if ((recv(sock, &r, sizeof(Resp), 0)) == -1) {
        perror("recv");
        exit(EXIT_FAILURE);
    }

    std::cerr << "Responded\n";

    close(sock);

    return (r == Resp::OK ? EXIT_SUCCESS : EXIT_FAILURE);
}
