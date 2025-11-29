#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>

#include "ipc.h"
#include "tmux.h"
#include "tmux_keys.h"

struct Command {
  public:
    virtual std::string keyword() const = 0;
    virtual std::string usage_suffix() const = 0;

    virtual std::optional<Msg> parse(int argc, char **argv, int cur,
                                     Display *dpy) = 0;

    virtual ~Command() {}

    virtual std::optional<Msg> operator()(int argc, char **argv, Display *dpy) {
        std::optional<Msg> ret = parse(argc, argv, 0, dpy);
        if (!ret.has_value()) {
            std::cerr << "Usage: xwmux-ctl " << keyword() << usage_suffix()
                      << std::endl;
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
        } else if (std::strcmp("bottom", argv[cur + 5])) {
            std::cerr << "bad bar position\n";
        }

        return Msg::report_resolution(dpy, {ch_w, ch_h}, {px_w, px_h}, pos);
    }
};

struct InitPrefix : Command {
    std::string keyword() const override { return "prefix"; }
    std::string usage_suffix() const override { return " <prefix>"; }
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
    std::optional<Msg> parse(int argc, char **argv, int cur,
                             Display *dpy) override {
        (void)argc;
        (void)argv;
        (void)cur;
        return Msg::exit(dpy);
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
    std::string keyword() const override { return "kill-pane"; }
    std::string usage_suffix() const override {
        return " [ %<pane-id> | focused ]";
    }
    std::optional<Msg> parse(int argc, char **argv, int cur,
                             Display *dpy) override {
        if (cur + 1 != argc - 1) {
            return std::nullopt;
        }
        TmuxPaneID tm_pane = -1;
        if (std::strcmp(argv[1], "focused")) {
            tm_pane = stoi(std::string(argv[1]));
        }
        return Msg::kill_pane(dpy, tm_pane);
    }
};

struct NotifyTmuxPosition : Command {
    std::string keyword() const override { return "tmux-position"; }
    std::string usage_suffix() const override {
        return " focused zoomed $<session-id> @<window-id> %<pane-id> "
               "pane_left "
               "pane_top pane_width pane_height";
    }
    std::optional<Msg> parse(int argc, char **argv, int cur,
                             Display *dpy) override {
        if (cur + 9 != argc - 1) {
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

            return Msg::report_position(
                dpy, loc.value(),
                WindowPosition({pane_left, pane_top}, {pane_left + pane_width,
                                                       pane_top + pane_height}),
                focused, zoomed);

        } catch (std::invalid_argument e) {
            std::cout << "couldn't get rest\n";
            return std::nullopt;
        }
    }
};

std::unique_ptr<Command> parse_cmd(std::string cmd) {
    if (cmd == InitLayout().keyword()) {
        return std::make_unique<InitLayout>();
    } else if (cmd == InitPrefix().keyword()) {
        return std::make_unique<InitPrefix>();
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
    Display *dpy = XOpenDisplay(nullptr);
    std::optional<Msg> opt_msg = (*cmd.get())(argc - 1, argv + 1, dpy);

    if (!opt_msg.has_value()) {
        return EXIT_FAILURE;
    }
    XEvent ev = opt_msg.value().get_event();

    if (!XSendEvent(dpy, XDefaultRootWindow(dpy), false,
                    SubstructureRedirectMask, &ev)) {
        return EXIT_FAILURE;
    };

    XCloseDisplay(dpy);
}
