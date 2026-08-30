#include "tmux.h"
#include "log.h"

#include <algorithm>
#include <cstdio>
#include <format>
#include <sstream>
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

std::optional<std::string> TmuxClient::capture(const std::string &command) const {
    FILE *pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return std::nullopt;
    }

    std::string output;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }

    const int status = pclose(pipe);
    if (status != 0) {
        return std::nullopt;
    }
    return output;
}

void TmuxClient::split_window() const {
    if (run(std::format("{} split-window '' \\; break-pane", base_command()))) {
        xwmux::log_error("Failed to spawn window.\n");
    }
}

void TmuxClient::send_message(const std::string_view msg) const {
    std::string fail_str = "";
    if (run(std::format("{} display-message {}", base_command(),
                        shell_quote(msg)))) {
        fail_str = " (FAILED)";
    }
    xwmux::log_info(std::format("Sending message: {}{}\n", msg, fail_str));
}

void TmuxClient::kill_pane(const TmuxPaneID tm_pane) const {
    if (run(std::format("{} kill-pane -t %{}", base_command(), tm_pane))) {
        xwmux::log_error("Failed to kill pane.\n");
    }
}

void TmuxClient::focus_location(const TmuxPaneID tm_pane) const {
    if (run(std::format("{} select-pane -t %{}", base_command(), tm_pane))) {
        xwmux::log_error("Failed to focus location.\n");
    }
}

void TmuxClient::name_pane(const TmuxPaneID tm_pane,
                           const std::string_view name) const {
    if (run(std::format("{} select-pane -t %{} -T {}", base_command(), tm_pane,
                        shell_quote(name)))) {
        xwmux::log_error("Failed to name pane.\n");
    }
}

void TmuxClient::send_prefix() const {
    if (run(std::format(
            "{} send-keys -K $({} show-option prefix | cut -f 2 -d ' ')",
            base_command(), base_command()))) {
        xwmux::log_error("Failed to send prefix.\n");
    }
}

void TmuxClient::send_escape() const {
    if (run(std::format("{} send-keys -K escape", base_command()))) {
        xwmux::log_error("Failed to send escape.\n");
    }
}

bool TmuxClient::client_prefix_active() const {
    const std::string command =
        std::format("{} display-message -p '#{{client_prefix}}'",
                    base_command());
    FILE *pipe = popen(command.c_str(), "r");
    if (!pipe) {
        xwmux::log_error("Failed to query tmux client_prefix state.\n");
        return false;
    }

    char buffer[16] = {};
    const bool ok = fgets(buffer, sizeof(buffer), pipe) != nullptr;
    const int status = pclose(pipe);
    if (!ok || status != 0) {
        xwmux::log_error("Failed to read tmux client_prefix state.\n");
        return false;
    }

    return buffer[0] == '1';
}

bool TmuxClient::find_pane(const TmuxPaneID tm_pane) const {
    return !run(std::format("{} has -t %{}", base_command(), tm_pane));
}

std::optional<TmuxSnapshot> TmuxClient::snapshot() const {
    const std::string command = std::format(
        "{} list-panes -a -F '{}'",
        base_command(),
        "#{window_active} #{pane_active} #{window_zoomed_flag} #{window_id} #{pane_id} #{pane_left} #{pane_top} #{pane_width} #{pane_height} #{pane_dead}");
    std::optional<std::string> output = capture(command);
    if (!output.has_value()) {
        xwmux::log_error("Failed to query tmux pane snapshot.\n");
        return std::nullopt;
    }

    TmuxSnapshot snapshot;
    std::istringstream lines(output.value());
    std::string line;
    while (std::getline(lines, line)) {
        if (line.empty()) {
            continue;
        }

        std::istringstream fields(line);
        int window_active = 0;
        int pane_active = 0;
        int window_zoomed = 0;
        std::string window_id_text;
        std::string pane_id_text;
        size_t pane_left = 0;
        size_t pane_top = 0;
        size_t pane_width = 0;
        size_t pane_height = 0;
        int pane_dead = 0;
        if (!(fields >> window_active >> pane_active >> window_zoomed >>
              window_id_text >> pane_id_text >> pane_left >> pane_top >>
              pane_width >> pane_height >> pane_dead)) {
            xwmux::log_error(
                std::format("Failed to parse tmux pane snapshot line: {}\n", line));
            return std::nullopt;
        }
        if (window_id_text.size() < 2 || window_id_text[0] != '@' ||
            pane_id_text.size() < 2 || pane_id_text[0] != '%') {
            xwmux::log_error(std::format(
                "Unexpected tmux pane ids in snapshot line: {}\n", line));
            return std::nullopt;
        }

        const TmuxWindowID window_id = std::stoi(window_id_text.substr(1));
        const TmuxPaneID pane_id = std::stoi(pane_id_text.substr(1));
        snapshot.panes.push_back({
            .location = {window_id, pane_id},
            .position = WindowPosition({pane_left, pane_top},
                                       {pane_left + pane_width,
                                        pane_top + pane_height}),
            .focused = static_cast<bool>(window_active && pane_active),
            .zoomed = static_cast<bool>(window_zoomed),
            .dead = static_cast<bool>(pane_dead),
        });
    }

    std::sort(snapshot.panes.begin(), snapshot.panes.end(),
              [](const TmuxPaneSnapshot &lhs, const TmuxPaneSnapshot &rhs) {
                  if (lhs.focused != rhs.focused) {
                      return lhs.focused > rhs.focused;
                  }
                  if (lhs.dead != rhs.dead) {
                      return lhs.dead > rhs.dead;
                  }
                  if (lhs.location.first != rhs.location.first) {
                      return lhs.location.first < rhs.location.first;
                  }
                  return lhs.location.second < rhs.location.second;
              });

    return snapshot;
}
