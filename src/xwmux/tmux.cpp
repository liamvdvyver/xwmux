#include "tmux.h"
#include "log.h"

#include <format>
#include <string>

void split_window() {
    if (std::system("tmux split-window ''")) {
        log_msg("Failed to spawn window.\n");
    };
}

void send_message(const std::string_view msg) {
    std::string fail_str = "";
    if (std::system(std::format("tmux display-message '{}'", msg).c_str())) {
        fail_str = " (FAILED)";
    };
    log_msg(std::format("Sending message: {}{}\n", msg, fail_str));
}

void kill_pane(const TmuxPaneID tm_pane) {
    if (std::system(std::format("tmux kill-pane -t %{}", tm_pane).c_str())) {
        log_msg("Failed to kill pane.\n");
    };
}

void focus_location(const TmuxPaneID tm_pane) {
    if (std::system(std::format("tmux select-pane -t %{}", tm_pane).c_str())) {
        log_msg("Failed to focus location.\n");
    };
}

void name_pane(const TmuxPaneID tm_pane, const std::string_view name) {
    if (std::system(
            std::format("tmux select-pane -t %{} -T '{}'", tm_pane, name)
                .c_str())) {
        log_msg("Failed to name pane.\n");
    };
}

void send_prefix() {
    if (std::system(
        "tmux send-keys -K $(tmux show-option prefix | cut -f 2 -d ' ')")) {
        log_msg("Failed to send prefix.\n");
    };
};

bool find_pane(const TmuxPaneID tm_pane) {
    return !(std::system(std::format("tmux has -t %{}", tm_pane).c_str()));
};
