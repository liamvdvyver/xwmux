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

constexpr void split_window() { std::system("tmux split-window ''"); }

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

constexpr void focus_location(const TmuxPaneID tm_pane) {
    std::string msg = "tmux select-pane -t %";
    msg.append(std::to_string(tm_pane));
    std::system(msg.c_str());
}

// Represents a tmux pane containing an X11 window
struct WindowPane {

    WindowPane() = default;

    WindowPane(const Window window, const bool hidden = false)
        : m_window(window), m_hidden(hidden) {}

    Window get_window() const { return m_window; }
    void set_window(const Window window) { m_window = window; }

    bool hidden() const { return m_hidden; }

    void show(const XState &state) {
        XMapWindow(state.display, m_window);
        m_hidden = false;
    }

    void hide(const XState &state) {
        XUnmapWindow(state.display, m_window);
        m_hidden = true;
    }

    void set_position(const XState &state, const WindowPosition &pos) {
        pos.resize_to(state.display, m_window);
    }

  private:
    Window m_window;
    bool m_hidden;
};

// Represents a tmux window and all associated WindowPanes
struct Workspace {
  public:
    Workspace() = default;

    void add_window(const XState &state, const TmuxPaneID tm_pane,
                    const Window window) {
        m_app_windows[tm_pane] = window;
        state.term_layout.fullscreen_term_position().resize_to(state.display,
                                                               window);
    }

    void erase_pane(const TmuxPaneID tm_pane) {
        if (m_app_windows.contains(tm_pane)) {
            m_app_windows.erase(tm_pane);
        }
    }

    WindowPane at(const TmuxWindowID tm_window) const {
        return m_app_windows.at(tm_window);
    }

    WindowPane &operator[](const TmuxWindowID tm_window) {
        return m_app_windows[tm_window];
    }

    const std::unordered_map<TmuxPaneID, WindowPane> &get_windows() const {
        return m_app_windows;
    }

    void show(const XState &state) {
        for (auto &[p, w] : m_app_windows) {
            w.show(state);
        }
    }

    void hide(const XState &state) {
        for (auto &[p, w] : m_app_windows) {
            w.hide(state);
        }
    }

  private:
    std::unordered_map<TmuxPaneID, WindowPane> m_app_windows;
};

struct TmuxXWindowMapping {

  public:
    // default c'tor

    TmuxLocation get_active() const { return m_active; }

    Window current_window() const {
        return m_workspaces.at(m_active.first).at(m_active.second).get_window();
    }

    Window current_window() {
        return m_workspaces[m_active.first][m_active.second].get_window();
    }

    const Workspace &current_workspace() const {
        return m_workspaces.at(m_active.first);
    }

    void add_window(const XState &state, const Window window,
                    const TmuxLocation location) {
        m_workspaces[location.first].add_window(state, location.second, window);
        m_inverse_map[window] = location.second;
        m_inverse_tm_map[location.second] = location.first;
    }

    void remove_window(const Window window) {
        if (m_inverse_map.count(window)) {

            TmuxPaneID tm_pane = m_inverse_map[window];
            TmuxWindowID tm_window = m_inverse_tm_map[tm_pane];

            m_workspaces[tm_window].erase_pane(tm_pane);
            if (m_workspaces[tm_window].get_windows().empty()) {
                m_workspaces.erase(tm_window);
            }

            m_inverse_map.erase(window);
            m_inverse_tm_map.erase(tm_pane);

            kill_pane(tm_pane);
        }
    }
    
    // void move_pane(const TmuxPaneID pane, const TmuxWindowID new_window) {
    //     if (m_inverse_tm_map.count(pane) && m_inverse_tm_map[pane] != new_window) {
    //         const TmuxWindowID old_window = m_inverse_tm_map[pane];
    //         const WindowPane wp = m_workspaces[old_window][pane];
    //
    //         m_workspaces[old_window].erase_pane(pane);
    //         if (m_workspaces[old_window].get_windows().empty()) {
    //             m_workspaces.erase(old_window);
    //         }
    //
    //         m_workspaces[new_window][pane] = wp;
    //     }
    // }

    const Workspace &get_workspace(const TmuxWindowID tm_window) const {
        return m_workspaces.at(tm_window);
    }

    const std::unordered_map<TmuxWindowID, Workspace> &
    get_workspaces() const {
        return m_workspaces;
    }

    Workspace &operator[](const TmuxWindowID tm_window) {
        return m_workspaces[tm_window];
    }

    Window operator[](const TmuxLocation location) {
        return m_workspaces[location.first][location.second].get_window();
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

    // Does not check for membership
    bool hidden(Window window) const {
        TmuxPaneID p = m_inverse_map.at(window);
        TmuxWindowID w = m_inverse_tm_map.at(p);
        return m_workspaces.at(w).at(p).hidden();
    }

    // Sets the override flag
    void override() { m_overriden = true; }

    // Unsets the override flag and re-focuses the active window
    void release_override(const XState &state) {
        focus_pane(state, m_active, true);
    }

    bool overridden() const { return m_overriden; }

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

    // Clears override if switch takes place
    void focus_pane(const XState &state, TmuxLocation location,
                    bool redundant_refocus = false) {

        if (m_active != location || redundant_refocus) {

            // If workspace has gui window at location, focus it
            bool has_x_window =
                m_workspaces[location.first].get_windows().count(
                    location.second);

            Window target = has_x_window
                                ? m_workspaces[location.first][location.second].get_window()
                                : state.term.value_or(state.root);

            XSetInputFocus(state.display, target, 0, 0);
            m_active.second = location.second;

            m_overriden = false;
        }
    }

    // Window mapping
    std::unordered_map<TmuxWindowID, Workspace> m_workspaces;
    std::unordered_map<TmuxPaneID, TmuxWindowID> m_inverse_tm_map;
    std::unordered_map<Window, TmuxPaneID> m_inverse_map;

    // Focus/active windows
    TmuxLocation m_active{-1, -1};

    // Active location has a gui window which is overridden
    bool m_overriden;
};
