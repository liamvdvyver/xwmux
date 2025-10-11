#include "tmux.h"
#include <ipc.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include <memory>
#include <optional>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

struct Command {
  public:
    virtual std::string keyword() = 0;
    virtual std::string usage_suffix() = 0;

    virtual std::optional<Msg> handle(int argc, char **argv, int cur) = 0;

    virtual ~Command() {}

    virtual std::optional<Msg> operator()(int argc, char **argv) {
        std::optional<Msg> ret = handle(argc, argv, 0);
        if (!ret.has_value()) {
            std::cerr << "Usage: xwmux-ctl " << keyword() << usage_suffix()
                      << std::endl;
        }

        return ret;
    }

  private:
};

struct InitLayout : Command {
    std::string keyword() override { return "init"; }
    std::string usage_suffix() override {
        return " <rows> <cols> <bar-position>";
    }
    std::optional<Msg> handle(int argc, char **argv, int cur) override {
        if (cur + 3 != argc - 1) {
            return std::nullopt;
        }
        std::size_t h = std::atoi(argv[cur + 1]);
        std::size_t w = std::atoi(argv[cur + 2]);

        TmuxBarPosition pos = TmuxBarPosition::BOTTOM;
        if (!std::strcmp("top", argv[cur + 3])) {
            pos = TmuxBarPosition::TOP;
        } else if (std::strcmp("bottom", argv[cur + 3])) {
            std::cerr << "Bad bar position\n";
        }

        Msg msg(TermInitLayout{{w, h}, pos});
        return msg;
    }
};

struct Exit : Command {
    std::string keyword() override { return "exit"; }
    std::string usage_suffix() override { return ""; }
    std::optional<Msg> handle(int argc, char **argv, int cur) override {
        (void)argc;
        (void)argv;
        (void)cur;
        return Msg(MsgType::EXIT);
    }
};

std::optional<TmuxLocation> get_loc(int argc, char **argv, int cur) {
    if (cur + 2 >= argc) {
        return std::nullopt;
    }
    try {
        uint32_t window_id = 0, pane_id = 0;
        window_id = std::stoi(argv[cur + 1] + 1);
        pane_id = std::stoi(argv[cur + 2] + 1);
        return std::make_pair(window_id, pane_id);
    } catch (std::invalid_argument e) {

        return std::nullopt;
    }
}

struct KillPane : Command {
    std::string keyword() override { return "kill-pane"; }
    std::string usage_suffix() override { return " [ %<pane-id> | focused ]"; }
    std::optional<Msg> handle(int argc, char **argv, int cur) override {
        if (cur + 1 != argc - 1) {
            return std::nullopt;
        }
        TmuxPaneID tm_pane = -1;
        if (std::strcmp(argv[1], "focused")) {
            tm_pane = stoi(std::string(argv[1]));
        }
        return Msg(tm_pane);
    }
};

struct NotifyTmuxPosition : Command {
    std::string keyword() override { return "tmux-position"; }
    std::string usage_suffix() override {
        return " focused $<session-id> @<window-id> %<pane-id> pane_left "
               "pane_top pane_width pane_height";
    }
    std::optional<Msg> handle(int argc, char **argv, int cur) override {
        if (cur + 8 != argc - 1) {
            std::cout << "wrong args\n";
            return std::nullopt;
        }
        cur++;

        try {
            bool focused = std::stoi(argv[cur++]);

            std::optional<TmuxLocation> loc = get_loc(argc, argv, cur);
            if (!loc.has_value()) {
                std::cout << "couldn't get loc\n";
                return std::nullopt;
            }
            cur += 3;

            size_t pane_top, pane_left, pane_width, pane_height;
            pane_left = std::stoi(argv[cur++]);
            pane_top = std::stoi(argv[cur++]);
            pane_width = std::stoi(argv[cur++]);
            pane_height = std::stoi(argv[cur++]);

            return TmuxPanePosition{
                .location = loc.value(),
                .position = WindowPosition(
                    {pane_left, pane_top},
                    {pane_left + pane_width, pane_top + pane_height}),
                .focused = focused};

        } catch (std::invalid_argument e) {
            std::cout << "couldn't get rest\n";
            return std::nullopt;
        }
    }
};

std::unique_ptr<Command> parse_cmd(std::string cmd) {
    if (cmd == InitLayout().keyword()) {
        return std::make_unique<InitLayout>();
    } else if (cmd == Exit().keyword()) {
        return std::make_unique<Exit>();
    } else if (cmd == KillPane().keyword()) {
        return std::make_unique<KillPane>();
    } else if (cmd == NotifyTmuxPosition().keyword()) {
        return std::make_unique<NotifyTmuxPosition>();
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
