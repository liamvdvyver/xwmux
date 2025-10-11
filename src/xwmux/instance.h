#pragma once

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>

#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <pthread.h>
#include <queue>
#include <unordered_set>

#include "ipc.h"
#include "layout.h"
#include "listen.h"
#include "tmux.h"

constexpr void notify(std::string_view msg) {
    std::string cmd = "notify-send '";
    cmd.append(msg);
    cmd.push_back('\'');
    std::system(cmd.c_str());
}

const std::string ROOT_CLASS = "xwmux_root";

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
                     SubstructureRedirectMask | SubstructureNotifyMask);

        sync();

        if (m_existing_wm) {
            std::cerr << "Another wm is running\n";
            exit(EXIT_FAILURE);
        }

        XSetErrorHandler(*runtime_handler);
    };

    void run() {

        // Open terminal
        open_term();

        XEvent ev;

        // run listener
        pthread_t listener;
        pthread_create(&listener, nullptr, Listener::operator(), (void *)this);

        // set cursor
        XDefineCursor(m_xstate.display, m_xstate.root,
                      XCreateFontCursor(m_xstate.display, XC_left_ptr));

        while (!m_stop.load(std::memory_order_relaxed)) {

            // Handle event
            XNextEvent(m_xstate.display, &ev);
            handle_event(ev);
            sync();
        }

        // Kill listener
        pthread_join(listener, nullptr);
    };

    //--- Handle IPC events --------------------------------------------------//

    const XState &get_xstate() { return m_xstate; }

    void push_msg(Msg msg) {
        std::lock_guard lk = lock();
        m_msg_q.push(msg);
    }

    void set_term_resolution(Resolution resoltuion) {
        m_xstate.term_layout.set_term_resolution(resoltuion);
    }

    void stop() {
        XCloseDisplay(m_xstate.display);
        for (auto [tm_window, workspace] : m_tmux_mapping.get_workspaces()) {
            for (auto [tm_pane, w] : workspace.get_windows()) {
                kill_pane(tm_pane);
            }
        }
        exit(EXIT_SUCCESS);
    }

    void focus_tmux(TmuxLocation location) {
        // Have window to add
        if (!m_window_q.empty() && !m_tmux_mapping.filled(location)) {

            Window window = m_window_q.front();
            m_window_q.pop();

            // Already destroyed or window already mapped, so kill the pane
            // TODO: avoid adding to queue twice instead?
            if (!m_pending_windows.count(window) ||
                m_tmux_mapping.has_window(window)) {
                kill_pane(location.second);

            } else {
                m_tmux_mapping.add_window(m_xstate, window, location);
                m_pending_windows.erase(window);
            }
        }

        m_tmux_mapping.set_active(m_xstate, location);
    }

    void kill_client(Window window) {
        // Close window
        // https://nachtimwald.com/2009/11/08/sending-wm_delete_window-client-messages/
        XEvent ev;
        ev.xclient.type = ClientMessage;
        ev.xclient.window = window;
        ev.xclient.message_type =
            XInternAtom(m_xstate.display, "WM_PROTOCOLS", true);
        ev.xclient.format = 32;
        ev.xclient.data.l[0] =
            XInternAtom(m_xstate.display, "WM_DELETE_WINDOW", false);
        ev.xclient.data.l[1] = CurrentTime;
        XSendEvent(m_xstate.display, window, False, NoEventMask, &ev);
    }

    // Kills the focused client, if any
    void kill_pane_client() {
        if (m_tmux_mapping.filled()) {
            kill_client(m_tmux_mapping.current_window());
        }
        // Pane should be killed normally on unmap notify.
    }

    void set_position(TmuxLocation location, WindowPosition term_position) {
        WindowPosition gui_position = m_xstate.term_layout.term_to_screen_pos(
            m_xstate.term_layout.add_bar(term_position));
        if (m_tmux_mapping.filled(location)) {
            m_tmux_mapping[location.first].set_position(
                m_xstate, location.second, gui_position);
        }
    }

    std::lock_guard<std::mutex> lock() {
        return std::lock_guard(m_event_mutex);
    }

    void sync() { XSync(m_xstate.display, 0); }

  private:
    std::mutex m_event_mutex;

    // State
    XState m_xstate;
    TmuxXWindowMapping m_tmux_mapping;

    std::queue<Msg> m_msg_q;

    std::queue<Window> m_window_q;
    std::unordered_set<Window> m_pending_windows;

    std::atomic<bool> m_stop = false;
    static bool m_existing_wm;

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

    int open_term() {
        // TODO: make configurable
        std::string root_term_cmd = "kitty --detach";
        root_term_cmd.append(" --class ");
        root_term_cmd.append(ROOT_CLASS);
        root_term_cmd.append(" --exec ~/git/xwmux/scripts/init_term.sh");
        return std::system(root_term_cmd.c_str());
    }

    // Check if window is a root terminal.
    constexpr bool is_root_term(Window id) {
        XClassHint *hint = XAllocClassHint();
        bool ret = XGetClassHint(m_xstate.display, id, hint)
                       ? std::strcmp(hint->res_class, ROOT_CLASS.c_str()) == 0
                       : false;
        XFree(hint);
        return ret;
    }

    constexpr bool override_redirect(Window id) {
        XWindowAttributes attr;
        XGetWindowAttributes(m_xstate.display, id, &attr);
        return attr.override_redirect;
    }

    // If the window has appropriate class name,
    void set_term(Window id) {

        // Close current window
        if (m_xstate.term) {
            XKillClient(m_xstate.display, m_xstate.term.value());
        }

        // Use new window as root
        m_xstate.term = id;
    }

    void handle_event(XEvent ev) {
        switch (ev.type) {
        case CreateNotify:
            break;
        case ConfigureRequest:
            break;
        case ConfigureNotify:
            break;
        case MapRequest:
            [&](Window w) {
                if (override_redirect(w)) {
                } else if (is_root_term(w)) {
                    m_xstate.resolution.fullscreen().resize_to(m_xstate.display,
                                                               w);
                    XLowerWindow(m_xstate.display, w);
                    XMapWindow(m_xstate.display, w);
                    m_xstate.set_term(w);
                    m_xstate.focus_term();
                } else {
                    m_window_q.push(w);
                    m_pending_windows.insert(w);
                    split_window();
                }
            }(ev.xmaprequest.window);
            break;
        case ReparentNotify:
            break;
        case MapNotify:
            break;
        case Expose:
            break;
        case UnmapNotify:
            break;
        case DestroyNotify:
            if (ev.xdestroywindow.window == m_xstate.term) {
                m_xstate.term = {};
                open_term();
                m_xstate.focus_term();
            } else if (!m_tmux_mapping.has_window(ev.xdestroywindow.window)) {
                m_pending_windows.erase(ev.xdestroywindow.window);
                // Not focused yet, do not focus terminal
            } else {
                m_tmux_mapping.remove_window(ev.xdestroywindow.window);
                m_xstate.focus_term();
            }
            break;
        case KeyPress:
            break;

        case ClientMessage:
            // Handle ipc message
            m_event_mutex.lock();
            if (!m_msg_q.empty()) {
                Msg msg = m_msg_q.front();
                m_msg_q.pop();
                m_event_mutex.unlock();

                switch (msg.type) {
                case MsgType::RESOLUTION:
                    set_term_resolution(msg.msg.resolution);
                    break;
                case MsgType::EXIT:
                    stop();
                    pthread_exit(EXIT_SUCCESS);
                    break;
                case MsgType::KILL_PANE:
                    kill_pane_client();
                case MsgType::TMUX_POSITION:
                    if (msg.msg.pane_position.focused) {
                        focus_tmux(msg.msg.pane_position.location);
                    }
                    set_position(msg.msg.pane_position.location,
                                 msg.msg.pane_position.position);
                }
            } else {
                m_event_mutex.unlock();
            }
            break;
        default:
            break;
        }
    }
};
