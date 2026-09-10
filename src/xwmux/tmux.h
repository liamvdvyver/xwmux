/*
 * Internal represetntations of tmux state, and helpers for interacting with
 * tmux via cli.
 */

#pragma once

#include <cstdint>
#include <cstdlib>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "log.h"
#include "xwrapper.h"

using TmuxWindowID = int32_t;
using TmuxPaneID = int32_t;

using TmuxLocation = std::pair<TmuxWindowID, TmuxPaneID>;

struct TmuxPaneSnapshot {
    TmuxLocation location;
    WindowPosition position;
    bool focused = false;
    bool zoomed = false;
    int pane_pid = 0;
};

struct TmuxSnapshot {
    std::vector<TmuxPaneSnapshot> panes;
};

class TmuxClient {
  public:
    TmuxClient();
    explicit TmuxClient(std::optional<std::string> socket);

    void split_window() const;
    void send_message(const std::string_view msg) const;
    void kill_pane(const TmuxPaneID tm_pane) const;
    void focus_location(const TmuxPaneID tm_pane) const;
    void name_pane(const TmuxPaneID tm_pane, const std::string_view name) const;
    void send_prefix() const;
    void send_escape() const;
    bool client_prefix_active() const;
    bool find_pane(const TmuxPaneID tm_pane) const;
    std::optional<TmuxSnapshot> snapshot() const;

    const std::optional<std::string> &socket() const { return m_socket; }

  private:
    std::optional<std::string> capture(const std::string &command) const;

  private:
    std::optional<std::string> m_socket;

    std::string base_command() const;
    int run(const std::string &command) const;
    static std::string shell_quote(const std::string_view str);
};

// Represents a tmux pane containing an X11 window
struct WindowPane {

    WindowPane() = default;

    WindowPane(const Window window, const bool hidden = false)
        : m_window(window), m_hidden(hidden) {}

    Window get_window() const { return m_window; }
    void set_window(const Window window) { m_window = window; }

    bool is_hidden() const { return m_hidden; }

    bool unmap_pending() const { return m_unmap_req_count; }

    std::string to_json() const {
        return std::format(
            "{{\"window\":{},\"hidden\":{},\"unmap_pending\":{},\"dying\":{}}}",
            static_cast<unsigned long>(m_window), m_hidden ? "true" : "false",
            m_unmap_req_count, m_dying ? "true" : "false");
    }

    void notify_unmapped() { m_unmap_req_count--; }

    void show(const XState &state) {
        XMapWindow(state.display, m_window);
        m_hidden = false;
    }

    void kill_client(Display *display) {
        if (m_dying)
            return;
        m_dying = true;

        // Close window
        // https://nachtimwald.com/2009/11/08/sending-wm_delete_window-client-messages/
        XEvent ev;
        ev.xclient.type = ClientMessage;
        ev.xclient.window = m_window;
        ev.xclient.message_type = XInternAtom(display, "WM_PROTOCOLS", true);
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = XInternAtom(display, "WM_DELETE_WINDOW", false);
        ev.xclient.data.l[1] = CurrentTime;
        XSendEvent(display, m_window, False, NoEventMask, &ev);
    }

    void hide(const XState &state) {
        XUnmapWindow(state.display, m_window);
        m_hidden = true;
        m_unmap_req_count++;
    }

    void set_position(const XState &state, const WindowPosition &pos) {
        pos.resize_to(state.display, m_window);
    }

  private:
    Window m_window;
    bool m_hidden = false;

    // If requested to die, but no destroy notification yet
    // Avoid double sending requests
    bool m_dying = false;

    // To differentiate unmap notifications originating from xwmux and the
    // application, keep track of the number of pending unmap requests.
    size_t m_unmap_req_count = 0;
};

// Represents a tmux window and all associated WindowPanes
struct Workspace {
  public:
    Workspace() = default;

    void add_window(const XState &state, const TmuxPaneID tm_pane,
                    const Window window,
                    const bool initialize_geometry = true) {
        m_app_windows[tm_pane] = window;
        if (initialize_geometry) {
            state.term_layout.fullscreen_term_position().resize_to(
                state.display, window);
        }
    }

    void erase_pane(const TmuxPaneID tm_pane) {
        if (m_app_windows.contains(tm_pane)) {
            m_app_windows.erase(tm_pane);
        }
    }

    WindowPane &operator[](const TmuxWindowID tm_window) {
        return m_app_windows[tm_window];
    }

    const WindowPane operator[](const TmuxWindowID tm_window) const {
        return m_app_windows.at(tm_window);
    }

    const std::unordered_map<TmuxPaneID, WindowPane> &get_windows() const {
        return m_app_windows;
    }

    void show(const XState &state) {
        for (auto &[p, w] : m_app_windows) {
            w.show(state);
        }
    }

    void show(const XState &state, const TmuxPaneID zoomed) {
        for (auto &[p, w] : m_app_windows) {
            if (p == zoomed) {
                w.show(state);
            } else {
                w.hide(state);
            }
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
    std::optional<TmuxLocation> get_active() const {
        if (m_active.first < 0 || m_active.second < 0) {
            return std::nullopt;
        }
        return m_active;
    }

    Window current_window() const {
        return m_workspaces.at(m_active.first)[m_active.second].get_window();
    }

    Window current_window() {
        return m_workspaces[m_active.first][m_active.second].get_window();
    }

    const Workspace &current_workspace() const {
        return m_workspaces.at(m_active.first);
    }

    void add_window(const XState &state, const Window window,
                    const TmuxLocation location,
                    const bool initialize_geometry = true) {
        m_workspaces[location.first].add_window(state, location.second, window,
                                                initialize_geometry);
        m_inverse_map[window] = location.second;
        m_inverse_tm_map[location.second] = location.first;
    }

    void remove_window(const TmuxClient &tmux, const Window window, bool kill_pane = true) {
        if (m_inverse_map.count(window)) {

            TmuxPaneID tm_pane = m_inverse_map[window];
            TmuxWindowID tm_window = m_inverse_tm_map[tm_pane];

            m_workspaces[tm_window].erase_pane(tm_pane);
            if (m_workspaces[tm_window].get_windows().empty()) {
                m_workspaces.erase(tm_window);
            }

            m_inverse_map.erase(window);
            m_inverse_tm_map.erase(tm_pane);

            if (kill_pane) {
                tmux.kill_pane(tm_pane);
            }
        }
    }

    void move_pane(const TmuxLocation loc) {
        const TmuxPaneID new_window = loc.first;
        const TmuxPaneID pane = loc.second;
        if (m_inverse_tm_map.count(pane) &&
            m_inverse_tm_map[pane] != new_window) {

            // Get panes
            const TmuxWindowID old_window = m_inverse_tm_map[pane];
            if (m_workspaces[old_window].get_windows().count(pane)) {
                const WindowPane wp = m_workspaces[old_window][pane];

                // Remove pane from old workspace
                m_workspaces[old_window].erase_pane(pane);
                if (m_workspaces[old_window].get_windows().empty()) {
                    m_workspaces.erase(old_window);
                }

                // Add pane to new workspace
                m_workspaces[new_window][pane] = wp;
                m_inverse_tm_map[pane] = new_window;
            }
        }
    }

    const Workspace &get_workspace(const TmuxWindowID tm_window) const {
        return m_workspaces.at(tm_window);
    }

    const std::unordered_map<TmuxWindowID, Workspace> &get_workspaces() const {
        return m_workspaces;
    }

    Workspace &operator[](const TmuxWindowID tm_window) {
        return m_workspaces[tm_window];
    }

    WindowPane &operator[](const TmuxLocation location) {
        return m_workspaces[location.first][location.second];
    }

    WindowPane &window_for_pane(const TmuxPaneID tm_pane) {
        TmuxWindowID tm_window = m_inverse_tm_map[tm_pane];
        return (*this)[{tm_window, tm_pane}];
    }

    Workspace &workspace(const TmuxWindowID tm_window) {
        return m_workspaces[tm_window];
    }

    void set_active_window(const TmuxWindowID tm_window) {
        m_active.first = tm_window;
    }

    void set_active_pane(const TmuxPaneID tm_pane) { m_active.second = tm_pane; }

    bool is_filled(const TmuxLocation location) const {
        return m_workspaces.count(location.first) &&
               m_workspaces.at(location.first)
                   .get_windows()
                   .count(location.second);
    }

    bool is_filled() const { return is_filled(m_active); }

    bool has_window(const Window window) const {
        return m_inverse_map.count(window);
    }

    bool has_pane(const TmuxPaneID tm_pane) const {
        return m_inverse_tm_map.count(tm_pane);
    }

    TmuxLocation find(const Window window) const {
        const TmuxPaneID p = m_inverse_map.at(window);
        const TmuxWindowID w = m_inverse_tm_map.at(p);
        return {w, p};
    }

    WindowPane &get(const Window window) { return (*this)[find(window)]; }

    // Does not check for membership
    bool is_hidden(const Window window) const {
        TmuxPaneID p = m_inverse_map.at(window);
        TmuxWindowID w = m_inverse_tm_map.at(p);
        return m_workspaces.at(w)[p].is_hidden();
    }

    void kill_client(const Window window, Display *display) {
        TmuxPaneID p = m_inverse_map.at(window);
        TmuxWindowID w = m_inverse_tm_map.at(p);
        return m_workspaces.at(w)[p].kill_client(display);
    }

    // Sets the override flag
    void override() { m_overriden = true; }

    void clear_override() { m_overriden = false; }

    bool overridden() const { return m_overriden; }

    std::string to_json() const {
        std::string json = "{";

        json += "\"active\":";
        if (m_active.first >= 0 && m_active.second >= 0) {
            json += std::format("{{\"window\":{},\"pane\":{}}}", m_active.first,
                                m_active.second);
        } else {
            json += "null";
        }

        json += std::format(",\"overridden\":{}",
                            m_overriden ? "true" : "false");

        json += ",\"workspaces\":{";
        bool first_workspace = true;
        for (const auto &[tm_window, workspace] : m_workspaces) {
            if (!first_workspace) {
                json += ",";
            }
            first_workspace = false;
            json += std::format("\"{}\":{{\"panes\":{{", tm_window);

            bool first_pane = true;
            for (const auto &[tm_pane, window_pane] : workspace.get_windows()) {
                if (!first_pane) {
                    json += ",";
                }
                first_pane = false;
                json += std::format("\"{}\":{}", tm_pane,
                                    window_pane.to_json());
            }

            json += "}}";
        }
        json += "}";

        json += ",\"inverse_tm_map\":{";
        bool first_inverse_tm = true;
        for (const auto &[tm_pane, tm_window] : m_inverse_tm_map) {
            if (!first_inverse_tm) {
                json += ",";
            }
            first_inverse_tm = false;
            json += std::format("\"{}\":{}", tm_pane, tm_window);
        }
        json += "}";

        json += ",\"inverse_map\":{";
        bool first_inverse = true;
        for (const auto &[window, tm_pane] : m_inverse_map) {
            if (!first_inverse) {
                json += ",";
            }
            first_inverse = false;
            json += std::format("\"{}\":{}",
                                static_cast<unsigned long>(window), tm_pane);
        }
        json += "}";

        json += "}";
        return json;
    }

    // TODO: do this in linear time w/ two sorted lists
    std::unordered_set<Window> find_orphans(const TmuxClient &tmux) {
        std::unordered_set<Window> ret;
        for (auto [w, tm_pane] : m_inverse_map) {
            if (!tmux.find_pane(tm_pane)) {
                ret.insert(w);
            }
        }
        return ret;
    }

  private:
    // Window mapping
    std::unordered_map<TmuxWindowID, Workspace> m_workspaces;
    std::unordered_map<TmuxPaneID, TmuxWindowID> m_inverse_tm_map;
    std::unordered_map<Window, TmuxPaneID> m_inverse_map;

    // Focus/active windows
    TmuxLocation m_active{-1, -1};

    // Active location has a gui window which is overridden
    bool m_overriden = false;
};
