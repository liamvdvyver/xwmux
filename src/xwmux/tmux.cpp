#include "tmux.h"

#include <format>
#include <string>

void spawn_window() { std::system("tmux new-window -n 'xwmux-app' ''"); }

void split_window() { std::system("tmux split-window ''"); }

void send_message(const std::string_view msg) {
    std::string tmux_msg = "tmux display-message '";
    tmux_msg.append(msg);
    tmux_msg.push_back('\'');
    std::system(tmux_msg.c_str());
};

void kill_pane(const TmuxPaneID tm_pane) {
    std::string msg = "tmux kill-pane -t %";
    msg.append(std::to_string(tm_pane));
    std::system(msg.c_str());
}

void focus_location(const TmuxPaneID tm_pane) {
    std::string msg = "tmux select-pane -t %";
    msg.append(std::to_string(tm_pane));
    std::system(msg.c_str());
}

void name_pane(const TmuxPaneID tm_pane, const std::string_view name) {
    std::system(
        std::format("tmux select-pane -t %{} -T '{}'", tm_pane, name).c_str());
}

void send_prefix() {
    std::system(
        "tmux send-keys -K $(tmux show-option prefix | cut -f 2 -d ' ')");
};
