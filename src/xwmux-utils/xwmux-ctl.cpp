#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

#include "ipc.h"
#include "tmux.h"
#include "tmux_keys.h"

struct Command {
  public:
    virtual std::string keyword() const = 0;
    virtual std::string usage_suffix() const = 0;
    virtual std::string description() const = 0;

    virtual std::optional<Msg> parse(int argc, char **argv, int cur,
                                     Display *dpy) = 0;

    virtual ~Command() {}

    std::string usage() const {
        return "xwmux-ctl " + keyword() + usage_suffix();
    }

    virtual std::optional<Msg> operator()(int argc, char **argv, Display *dpy) {
        std::optional<Msg> ret = parse(argc, argv, 0, dpy);
        if (!ret.has_value()) {
            std::cerr << "Usage: " << usage() << std::endl;
        }

        return ret;
    }

  private:
};

struct InitLayout : Command {
    std::string keyword() const override { return "init"; }
    std::string usage_suffix() const override {
        return " <rows> <cols> <px_w> <px_h> <bar-position>";
    }
    std::string description() const override {
        return "Report terminal geometry and tmux bar position to xwmux.";
    }
    std::optional<Msg> parse(int argc, char **argv, int cur,
                             Display *dpy) override {
        if (cur + 5 != argc - 1) {
            return std::nullopt;
        }
        std::size_t ch_h = std::atoi(argv[cur + 1]);
        std::size_t ch_w = std::atoi(argv[cur + 2]);

        std::size_t px_w = std::atoi(argv[cur + 3]);
        std::size_t px_h = std::atoi(argv[cur + 4]);

        TmuxBarPosition pos = TmuxBarPosition::BOTTOM;
        if (!std::strcmp("top", argv[cur + 5])) {
            pos = TmuxBarPosition::TOP;
        } else if (!std::strcmp("bottom", argv[cur + 5])) {
            pos = TmuxBarPosition::BOTTOM;
        } else if (!std::strcmp("off", argv[cur + 5]) ||
                   !std::strcmp("none", argv[cur + 5])) {
            pos = TmuxBarPosition::NONE;
        } else {
            std::cerr << "bad bar position\n";
        }

        return Msg::report_resolution(dpy, {ch_w, ch_h}, {px_w, px_h}, pos);
    }
};

struct InitPrefix : Command {
    std::string keyword() const override { return "prefix"; }
    std::string usage_suffix() const override { return " <prefix>"; }
    std::string description() const override {
        return "Report the current tmux prefix key to xwmux.";
    }
    std::optional<Msg> parse(int argc, char **argv, int cur,
                             Display *dpy) override {
        if (cur + 1 != argc - 1) {
            return std::nullopt;
        }
        ModifiedKeyCode prefix = tmux_to_keycode(dpy, argv[cur + 1]);
        return Msg::report_prefix(dpy, prefix);
    }
};

struct Exit : Command {
    std::string keyword() const override { return "exit"; }
    std::string usage_suffix() const override { return ""; }
    std::string description() const override {
        return "Ask the running xwmux session to exit.";
    }
    std::optional<Msg> parse(int argc, char **argv, int cur,
                             Display *dpy) override {
        (void)argc;
        (void)argv;
        (void)cur;
        return Msg::exit(dpy);
    }
};

struct Refresh : Command {
    std::string keyword() const override { return "refresh"; }
    std::string usage_suffix() const override { return ""; }
    std::string description() const override {
        return "Internal hook: request a tmux snapshot refresh in xwmux.";
    }
    std::optional<Msg> parse(int argc, char **argv, int cur,
                             Display *dpy) override {
        (void)argc;
        (void)argv;
        (void)cur;
        return Msg::refresh(dpy);
    }
};

struct DumpState : Command {
    std::string keyword() const override { return "dump-state"; }
    std::string usage_suffix() const override { return ""; }
    std::string description() const override {
        return "Print the current tmux/X window mapping state as JSON.";
    }
    std::optional<Msg> parse(int argc, char **argv, int cur,
                             Display *dpy) override {
        (void)argc;
        (void)argv;
        (void)cur;
        return Msg::dump_state(dpy);
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
    } catch (std::invalid_argument &e) {
        return std::nullopt;
    }
}

struct KillPane : Command {
    std::string keyword() const override { return "kill-pane"; }
    std::string usage_suffix() const override {
        return " [ %<pane-id> | focused | orphans ]";
    }
    std::string description() const override {
        return "Kill a tmux pane, the focused pane, or orphaned panes.";
    }
    std::optional<Msg> parse(int argc, char **argv, int cur,
                             Display *dpy) override {
        if (cur + 1 != argc - 1) {
            return std::nullopt;
        }
        TmuxPaneID tm_pane = -1;
        if (!std::strcmp(argv[1], "focused")) {
            return Msg::kill_pane(dpy, tm_pane);
        } else if (!std::strcmp(argv[1], "orphans")) {
            return Msg::kill_ophans(dpy);
        }

        if (argv[1][0] != '%') {
            return std::nullopt;
        }

        tm_pane = stoi(std::string(argv[1] + 1));
        return Msg::kill_pane(dpy, tm_pane);
    }
};

struct NotifyTmuxPosition : Command {
    std::string keyword() const override { return "tmux-position"; }
    std::string usage_suffix() const override {
        return " focused zoomed $<session-id> @<window-id> %<pane-id> "
               "pane_left "
               "pane_top pane_width pane_height dead";
    }
    std::string description() const override {
        return "Internal hook: report a tmux pane's current position to xwmux.";
    }
    std::optional<Msg> parse(int argc, char **argv, int cur,
                             Display *dpy) override {
        if (cur + 10 != argc - 1) {
            std::cout << "wrong args\n";
            return std::nullopt;
        }
        cur++;

        try {
            bool focused = std::stoi(argv[cur++]);
            bool zoomed = std::stoi(argv[cur++]);

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
            bool dead = std::stoi(argv[cur++]);

            return Msg::report_position(
                dpy, loc.value(),
                WindowPosition({pane_left, pane_top}, {pane_left + pane_width,
                                                       pane_top + pane_height}),
                focused, zoomed, dead);

        } catch (std::invalid_argument &e) {
            std::cout << "couldn't get rest\n";
            return std::nullopt;
        }
    }
};

std::optional<std::string> read_state_json(Display *dpy) {
    Atom actual_type;
    int actual_format;
    unsigned long item_count;
    unsigned long bytes_after;
    unsigned char *prop = nullptr;

    if (XGetWindowProperty(dpy, XDefaultRootWindow(dpy), state_json_atom(dpy),
                           0, 1 << 20, False, AnyPropertyType, &actual_type,
                           &actual_format, &item_count, &bytes_after,
                           &prop) != Success) {
        return std::nullopt;
    }

    if (!prop) {
        return std::nullopt;
    }

    std::string json(reinterpret_cast<char *>(prop), item_count);
    XFree(prop);
    return json;
}

std::unique_ptr<Command> parse_cmd(std::string cmd) {
    if (cmd == InitLayout().keyword()) {
        return std::make_unique<InitLayout>();
    } else if (cmd == InitPrefix().keyword()) {
        return std::make_unique<InitPrefix>();
    } else if (cmd == Exit().keyword()) {
        return std::make_unique<Exit>();
    } else if (cmd == Refresh().keyword()) {
        return std::make_unique<Refresh>();
    } else if (cmd == DumpState().keyword()) {
        return std::make_unique<DumpState>();
    } else if (cmd == KillPane().keyword()) {
        return std::make_unique<KillPane>();
    } else if (cmd == NotifyTmuxPosition().keyword()) {
        return std::make_unique<NotifyTmuxPosition>();
    }
    return nullptr;
}

void print_general_help(std::ostream &out) {
    out << "Usage: xwmux-ctl <command> [args]\n\n";
    out << "Commands:\n";

    const Command *commands[] = {
        new Exit(), new Refresh(), new DumpState(), new KillPane(),
        new InitLayout(), new InitPrefix(), new NotifyTmuxPosition(),
    };

    for (const Command *cmd : commands) {
        out << "  " << cmd->usage() << "\n";
        delete cmd;
    }
}

bool is_help_flag(const std::string &arg) {
    return arg == "--help" || arg == "-h";
}

int main(int argc, char **argv) {

    if (argc < 2) {
        print_general_help(std::cerr);
        return EXIT_FAILURE;
    }

    std::string cmd_str = argv[1];
    if (is_help_flag(cmd_str)) {
        print_general_help(std::cout);
        return EXIT_SUCCESS;
    }

    const bool wants_state_dump = cmd_str == "dump-state";

    std::unique_ptr<Command> cmd = parse_cmd(cmd_str);
    if (!cmd) {
        std::cerr << "Unknown command: " << cmd_str << "\n\n";
        print_general_help(std::cerr);
        return EXIT_FAILURE;
    }

    if (argc >= 3 && is_help_flag(argv[2])) {
        std::cout << "Usage: " << cmd->usage() << std::endl;
        return EXIT_SUCCESS;
    }

    Display *dpy = XOpenDisplay(nullptr);
    std::optional<Msg> opt_msg = (*cmd.get())(argc - 1, argv + 1, dpy);

    if (!opt_msg.has_value()) {
        return EXIT_FAILURE;
    }

    if (wants_state_dump) {
        XDeleteProperty(dpy, XDefaultRootWindow(dpy), state_json_atom(dpy));
    }

    XEvent ev = opt_msg.value().get_event();

    if (!XSendEvent(dpy, XDefaultRootWindow(dpy), false,
                    SubstructureRedirectMask, &ev)) {
        return EXIT_FAILURE;
    }

    XSync(dpy, false);

    if (wants_state_dump) {
        for (int i = 0; i < 50; i++) {
            std::optional<std::string> json = read_state_json(dpy);
            if (json.has_value()) {
                std::cout << json.value() << std::endl;
                XCloseDisplay(dpy);
                return EXIT_SUCCESS;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        std::cerr << "Timed out waiting for xwmux state dump" << std::endl;
        XCloseDisplay(dpy);
        return EXIT_FAILURE;
    }

    XCloseDisplay(dpy);
}
