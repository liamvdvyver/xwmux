#pragma once

#include <X11/Xlib.h>
#include <cassert>
#include <cstdint>
#include <cstdlib>

#include <string>
#include <sys/socket.h>
#include <unordered_map>

#include "xwrapper.h"

using TmuxWindowID = int32_t;
using TmuxPaneID = int32_t;

using TmuxLocation = std::pair<TmuxWindowID, TmuxPaneID>;

constexpr void spawn_window() {
    std::system("tmux new-window -n 'xwmux-app' ''");
}

constexpr void split_window() {
    std::system("tmux split-window ''");
}

constexpr void send_message(std::string msg) {
    std::string tmux_msg = "tmux display-message '";
    tmux_msg.append(msg);
    tmux_msg.push_back('\'');
    std::system(tmux_msg.c_str());
};

constexpr void kill_pane(const TmuxPaneID tm_pane) {
    std::string msg = "tmux kill-pane -t %";
    msg.append(std::to_string(tm_pane));
    std::system(msg.c_str());
}

struct TmuxWorkspace {
  public:
    TmuxWorkspace() = default;

    void add_window(const XState &state, const TmuxPaneID tm_pane,
                    const Window window) {
        m_app_windows[tm_pane] = window;
        state.term_layout.fullscreen_term_position().resize_to(state.display,
                                                               window);
    }

    void set_position(const XState &state, const TmuxPaneID tm_pane,
                      const WindowPosition position) {
        position.resize_to(state.display, m_app_windows[tm_pane]);
    }

    void erase_pane(const TmuxPaneID tm_pane) {
        if (m_app_windows.contains(tm_pane)) {
            // TODO: need to unmap?
            m_app_windows.erase(tm_pane);
        }
    }

    Window at(const TmuxWindowID tm_window) const {
        return m_app_windows.at(tm_window);
    }

    Window &operator[](const TmuxWindowID tm_window) {
        return m_app_windows[tm_window];
    }

    const std::unordered_map<TmuxPaneID, Window> &get_windows() const {
        return m_app_windows;
    }

    void show(const XState &state) const {
        for (auto [p, w] : m_app_windows) {
            XMapWindow(state.display, w);
        }
    }

    void hide(const XState &state) const {
        for (auto [p, w] : m_app_windows) {
            XUnmapWindow(state.display, w);
        }
    }

  private:
    std::unordered_map<TmuxPaneID, Window> m_app_windows;
};

// Maintains correspndences between X windows/workspaces and
struct TmuxXWindowMapping {

  public:
    // default c'tor

    TmuxLocation get_active() const { return m_active; }

    Window current_window() const {
        return m_workspaces.at(m_active.first).at(m_active.second);
    }

    Window current_window() {
        return m_workspaces[m_active.first][m_active.second];
    }

    void add_window(const XState &state, const Window window,
                    const TmuxLocation location) {
        m_workspaces[location.first].add_window(state, location.second, window);
        m_inverse_map[window] = location;
    }

    void remove_window(const Window window) {
        if (m_inverse_map.count(window)) {

            auto [tm_window, tm_pane] = m_inverse_map[window];
            m_workspaces[tm_window].erase_pane(tm_pane);
            m_workspaces.erase(tm_window);
            // m_gui_panes.erase(tm_pane);
            m_inverse_map.erase(window);

            kill_pane(tm_pane);
        }
    }

    const TmuxWorkspace &get_workspace(const TmuxWindowID tm_window) const {
        return m_workspaces.at(tm_window);
    }

    const std::unordered_map<TmuxWindowID, TmuxWorkspace> &
    get_workspaces() const {
        return m_workspaces;
    }

    TmuxWorkspace &operator[](const TmuxWindowID tm_window) {
        return m_workspaces[tm_window];
    }

    Window operator[](const TmuxLocation location) {
        return m_workspaces[location.first][location.second];
    }

    void set_active(const XState &state, const TmuxLocation location) {
        activate_window(state, location.first);
        focus_pane(state, location);
    }

    bool filled(TmuxLocation location) const {
        return m_workspaces.count(location.first) &&
               m_workspaces.at(location.first)
                   .get_windows()
                   .count(location.second);
    }

    bool filled() const { return filled(m_active); }

    bool has_window(Window window) const { return m_inverse_map.count(window); }

  private:
    void activate_window(const XState &state, TmuxWindowID tm_window) {
        if (m_active.first != tm_window) {

            // Deactivate old
            if (m_active.first >= 0) {
                m_workspaces[m_active.first].hide(state);
            }
        }

        m_workspaces[tm_window].show(state);
        m_active.first = tm_window;
    }

    void focus_pane(const XState &state, TmuxLocation location) {
        if (m_active != location) {

            // If workspace has gui window at location, focus it
            bool has_x_window =
                m_workspaces[location.first].get_windows().count(
                    location.second);

            Window target = has_x_window
                                ? m_workspaces[location.first][location.second]
                                : state.term.value_or(state.root);

            XSetInputFocus(state.display, target, 0, 0);
            m_active.second = location.second;
        }
    }

    // Window mapping
    std::unordered_map<TmuxWindowID, TmuxWorkspace> m_workspaces;
    // std::unordered_map<TmuxPaneID, Window> m_gui_panes;
    std::unordered_map<Window, TmuxLocation> m_inverse_map;

    // Focus/active windows
    TmuxLocation m_active{-1, -1};
};
