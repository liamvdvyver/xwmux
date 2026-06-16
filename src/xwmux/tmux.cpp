#include "tmux.h"
#include "log.h"

#include <format>
#include <string>

TmuxClient::TmuxClient()
    : TmuxClient([]() -> std::optional<std::string> {
          const char *socket = std::getenv("XWMUX_TMUX_SOCKET");
          if (!socket || socket[0] == '\0') {
              return std::nullopt;
          }
          return std::string(socket);
      }()) {}

TmuxClient::TmuxClient(std::optional<std::string> socket)
    : m_socket(std::move(socket)) {}

std::string TmuxClient::shell_quote(const std::string_view str) {
    std::string quoted{"'"};
    for (char c : str) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(c);
        }
    }
    quoted.push_back('\'');
    return quoted;
}

std::string TmuxClient::base_command() const {
    if (!m_socket.has_value()) {
        return "tmux";
    }
    return std::format("tmux -S {}", shell_quote(m_socket.value()));
}

int TmuxClient::run(const std::string &command) const {
    return std::system(command.c_str());
}

void TmuxClient::split_window() const {
    if (run(std::format("{} split-window '' \\; break-pane", base_command()))) {
        log_msg("Failed to spawn window.\n");
    }
}

void TmuxClient::send_message(const std::string_view msg) const {
    std::string fail_str = "";
    if (run(std::format("{} display-message {}", base_command(),
                        shell_quote(msg)))) {
        fail_str = " (FAILED)";
    }
    log_msg(std::format("Sending message: {}{}\n", msg, fail_str));
}

void TmuxClient::kill_pane(const TmuxPaneID tm_pane) const {
    if (run(std::format("{} kill-pane -t %{}", base_command(), tm_pane))) {
        log_msg("Failed to kill pane.\n");
    }
}

void TmuxClient::focus_location(const TmuxPaneID tm_pane) const {
    if (run(std::format("{} select-pane -t %{}", base_command(), tm_pane))) {
        log_msg("Failed to focus location.\n");
    }
}

void TmuxClient::name_pane(const TmuxPaneID tm_pane,
                           const std::string_view name) const {
    if (run(std::format("{} select-pane -t %{} -T {}", base_command(), tm_pane,
                        shell_quote(name)))) {
        log_msg("Failed to name pane.\n");
    }
}

void TmuxClient::send_prefix() const {
    if (run(std::format(
            "{} send-keys -K $({} show-option prefix | cut -f 2 -d ' ')",
            base_command(), base_command()))) {
        log_msg("Failed to send prefix.\n");
    }
}

void TmuxClient::send_escape() const {
    if (run(std::format("{} send-keys -K escape", base_command()))) {
        log_msg("Failed to send escape.\n");
    }
}

bool TmuxClient::find_pane(const TmuxPaneID tm_pane) const {
    return !run(std::format("{} has -t %{}", base_command(), tm_pane));
}
