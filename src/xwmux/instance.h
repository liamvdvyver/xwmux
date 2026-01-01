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
#include <iostream>
#include <queue>
#include <unordered_set>

#include "ipc.h"
#include "tmux.h"

constexpr void notify(std::string_view msg) {
    if (std::system(std::format("notify-send '{}'", msg).c_str())) {
        // Fallback: send via tmux
        send_message(msg);
    };
}

class WMInstance {

  public:
    WMInstance() : m_xstate() {
        init();
        run();
    };

    ~WMInstance() { stop(); }

    void init() {

        if (!m_xstate.display) {
            std::cerr << "Failed to open display\n";
            exit(EXIT_FAILURE);
        }

        // set error handler: for startup
        XSetErrorHandler(*startup_error_handler);

        XSelectInput(m_xstate.display, m_xstate.root,
                     StructureNotifyMask | SubstructureRedirectMask |
                         SubstructureNotifyMask);

        m_xstate.sync();

        if (m_existing_wm) {
            std::cerr << "Another wm is running\n";
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
                kill_pane(tm_pane);
            }
        }
        exit(EXIT_SUCCESS);
    }

  private:
    //--- State --------------------------------------------------------------//

    XState m_xstate;
    TmuxXWindowMapping m_tmux_mapping;

    std::queue<Window> m_window_q;
    std::unordered_set<Window> m_pending_windows;

    bool m_stop = false;
    static bool m_existing_wm;

    // When sending tmux commands to pane from gui focus
    bool m_ignore_focus = false;

    //--- Helpers ------------------------------------------------------------//

    void name_client(Window window, TmuxPaneID pane) {
        XTextProperty name;
        XGetWMName(m_xstate.display, window, &name);
        std::string_view name_v(reinterpret_cast<char *>(name.value),
                                name.nitems);
        name_pane(pane, name_v);
        XFree(name.value);
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

        send_message(err_msg);
        return EXIT_SUCCESS;
    }

    //--- Event handlers -----------------------------------------------------//

    template <int XEventType, typename T> void handle_x_event(T &ev);

    void handle_event(XEvent &ev);

    //--- Client message handlers --------------------------------------------//

    template <MsgType msg_type> void handle_client_msg(const Msg &msg);
};
