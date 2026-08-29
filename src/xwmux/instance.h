#pragma once

#include <unistd.h>
extern "C" {
#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
}

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <format>
#include <iostream>
#include <queue>
#include <unordered_set>

#include "ipc.h"
#include "log.h"
#include "tmux.h"

class WMInstance {

  public:
    WMInstance() : m_xstate(), m_tmux() {
        init();
        run();
    };

    ~WMInstance() { stop(); }

    void init() {

        if (!m_xstate.display) {
            xwmux::log_error("Failed to open display\n");
            exit(EXIT_FAILURE);
        }

        // set error handler: for startup
        XSetErrorHandler(*startup_error_handler);

        XSelectInput(m_xstate.display, m_xstate.root,
                     StructureNotifyMask | SubstructureRedirectMask |
                         SubstructureNotifyMask);

        m_xstate.sync();

        if (m_existing_wm) {
            xwmux::log_error("Another wm is running\n");
            exit(EXIT_FAILURE);
        }

        XSetErrorHandler(*runtime_handler);
    };

    void run() {

        // Open terminal
        m_xstate.open_term();

        XEvent ev;

        // set cursor
        XDefineCursor(m_xstate.display, m_xstate.root,
                      XCreateFontCursor(m_xstate.display, XC_left_ptr));

        while (!m_stop) {

            // Handle event
            XNextEvent(m_xstate.display, &ev);
            handle_event(ev);
            m_xstate.sync();
        }
    };

    void stop() {
        XCloseDisplay(m_xstate.display);
        for (auto [tm_window, workspace] : m_tmux_mapping.get_workspaces()) {
            for (auto [tm_pane, w] : workspace.get_windows()) {
                m_tmux.kill_pane(tm_pane);
            }
        }
        exit(EXIT_SUCCESS);
    }

  private:
    //--- State --------------------------------------------------------------//

    XState m_xstate;
    TmuxClient m_tmux;
    TmuxXWindowMapping m_tmux_mapping;

    std::queue<Window> m_window_q;
    std::unordered_set<Window> m_pending_windows;

    bool m_stop = false;
    static bool m_existing_wm;

    // When sending tmux commands to pane from gui focus
    bool m_ignore_focus = false;

    // Whether terminal layout/resolution has been initialized from IPC
    bool m_layout_initialized = false;

    //--- Helpers ------------------------------------------------------------//

    void notify(std::string_view msg) const {
        xwmux::log_info(msg);
        m_tmux.send_message(msg);
    }

    void name_client(Window window, TmuxPaneID pane) {
        XTextProperty name;
        XGetWMName(m_xstate.display, window, &name);
        std::string_view name_v(reinterpret_cast<char *>(name.value),
                                name.nitems);
        m_tmux.name_pane(pane, name_v);
        XFree(name.value);
    }

    void write_state_json() {
        std::string json = m_tmux_mapping.to_json();
        if (!json.empty() && json.back() == '}') {
            json.pop_back();
        }

        json += std::format(
            ",\"layout_initialized\":{}",
            m_layout_initialized ? "true" : "false");
        json += std::format(
            ",\"root_terminal_window\":{}",
            m_xstate.term.has_value()
                ? std::to_string(static_cast<unsigned long>(m_xstate.term.value()))
                : std::string("null"));
        json += std::format(
            ",\"pending_window_count\":{}",
            m_pending_windows.size());

        json += ",\"pending_windows\":[";
        bool first_pending = true;
        for (const Window window : m_pending_windows) {
            if (!first_pending) {
                json += ",";
            }
            first_pending = false;
            json += std::to_string(static_cast<unsigned long>(window));
        }
        json += "]";

        json += std::format(
            ",\"prefix\":{{\"initialized\":{},\"grabbed\":{},\"keycode\":{},\"modifiers\":{}}}",
            m_xstate.prefix_initialized ? "true" : "false",
            m_xstate.grabbed ? "true" : "false", m_xstate.prefix.keycode,
            m_xstate.prefix.modifiers);
        json += "}";

        XChangeProperty(m_xstate.display, m_xstate.root,
                        state_json_atom(m_xstate.display), XA_STRING, 8,
                        PropModeReplace,
                        reinterpret_cast<const unsigned char *>(json.c_str()),
                        json.size());
    }

    //--- Error handlers -----------------------------------------------------//

    static int startup_error_handler(_XDisplay *display, XErrorEvent *err) {
        assert(err->error_code == BadAccess);
        m_existing_wm = true;
        (void)display;
        (void)err;
        return EXIT_FAILURE;
    }

    static int runtime_handler(_XDisplay *display, XErrorEvent *err) {

        size_t err_msg_len = 100;
        char *err_msg = (char *)malloc(err_msg_len + 1);
        XGetErrorText(display, err->error_code, err_msg, err_msg_len);

        xwmux::log_error(std::format(
            "X11 error: {} (code={}, request_code={}, minor_code={}, resource_id={}, serial={})\n",
            err_msg, err->error_code, err->request_code, err->minor_code,
            static_cast<unsigned long>(err->resourceid), err->serial));
        free(err_msg);
        return EXIT_SUCCESS;
    }

    //--- Event handlers -----------------------------------------------------//

    template <int XEventType, typename T> void handle_x_event(T &ev);

    void handle_event(XEvent &ev);

    //--- Client message handlers --------------------------------------------//

    template <MsgType msg_type> void handle_client_msg(const Msg &msg);
};
