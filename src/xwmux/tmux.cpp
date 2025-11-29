#include "tmux.h"

#include <format>
#include <string>

void spawn_window() { std::system("tmux new-window -n 'xwmux-app' ''"); }

void split_window() { std::system("tmux split-window ''"); }

void send_message(const std::string_view msg) {
    std::system(std::format("tmux display-message '{}'", msg).c_str());
};

void kill_pane(const TmuxPaneID tm_pane) {
    std::system(std::format("tmux kill-pane -t %{}", tm_pane).c_str());
}

void focus_location(const TmuxPaneID tm_pane) {
    std::system(std::format("tmux select-pane -t %{}", tm_pane).c_str());
}

void name_pane(const TmuxPaneID tm_pane, const std::string_view name) {
    std::system(
        std::format("tmux select-pane -t %{} -T '{}'", tm_pane, name).c_str());
}

void send_prefix() {
    std::system(
        "tmux send-keys -K $(tmux show-option prefix | cut -f 2 -d ' ')");
};

bool find_pane(const TmuxPaneID tm_pane) {
    return !(std::system(std::format("tmux has -t %{}", tm_pane).c_str()));
};
