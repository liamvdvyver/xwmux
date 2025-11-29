#pragma once

#include <unistd.h>
extern "C" {
#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/extensions/XTest.h>
}

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <queue>
#include <unordered_set>

#include "ipc.h"
#include "layout.h"
#include "tmux.h"

constexpr void notify(std::string_view msg) {
    std::system(std::format("notify-send '{}'", msg).c_str());
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

    void handle_event(XEvent &ev) {
        switch (ev.type) {
        case ConfigureNotify:
            handle_x_event<ConfigureNotify>(ev.xconfigure);
            break;
        case MapRequest:
            handle_x_event<MapRequest>(ev.xmaprequest);
            break;
        case UnmapNotify:
            handle_x_event<UnmapNotify>(ev.xunmap);
            break;
        case DestroyNotify:
            handle_x_event<DestroyNotify>(ev.xdestroywindow);
            break;
        case KeyRelease:
            break;
        case KeyPress:
            handle_x_event<KeyPress>(ev);
            break;
        case ClientMessage:
            handle_x_event<ClientMessage>(ev.xclient);
            break;
        case PropertyNotify:
            handle_x_event<PropertyNotify>(ev.xproperty);
            break;
        default:
            break;
        }
    }

    template <> void handle_x_event<ConfigureNotify>(XConfigureEvent &ev) {
        if (ev.window == m_xstate.root) {
            m_xstate.set_resolution({static_cast<size_t>(ev.width),
                                     static_cast<size_t>(ev.height)});
            m_xstate.close_term();
        }
    }

    template <> void handle_x_event<MapRequest>(XMapRequestEvent &ev) {

        Window w = ev.window;
        if (m_xstate.override_redirect(w)) {
        } else if (m_xstate.is_root_term(w)) {
            m_xstate.resolution.fullscreen().resize_to(m_xstate.display, w);
            XLowerWindow(m_xstate.display, w);
            XMapWindow(m_xstate.display, w);
            m_xstate.set_term(w);
            m_xstate.focus_term();
        } else if (!m_pending_windows.count(w) && !m_xstate.iconic(w)) {

            m_window_q.push(w);
            m_pending_windows.insert(w);
            split_window();

            // Watch for name changes
            XSelectInput(m_xstate.display, w, PropertyChangeMask);
        }
    }

    template <> void handle_x_event<UnmapNotify>(XUnmapEvent &ev) {
        if (m_tmux_mapping.has_window(ev.window)) {
            WindowPane &wp = m_tmux_mapping.get(ev.window);
            if (wp.unmap_pending()) {
                wp.notify_unmapped();
            } else {
                m_tmux_mapping.remove_window(ev.window);
                m_xstate.focus_term();
            }
        } else {
            m_pending_windows.erase(ev.window);
        }
    }

    template <> void handle_x_event<DestroyNotify>(XDestroyWindowEvent &ev) {
        if (ev.window == m_xstate.term) {
            m_xstate.term = {};
            m_xstate.open_term();
            m_xstate.focus_term();
        } else if (!m_tmux_mapping.has_window(ev.window)) {
            m_pending_windows.erase(ev.window);
            // Not focused yet, do not focus terminal
        } else {
            m_tmux_mapping.remove_window(ev.window);
            m_xstate.focus_term();
        }
    }

    template <> void handle_x_event<KeyPress>(XEvent &ev) {
        if (ev.xkey.keycode == m_xstate.prefix->keycode &&
            ev.xkey.state == m_xstate.prefix->modifiers) {
            // Release the active grab, and generate focus in event
            XUngrabKeyboard(m_xstate.display, CurrentTime);

            // If terminal is focused (prefix active), overriding gui,
            // redirect the event to the gui window
            if (m_tmux_mapping.overridden()) {

                std::system("tmux send-keys -K escape");

                m_tmux_mapping.release_override(m_xstate);

                // Send to current window
                ev.xkey.display = m_xstate.display;
                ev.xkey.root = XDefaultRootWindow(m_xstate.display);
                ev.xkey.same_screen = True;
                ev.xkey.time = CurrentTime;
                ev.xkey.subwindow = None;
                ev.xkey.window = m_tmux_mapping.current_window();
                XSendEvent(m_xstate.display, ev.xkey.window, 0, 0, &ev);
                return;
            }

            if (!m_xstate.term.has_value()) {
                return;
            }

            // If gui has focus, move to terminal, send prefix and mark
            // overridden
            if (m_tmux_mapping.is_filled()) {

                // Focus terminal, ignore incoming focus notifications
                // to avoid re-focusing gui window
                m_ignore_focus = true;
                m_xstate.focus_term();
                m_ignore_focus = false;

                m_tmux_mapping.override();
            }

            // Ensure tmux gets focus
            m_xstate.sync();

            send_prefix();
        }
    }

    template <> void handle_x_event<PropertyNotify>(XPropertyEvent &ev) {
        if (m_tmux_mapping.has_window(ev.window) &&
            ev.atom == XInternAtom(m_xstate.display, "WM_NAME", 0)) {
            TmuxPaneID pane = m_tmux_mapping.find(ev.window).second;
            name_client(ev.window, pane);
        }
    }

    template <MsgType msg_type> void handle_client_msg(const Msg &msg);

    template <> void handle_client_msg<MsgType::RESOLUTION>(const Msg &msg) {
        m_xstate.term_layout.set_term_resolution(msg.res_chars(), msg.res_px());

        m_xstate.term_layout.set_bar_position(
            static_cast<TmuxBarPosition>(msg.bar_pos()));
    }

    template <> void handle_client_msg<MsgType::PREFIX>(const Msg &msg) {
        m_xstate.set_prefix(msg.mod_kc());
    }

    template <> void handle_client_msg<MsgType::KILL_PANE>(const Msg &msg) {
        (void)msg;
        if (m_tmux_mapping.is_filled()) {
            m_xstate.kill_client(m_tmux_mapping.current_window());
        }
        // Pane should be killed normally on unmap notify.
    }

    template <> void handle_x_event<ClientMessage>(XClientMessageEvent &ev) {
        const Msg msg{ev};
        if (ev.message_type ==
            msg_type_atom(m_xstate.display, MsgType::RESOLUTION)) {
            handle_client_msg<MsgType::RESOLUTION>(msg);
        } else if (ev.message_type ==
                   msg_type_atom(m_xstate.display, MsgType::PREFIX)) {
            handle_client_msg<MsgType::PREFIX>(msg);
        } else if (ev.message_type ==
                   msg_type_atom(m_xstate.display, MsgType::EXIT)) {
            handle_client_msg<MsgType::EXIT>(msg);
        } else if (ev.message_type ==
                   msg_type_atom(m_xstate.display, MsgType::KILL_PANE)) {
            handle_client_msg<MsgType::KILL_PANE>(msg);
        } else if (ev.message_type ==
                   msg_type_atom(m_xstate.display, MsgType::TMUX_POSITION)) {
            handle_client_msg<MsgType::TMUX_POSITION>(msg);
        }
    }

    //--- Client message handlers --------------------------------------------//

    template <> void handle_client_msg<MsgType::EXIT>(const Msg &msg) {
        (void)msg;
        stop();
    }

    template <> void handle_client_msg<MsgType::TMUX_POSITION>(const Msg &msg) {
        m_tmux_mapping.move_pane(msg.tm_location());
        if (msg.focused() && !m_ignore_focus) {

            // Have window to add
            if (!m_window_q.empty() &&
                !m_tmux_mapping.is_filled(msg.tm_location())) {

                Window window = m_window_q.front();
                m_window_q.pop();

                // Already destroyed or window already mapped, so kill the pane
                // TODO: avoid adding to queue twice instead?
                if (!m_pending_windows.count(window) ||
                    m_tmux_mapping.has_window(window)) {
                    kill_pane(msg.tm_location().second);

                } else {
                    m_tmux_mapping.add_window(m_xstate, window,
                                              msg.tm_location());
                    m_pending_windows.erase(window);
                    name_client(window, msg.tm_location().second);
                }
            }

            m_tmux_mapping.set_active(m_xstate, msg.tm_location(),
                                      msg.zoomed());
        }

        // set_position(msg.tm_location(), msg.window_position());
        // Moves the (possible window) at location to term_position
        // If pane doesn't have a window, do nothing
        // If pane is in seperate window, moves the pane
        WindowPosition gui_position = m_xstate.term_layout.term_to_screen_pos(
            m_xstate.term_layout.add_bar(msg.window_position()));

        if (m_tmux_mapping.is_filled(msg.tm_location())) {
            m_tmux_mapping[msg.tm_location()].set_position(m_xstate,
                                                           gui_position);
        }
    }
};
